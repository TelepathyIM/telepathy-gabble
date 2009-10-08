/*
 * gabble-presence-cache.c - Gabble's contact presence cache
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "presence-cache.h"
#include "vcard-manager.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>

/* When five DIFFERENT guys report the same caps for a given bundle, it'll
 * be enough. But if only ONE guy use the verification string (XEP-0115 v1.5),
 * it'll be enough too.
 */
#define CAPABILITY_BUNDLE_ENOUGH_TRUST 5
#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/intset.h>

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "capabilities.h"
#include "caps-channel-manager.h"
#include "caps-hash.h"
#include "debug.h"
#include "disco.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"
#include "roster.h"
#include "types.h"

/* Time period from the cache creation in which we're unsure whether we
 * got initial presence from all the contacts. */
#define UNSURE_PERIOD (5 * G_USEC_PER_SEC)

G_DEFINE_TYPE (GabblePresenceCache, gabble_presence_cache, G_TYPE_OBJECT);

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

/* signal enum */
enum
{
  PRESENCES_UPDATED,
  NICKNAME_UPDATE,
  CAPABILITIES_UPDATE,
  AVATAR_UPDATE,
  CAPABILITIES_DISCOVERED,
  LOCATION_UPDATED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define GABBLE_PRESENCE_CACHE_PRIV(account) ((account)->priv)

struct _GabblePresenceCachePrivate
{
  GabbleConnection *conn;

  gulong status_changed_cb;
  LmMessageHandler *lm_message_cb;

  GHashTable *presence;
  TpHandleSet *presence_handles;

  GHashTable *capabilities;
  GHashTable *disco_pending;
  guint caps_serial;

  GTimeVal creation_time;

  /* The cached contacts' location.
   * The key is the contact's TpHandle.
   * The value is a GHashTable of the user's location:
   *   - the key is a gchar* as per XEP-0080
   *   - the value is a slice allocation GValue, the exact
   *     type depends on the key.
   */
  GHashTable *location;

  /* Are we resetting the image hash as per XEP-0153 section 4.4 */
  gboolean avatar_reset_pending;

  gboolean dispose_has_run;
};

typedef struct _DiscoWaiter DiscoWaiter;

struct _DiscoWaiter
{
  TpHandleRepoIface *repo;
  TpHandle handle;
  gchar *resource;
  guint serial;
  gboolean disco_requested;
  gchar *hash;
  gchar *ver;
};

/**
 * disco_waiter_new ()
 */
static DiscoWaiter *
disco_waiter_new (TpHandleRepoIface *repo,
                  TpHandle handle,
                  const gchar *resource,
                  const gchar *hash,
                  const gchar *ver,
                  guint serial)
{
  DiscoWaiter *waiter;

  g_assert (repo);
  tp_handle_ref (repo, handle);

  waiter = g_slice_new0 (DiscoWaiter);
  waiter->repo = repo;
  waiter->handle = handle;
  waiter->resource = g_strdup (resource);
  waiter->hash = g_strdup (hash);
  waiter->ver = g_strdup (ver);
  waiter->serial = serial;

  DEBUG ("created waiter %p for handle %u with serial %u", waiter, handle,
      serial);

  return waiter;
}

static void
disco_waiter_free (DiscoWaiter *waiter)
{
  g_assert (NULL != waiter);

  DEBUG ("freeing waiter %p for handle %u with serial %u", waiter,
      waiter->handle, waiter->serial);

  tp_handle_unref (waiter->repo, waiter->handle);

  g_free (waiter->resource);
  g_free (waiter->hash);
  g_free (waiter->ver);
  g_slice_free (DiscoWaiter, waiter);
}

static void
disco_waiter_list_free (GSList *list)
{
  GSList *i;

  DEBUG ("list %p", list);

  for (i = list; NULL != i; i = i->next)
    disco_waiter_free ((DiscoWaiter *) i->data);

  g_slist_free (list);
}

static guint
disco_waiter_list_get_request_count (GSList *list)
{
  guint c = 0;
  GSList *i;

  for (i = list; i; i = i->next)
    {
      DiscoWaiter *waiter = (DiscoWaiter *) i->data;

      if (waiter->disco_requested)
        {
          if (!tp_strdiff (waiter->hash, "sha-1"))
            /* One waiter is enough if
             * 1. the request has a verification string
             * 2. the hash algorithm is supported
             */
            c += CAPABILITY_BUNDLE_ENOUGH_TRUST;
          else
            c++;
        }
    }

  return c;
}

typedef struct _CapabilityInfo CapabilityInfo;

struct _CapabilityInfo
{
  /* struct _CapabilityInfo can be allocated before receiving the contact's
   * caps. In this case, caps_set is FALSE and set to TRUE when the caps are
   * received */
  gboolean caps_set;
  GabblePresenceCapabilities caps;

  /* key: GabbleCapsChannelFactory -> value: gpointer
   *
   * The type of the value depends on the GabbleCapsChannelFactory. It is an
   * opaque pointer used by the channel manager to store the capabilities.
   * Some channel manager do not need to store anything, in this case the
   * value can just be NULL.
   *
   * Since the type of the value is not public, the value is allocated, copied
   * and freed by helper functions on the GabbleCapsChannelManager interface.
   *
   * For example:
   *   * GabblePrivateTubesFactory -> TubesCapabilities
   *
   * At the moment, only GabblePrivateTubesFactory use this mechanism to store
   * the list of supported tube types (example: stream tube for daap).
   */
  GHashTable *per_channel_manager_caps;

  TpIntSet *guys;
  guint trust;

  /* TRUE if this cache entry is one of our own, so between caps and
   * per_channel_manager_caps it holds the complete set of features for the
   * node.
   */
  gboolean complete;
};

static CapabilityInfo *
capability_info_get (GabblePresenceCache *cache, const gchar *node)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  CapabilityInfo *info = g_hash_table_lookup (priv->capabilities, node);

  if (NULL == info)
    {
      info = g_slice_new0 (CapabilityInfo);
      info->caps_set = FALSE;
      info->guys = tp_intset_new ();
      g_hash_table_insert (priv->capabilities, g_strdup (node), info);
    }

  return info;
}

static void
capability_info_free (CapabilityInfo *info)
{
  gabble_presence_cache_free_cache_entry (info->per_channel_manager_caps);
  info->per_channel_manager_caps = NULL;
  tp_intset_destroy (info->guys);
  g_slice_free (CapabilityInfo, info);
}

static guint
capability_info_recvd (GabblePresenceCache *cache, const gchar *node,
        TpHandle handle, GabblePresenceCapabilities caps,
        GHashTable *per_channel_manager_caps, guint trust_inc)
{
  CapabilityInfo *info = capability_info_get (cache, node);

  if (info->caps != caps || ! info->caps_set)
    {
      /* The caps are not valid, either because we detected inconsistency
       * between several contacts using the same node (when the hash is not
       * used), or because this is the first caps report and the caps were
       * never set.
       */
      tp_intset_clear (info->guys);
      info->caps = caps;
      info->per_channel_manager_caps = per_channel_manager_caps;
      info->trust = 0;
      info->caps_set = TRUE;
    }

  if (!tp_intset_is_member (info->guys, handle))
    {
      tp_intset_add (info->guys, handle);
      info->trust += trust_inc;
    }

  return info->trust;
}

static void gabble_presence_cache_init (GabblePresenceCache *presence_cache);
static GObject * gabble_presence_cache_constructor (GType type, guint n_props,
    GObjectConstructParam *props);
static void gabble_presence_cache_dispose (GObject *object);
static void gabble_presence_cache_finalize (GObject *object);
static void gabble_presence_cache_set_property (GObject *object, guint
    property_id, const GValue *value, GParamSpec *pspec);
static void gabble_presence_cache_get_property (GObject *object, guint
    property_id, GValue *value, GParamSpec *pspec);
static GabblePresence *_cache_insert (GabblePresenceCache *cache,
    TpHandle handle);

static void gabble_presence_cache_status_changed_cb (GabbleConnection *,
    TpConnectionStatus, TpConnectionStatusReason, gpointer);
static LmHandlerResult gabble_presence_cache_lm_message_cb (LmMessageHandler*,
    LmConnection*, LmMessage*, gpointer);

static void
gabble_presence_cache_class_init (GabblePresenceCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (object_class, sizeof (GabblePresenceCachePrivate));

  object_class->constructor = gabble_presence_cache_constructor;

  object_class->dispose = gabble_presence_cache_dispose;
  object_class->finalize = gabble_presence_cache_finalize;

  object_class->get_property = gabble_presence_cache_get_property;
  object_class->set_property = gabble_presence_cache_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this presence cache.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
                                   PROP_CONNECTION,
                                   param_spec);

  signals[PRESENCES_UPDATED] = g_signal_new (
    "presences-updated",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_UINT_ARRAY);
  signals[NICKNAME_UPDATE] = g_signal_new (
    "nickname-update",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
  signals[CAPABILITIES_UPDATE] = g_signal_new (
    "capabilities-update",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    gabble_marshal_VOID__UINT_UINT_UINT_POINTER_POINTER, G_TYPE_NONE,
    5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER);
  signals[AVATAR_UPDATE] = g_signal_new (
    "avatar-update",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT_POINTER, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);
  signals[CAPABILITIES_DISCOVERED] = g_signal_new (
    "capabilities-discovered",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT, G_TYPE_NONE,
    1, G_TYPE_UINT);
  signals[LOCATION_UPDATED] = g_signal_new (
    "location-update",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
gabble_presence_cache_init (GabblePresenceCache *cache)
{
  GabblePresenceCachePrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (cache,
      GABBLE_TYPE_PRESENCE_CACHE, GabblePresenceCachePrivate);

  cache->priv = priv;

  priv->presence = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
  priv->capabilities = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) capability_info_free);
  priv->disco_pending = g_hash_table_new_full (g_str_hash, g_str_equal,
    g_free, (GDestroyNotify) disco_waiter_list_free);
  priv->caps_serial = 1;

  priv->location = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_hash_table_destroy);
}

static GObject *
gabble_presence_cache_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabblePresenceCachePrivate *priv;

  obj = G_OBJECT_CLASS (gabble_presence_cache_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_PRESENCE_CACHE_PRIV (GABBLE_PRESENCE_CACHE (obj));

  priv->status_changed_cb = g_signal_connect (priv->conn, "status-changed",
      G_CALLBACK (gabble_presence_cache_status_changed_cb), obj);

  g_get_current_time (&priv->creation_time);

  return obj;
}

static void
gabble_presence_cache_dispose (GObject *object)
{
  GabblePresenceCache *self = GABBLE_PRESENCE_CACHE (object);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (self);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");

  priv->dispose_has_run = TRUE;

  g_assert (priv->lm_message_cb == NULL);

  g_signal_handler_disconnect (priv->conn, priv->status_changed_cb);

  g_hash_table_destroy (priv->presence);
  priv->presence = NULL;

  g_hash_table_destroy (priv->capabilities);
  priv->capabilities = NULL;

  g_hash_table_destroy (priv->disco_pending);
  priv->disco_pending = NULL;

  tp_handle_set_destroy (priv->presence_handles);
  priv->presence_handles = NULL;

  g_hash_table_destroy (priv->location);
  priv->location = NULL;

  if (G_OBJECT_CLASS (gabble_presence_cache_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_presence_cache_parent_class)->dispose (object);
}

static void
gabble_presence_cache_finalize (GObject *object)
{
  DEBUG ("called with %p", object);

  G_OBJECT_CLASS (gabble_presence_cache_parent_class)->finalize (object);
}

static void
gabble_presence_cache_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GabblePresenceCache *cache = GABBLE_PRESENCE_CACHE (object);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_presence_cache_set_property (GObject     *object,
                                    guint        property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GabblePresenceCache *cache = GABBLE_PRESENCE_CACHE (object);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpHandleRepoIface *contact_repo;
  TpHandleSet *new_presence_handles;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

      new_presence_handles = tp_handle_set_new (contact_repo);

      if (priv->presence_handles)
        {
          const TpIntSet *add;
          TpIntSet *tmp;
          add = tp_handle_set_peek (priv->presence_handles);
          tmp = tp_handle_set_update (new_presence_handles, add);
          tp_handle_set_destroy (priv->presence_handles);
          tp_intset_destroy (tmp);
        }
      priv->presence_handles = new_presence_handles;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_presence_cache_status_changed_cb (GabbleConnection *conn,
                                         TpConnectionStatus status,
                                         TpConnectionStatusReason reason,
                                         gpointer data)
{
  GabblePresenceCache *cache = GABBLE_PRESENCE_CACHE (data);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  g_assert (conn == priv->conn);

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_assert (priv->lm_message_cb == NULL);

      priv->lm_message_cb = lm_message_handler_new (
          gabble_presence_cache_lm_message_cb, cache, NULL);
      lm_connection_register_message_handler (priv->conn->lmconn,
                                              priv->lm_message_cb,
                                              LM_MESSAGE_TYPE_PRESENCE,
                                              LM_HANDLER_PRIORITY_LAST);
      lm_connection_register_message_handler (priv->conn->lmconn,
                                              priv->lm_message_cb,
                                              LM_MESSAGE_TYPE_MESSAGE,
                                              LM_HANDLER_PRIORITY_FIRST);
      break;
    case TP_CONNECTION_STATUS_CONNECTED:
      break;
    case TP_CONNECTION_STATUS_DISCONNECTED:
      if (priv->lm_message_cb != NULL)
        {
          lm_connection_unregister_message_handler (conn->lmconn,
                                                    priv->lm_message_cb,
                                                    LM_MESSAGE_TYPE_PRESENCE);
          lm_connection_unregister_message_handler (conn->lmconn,
                                                    priv->lm_message_cb,
                                                    LM_MESSAGE_TYPE_MESSAGE);
          lm_message_handler_unref (priv->lm_message_cb);
          priv->lm_message_cb = NULL;
        }
      break;
    default:
      g_assert_not_reached ();
    }
}

static GabblePresenceId
_presence_node_get_status (LmMessageNode *pres_node)
{
  const gchar *presence_show;
  LmMessageNode *child_node = lm_message_node_get_child (pres_node, "show");

  if (!child_node)
    {
      /*
      NODE_DEBUG (pres_node,
        "<presence> without <show> received from server, "
        "setting presence to available");
      */
      return GABBLE_PRESENCE_AVAILABLE;
    }

  presence_show = lm_message_node_get_value (child_node);

  if (!presence_show)
    {
      /*
      NODE_DEBUG (pres_node,
        "empty <show> tag received from server, "
        "setting presence to available");
      */
      return GABBLE_PRESENCE_AVAILABLE;
    }

  if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_AWAY))
    return GABBLE_PRESENCE_AWAY;
  else if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_CHAT))
    return GABBLE_PRESENCE_CHAT;
  else if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_DND))
    return GABBLE_PRESENCE_DND;
  else if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_XA))
    return GABBLE_PRESENCE_XA;
  else
    {
       NODE_DEBUG (pres_node,
        "unrecognised <show/> value received from server, "
        "setting presence to available");
      return GABBLE_PRESENCE_AVAILABLE;
    }
}

static void
_grab_nickname (GabblePresenceCache *cache,
                TpHandle handle,
                const gchar *from,
                LmMessageNode *node)
{
  const gchar *nickname;
  GabblePresence *presence;

  node = lm_message_node_get_child_with_namespace (node, "nick", NS_NICK);

  if (NULL == node)
    return;

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL == presence)
    return;

  nickname = lm_message_node_get_value (node);
  DEBUG ("got nickname \"%s\" for %s", nickname, from);

  if (tp_strdiff (presence->nickname, nickname))
    {
      g_free (presence->nickname);
      presence->nickname = g_strdup (nickname);
      g_signal_emit (cache, signals[NICKNAME_UPDATE], 0, handle);
    }
}

static void
self_vcard_request_cb (GabbleVCardManager *self,
                       GabbleVCardManagerRequest *request,
                       TpHandle handle,
                       LmMessageNode *vcard,
                       GError *error,
                       gpointer user_data)
{
  GabblePresenceCache *cache = user_data;
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  gchar *sha1 = NULL;

  priv->avatar_reset_pending = FALSE;

  if (vcard != NULL)
    {
      sha1 = vcard_get_avatar_sha1 (vcard);

      /* FIXME: presence->avatar_sha1 is resetted in
       * self_avatar_resolve_conflict() and the following signal set it in
       * conn-avatars.c. Doing that in 2 different files is confusing.
       */
      g_signal_emit (cache, signals[AVATAR_UPDATE], 0, handle, sha1);

      g_free (sha1);
    }
  DEBUG ("End of avatar conflict resolution");
}

static void
self_avatar_resolve_conflict (GabblePresenceCache *cache)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
  GabblePresence *presence = priv->conn->self_presence;
  GError *error = NULL;

  /* We don't want recursive image resetting
   *
   * FIXME: There is a race here: if the other resource sends us first the
   * hash 'hash1', and then 'hash2' while the vCard request generated for
   * 'hash1' is still pending, the current code doesn't send a new vCard
   * request, although it should because Gabble cannot know whether the reply
   * will be for hash1 or hash2. The good solution would be to store the
   * received hash and each time the hash is different, cancel the previous
   * vCard request and send a new one. However, this is tricky, so we don't
   * implement it.
   *
   * This race is not so bad: the only bad consequence is if the other Jabber
   * client changes the avatar twice quickly, we may get only the first one.
   * The real contacts should still get the last avatar.
   */
  if (priv->avatar_reset_pending)
    {
      DEBUG ("There is already an avatar conflict resolution pending.");
      return;
    }

  /* according to XEP-0153 section 4.3-2. 3rd bullet:
   * if we receive a photo from another resource, then we MUST
   * immediately send a presence update with an empty update child
   * element (no photo node), then re-download our own vCard;
   * when that arrives, we may start setting the photo node in our
   * presence again.
   */
  DEBUG ("Reset our avatar, signal our presence without an avatar and request"
         " our own vCard.");
  priv->avatar_reset_pending = TRUE;
  g_free (presence->avatar_sha1);
  presence->avatar_sha1 = NULL;
  if (!_gabble_connection_signal_own_presence (priv->conn, &error))
    {
      DEBUG ("failed to send own presence: %s", error->message);
      g_error_free (error);
    }

  gabble_vcard_manager_invalidate_cache (priv->conn->vcard_manager,
      base_conn->self_handle);
  gabble_vcard_manager_request (priv->conn->vcard_manager,
      base_conn->self_handle, 0, self_vcard_request_cb, cache,
      NULL);
}

static void
_grab_avatar_sha1 (GabblePresenceCache *cache,
                   TpHandle handle,
                   const gchar *from,
                   LmMessageNode *node)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
  const gchar *sha1;
  LmMessageNode *x_node, *photo_node;
  GabblePresence *presence;

  if (handle == base_conn->self_handle)
    presence = priv->conn->self_presence;
  else
    presence = gabble_presence_cache_get (cache, handle);

  if (NULL == presence)
    return;

  x_node = lm_message_node_get_child_with_namespace (node, "x",
      NS_VCARD_TEMP_UPDATE);

  if (NULL == x_node)
    {
      /* If (handle == base_conn->self_handle), then this means
       * that one of our other resources does not support XEP-0153. According
       * to that XEP, we MUST now stop advertising the image hash, at least
       * until all instances of non-conforming resources have gone offline, by
       * setting presence->avatar_sha1 to NULL.
       *
       * However, this would mean that logging in (e.g.) with an old version
       * of Gabble would disable avatars in this newer version, which is
       * quite a silly failure mode. As a result, we ignore this
       * requirement and hope that non-conforming clients won't alter the
       * <PHOTO>, which should in practice be true.
       *
       * If handle != self_handle, then in any case we want to ignore this
       * message for vCard purposes. */
      return;
    }

  photo_node = lm_message_node_get_child (x_node, "photo");

  /* If there is no photo node, the resource supports XEP-0153, but has
   * nothing in particular to say about the avatar. */
  if (NULL == photo_node)
    return;

  sha1 = lm_message_node_get_value (photo_node);

  /* "" means we know there is no avatar. NULL means we don't know what is the
   * avatar. In this case, there is a <photo> node. */
  if (sha1 == NULL)
    sha1 = "";

  if (tp_strdiff (presence->avatar_sha1, sha1))
    {
      if (handle == base_conn->self_handle)
        {
          DEBUG ("Avatar conflict! Received hash '%s' and our cache is '%s'",
            sha1, presence->avatar_sha1 == NULL ?
              "<NULL>" : presence->avatar_sha1);
          self_avatar_resolve_conflict (cache);
        }
      else
        {
          g_free (presence->avatar_sha1);
          presence->avatar_sha1 = g_strdup (sha1);
          gabble_vcard_manager_invalidate_cache (priv->conn->vcard_manager, handle);
          g_signal_emit (cache, signals[AVATAR_UPDATE], 0, handle, sha1);
        }
    }
}

static GSList *
_parse_cap_bundles (
    LmMessageNode *lm_node,
    const gchar **hash,
    const gchar **ver)
{
  const gchar *node, *ext;
  GSList *uris = NULL;
  LmMessageNode *cap_node;

  *hash = NULL;
  *ver = NULL;

  cap_node = lm_message_node_get_child_with_namespace (lm_node, "c", NS_CAPS);

  if (NULL == cap_node)
      return NULL;

  *hash = lm_message_node_get_attribute (cap_node, "hash");

  node = lm_message_node_get_attribute (cap_node, "node");

  if (NULL == node)
    return NULL;

  *ver = lm_message_node_get_attribute (cap_node, "ver");

  if (NULL != *ver)
    uris = g_slist_prepend (uris, g_strdup_printf ("%s#%s", node, *ver));

  /* If there is a hash, the remote contact uses XEP-0115 v1.5 and the 'ext'
   * attribute MUST be ignored. */
  if (NULL != *hash)
    return uris;

  ext = lm_message_node_get_attribute (cap_node, "ext");

  if (NULL != ext)
    {
      gchar **exts, **i;

      exts = g_strsplit (ext, " ", 0);

      for (i = exts; NULL != *i; i++)
        uris = g_slist_prepend (uris, g_strdup_printf ("%s#%s", node, *i));

      g_strfreev (exts);
    }

  return uris;
}

static void
free_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GabbleCapsChannelManager *manager = GABBLE_CAPS_CHANNEL_MANAGER (key);
  gabble_caps_channel_manager_free_capabilities (manager, value);
}

void
gabble_presence_cache_free_cache_entry (
    GHashTable *per_channel_manager_caps)
{
  if (per_channel_manager_caps == NULL)
    return;

  g_hash_table_foreach (per_channel_manager_caps, free_caps_helper,
      NULL);
  g_hash_table_destroy (per_channel_manager_caps);
}

static void
copy_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *table_out = user_data;
  GabbleCapsChannelManager *manager = GABBLE_CAPS_CHANNEL_MANAGER (key);
  gpointer out;
  gabble_caps_channel_manager_copy_capabilities (manager, &out, value);
  g_hash_table_insert (table_out, key, out);
}

void
gabble_presence_cache_copy_cache_entry (
    GHashTable **out, GHashTable *in)
{
  *out = g_hash_table_new (NULL, NULL);
  if (in != NULL)
    g_hash_table_foreach (in, copy_caps_helper,
        *out);
}

static void
update_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *table_out = user_data;
  GabbleCapsChannelManager *manager = GABBLE_CAPS_CHANNEL_MANAGER (key);
  gpointer out;

  out = g_hash_table_lookup (table_out, key);
  if (out == NULL)
    {
      gabble_caps_channel_manager_copy_capabilities (manager, &out, value);
      g_hash_table_insert (table_out, key, out);
    }
  else
    {
      gabble_caps_channel_manager_update_capabilities (manager, out, value);
    }
}

void
gabble_presence_cache_update_cache_entry (
    GHashTable *out, GHashTable *in)
{
  g_return_if_fail (out != NULL);

  if (in != NULL)
    g_hash_table_foreach (in, update_caps_helper, out);
}

static void _caps_disco_cb (GabbleDisco *disco,
    GabbleDiscoRequest *request,
    const gchar *jid,
    const gchar *node,
    LmMessageNode *query_result,
    GError *error,
    gpointer user_data);

static void
redisco (GabblePresenceCache *cache,
    GabbleDisco *disco,
    DiscoWaiter *waiter,
    const gchar *node)
{
  const gchar *waiter_jid;
  gchar *full_jid;

  waiter_jid = tp_handle_inspect (waiter->repo, waiter->handle);
  if (waiter->resource != NULL)
    full_jid = g_strdup_printf ("%s/%s", waiter_jid, waiter->resource);
  else
    full_jid = g_strdup (waiter_jid);

  gabble_disco_request (disco, GABBLE_DISCO_TYPE_INFO, full_jid,
      node, _caps_disco_cb, cache, G_OBJECT (cache), NULL);
  waiter->disco_requested = TRUE;

  g_free (full_jid);
}

static void
disco_failed (GabblePresenceCache *cache,
    GabbleDisco *disco,
    const gchar *node,
    GSList *waiters)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  GSList *i;
  DiscoWaiter *waiter = NULL;
  gchar *full_jid = NULL;

  for (i = waiters; NULL != i; i = i->next)
    {
      waiter = (DiscoWaiter *) i->data;

      if (!waiter->disco_requested)
        {
          redisco (cache, disco, waiter, node);
          break;
        }
    }

  if (NULL != i)
    {
      DEBUG ("sent a retry disco request to %s for URI %s", full_jid, node);
    }
  else
    {
      /* The contact sends us an error and we don't have any other
       * contacts to send the discovery request on the same node. We
       * cannot get the caps for this node. */
      DEBUG ("failed to find a suitable candidate to retry disco "
          "request for URI %s", node);
      g_hash_table_remove (priv->disco_pending, node);
    }

  g_free (full_jid);
}

static DiscoWaiter *
find_matching_waiter (GSList *waiters,
    TpHandle godot,
    const gchar *resource)
{
  GSList *i;

  for (i = waiters; NULL != i; i = i->next)
    {
      DiscoWaiter *waiter = i->data;

      if (waiter->handle == godot && !tp_strdiff (waiter->resource, resource))
        return waiter;
    }

  return NULL;
}

static GHashTable *
parse_contact_caps (TpBaseConnection *base_conn,
    LmMessageNode *query_result)
{
  GHashTable *per_channel_manager_caps = g_hash_table_new (NULL, NULL);
  TpChannelManagerIter iter;
  TpChannelManager *manager;

  tp_base_connection_channel_manager_iter_init (&iter, base_conn);

  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      gpointer factory_caps;

      /* all channel managers must implement the capability interface */
      g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

      factory_caps = gabble_caps_channel_manager_parse_capabilities (
          GABBLE_CAPS_CHANNEL_MANAGER (manager), query_result);

      if (factory_caps != NULL)
        g_hash_table_insert (per_channel_manager_caps,
            GABBLE_CAPS_CHANNEL_MANAGER (manager), factory_caps);
    }

  return per_channel_manager_caps;
}

static void
emit_capabilities_update (GabblePresenceCache *cache,
    TpHandle handle,
    GabblePresenceCapabilities old_caps,
    GabblePresenceCapabilities new_caps,
    GHashTable *old_enhanced_caps,
    GHashTable *new_enhanced_caps)
{
  g_signal_emit (cache, signals[CAPABILITIES_UPDATE], 0,
      handle, old_caps, new_caps, old_enhanced_caps, new_enhanced_caps);
}

/**
 * set_caps_for:
 *
 * Sets caps for @waiter to (@caps, @per_channel_manager_caps), having received
 * a trusted reply from @responder_{handle,jid}.
 */
static void
set_caps_for (DiscoWaiter *waiter,
    GabblePresenceCache *cache,
    GabblePresenceCapabilities caps,
    GHashTable *per_channel_manager_caps,
    TpHandle responder_handle,
    const gchar *responder_jid)
{
  GabblePresence *presence = gabble_presence_cache_get (cache, waiter->handle);
  GabblePresenceCapabilities save_caps;
  GHashTable *save_enhanced_caps;

  if (presence == NULL)
    return;

  save_caps = presence->caps;
  gabble_presence_cache_copy_cache_entry (&save_enhanced_caps,
      presence->per_channel_manager_caps);

  DEBUG ("setting caps for %d (thanks to %d %s) to %d (save_caps %d)",
      waiter->handle, responder_handle, responder_jid, caps, save_caps);

  gabble_presence_set_capabilities (presence, waiter->resource,
      caps, per_channel_manager_caps, waiter->serial);

  DEBUG ("caps for %d now %d", waiter->handle, presence->caps);

  emit_capabilities_update (cache, waiter->handle, save_caps, presence->caps,
      save_enhanced_caps, presence->per_channel_manager_caps);
  gabble_presence_cache_free_cache_entry (save_enhanced_caps);
}

static void
emit_capabilities_discovered (GabblePresenceCache *cache,
    TpHandle handle)
{
  g_signal_emit (cache, signals[CAPABILITIES_DISCOVERED], 0, handle);
}

static void
_caps_disco_cb (GabbleDisco *disco,
                GabbleDiscoRequest *request,
                const gchar *jid,
                const gchar *node,
                LmMessageNode *query_result,
                GError *error,
                gpointer user_data)
{
  GSList *waiters, *i;
  DiscoWaiter *waiter_self;
  GabblePresenceCache *cache;
  GabblePresenceCachePrivate *priv;
  TpHandleRepoIface *contact_repo;
  GabblePresenceCapabilities caps = 0;
  GHashTable *per_channel_manager_caps;
  guint trust;
  TpHandle handle = 0;
  gboolean bad_hash = FALSE;
  TpBaseConnection *base_conn;
  gchar *resource;
  gboolean jid_is_valid;

  cache = GABBLE_PRESENCE_CACHE (user_data);
  priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  base_conn = TP_BASE_CONNECTION (priv->conn);
  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  if (NULL == node)
    {
      DEBUG ("got disco response with NULL node, ignoring");
      return;
    }

  waiters = g_hash_table_lookup (priv->disco_pending, node);

  if (NULL != error)
    {
      DEBUG ("disco query failed: %s", error->message);

      disco_failed (cache, disco, node, waiters);

      return;
    }

  handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);

  if (handle == 0)
    {
      DEBUG ("Ignoring presence from invalid JID %s", jid);
      return;
    }

  /* If tp_handle_ensure () was happy with the jid, it's valid. */
  jid_is_valid = gabble_decode_jid (jid, NULL, NULL, &resource);
  g_assert (jid_is_valid);
  waiter_self = find_matching_waiter (waiters, handle, resource);
  g_free (resource);

  if (NULL == waiter_self)
    {
      DEBUG ("Ignoring non requested disco reply from %s", jid);
      goto OUT;
    }

  caps = capabilities_parse (query_result);
  per_channel_manager_caps = parse_contact_caps (base_conn, query_result);

  /* Only 'sha-1' is mandatory to implement by XEP-0115. If the remote contact
   * uses another hash algorithm, don't check the hash and fallback to the old
   * method. The hash method is not included in the discovery request nor
   * response but we saved it in disco_pending when we received the presence
   * stanza. */
  if (!tp_strdiff (waiter_self->hash, "sha-1"))
    {
      gchar *computed_hash;

      computed_hash = caps_hash_compute_from_lm_node (query_result);

      if (g_str_equal (waiter_self->ver, computed_hash))
        {
          trust = capability_info_recvd (cache, node, handle, caps,
              per_channel_manager_caps, CAPABILITY_BUNDLE_ENOUGH_TRUST);
        }
      else
        {
          DEBUG ("The verification string '%s' announced by '%s' does not "
              "match our hash of their disco reply '%s'.", waiter_self->ver,
              jid, computed_hash);
          trust = 0;
          bad_hash = TRUE;
          gabble_presence_cache_free_cache_entry (per_channel_manager_caps);
          per_channel_manager_caps = NULL;
        }

      g_free (computed_hash);
    }
  else
    {
      trust = capability_info_recvd (cache, node, handle, caps,
          per_channel_manager_caps, 1);
    }

  if (trust >= CAPABILITY_BUNDLE_ENOUGH_TRUST)
    {
      /* We trust this caps node. Serve all its waiters. */
      for (i = waiters; NULL != i; i = i->next)
        {
          DiscoWaiter *waiter = (DiscoWaiter *) i->data;

          set_caps_for (waiter, cache, caps, per_channel_manager_caps, handle,
              jid);
          emit_capabilities_discovered (cache, waiter->handle);
        }

      g_hash_table_remove (priv->disco_pending, node);
    }
  else
    {
      gpointer key;
      /* We don't trust this yet (either the hash was bad, or we haven't had
       * enough responses, as appropriate).
       */

      /* Set caps for the contact that replied (if the hash was correct) and
       * remove them from the list of waiters.
       * FIXME I think we should respect the caps, even if the hash is wrong,
       *       for the jid that answered the query.
       */
      if (!bad_hash)
        set_caps_for (waiter_self, cache, caps, per_channel_manager_caps,
            handle, jid);

      waiters = g_slist_remove (waiters, waiter_self);

      if (!g_hash_table_lookup_extended (priv->disco_pending, node, &key, NULL))
        g_assert_not_reached ();

      g_hash_table_steal (priv->disco_pending, key);
      g_hash_table_insert (priv->disco_pending, key, waiters);

      emit_capabilities_discovered (cache, waiter_self->handle);
      disco_waiter_free (waiter_self);

      /* Ensure that we have enough pending requests to get enough trust for
       * this node.
       */
      for (i = waiters; i != NULL; i = i->next)
        {
          DiscoWaiter *waiter = (DiscoWaiter *) i->data;

          if (trust + disco_waiter_list_get_request_count (waiters)
              >= CAPABILITY_BUNDLE_ENOUGH_TRUST)
            break;

          if (!waiter->disco_requested)
            redisco (cache, disco, waiter, node);
        }
    }

OUT:
  if (handle)
    tp_handle_unref (contact_repo, handle);
}

static void
_process_caps_uri (GabblePresenceCache *cache,
                   const gchar *from,
                   const gchar *uri,
                   const gchar *hash,
                   const gchar *ver,
                   TpHandle handle,
                   const gchar *resource,
                   guint serial)
{
  CapabilityInfo *info;
  GabblePresenceCachePrivate *priv;
  TpHandleRepoIface *contact_repo;

  priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  info = capability_info_get (cache, uri);

  if (info->trust >= CAPABILITY_BUNDLE_ENOUGH_TRUST
      || tp_intset_is_member (info->guys, handle))
    {
      /* we already have enough trust for this node; apply the cached value to
       * the (handle, resource) */

      GabblePresence *presence = gabble_presence_cache_get (cache, handle);
      DEBUG ("enough trust for URI %s, setting caps for %u (%s) to %u",
          uri, handle, from, info->caps);

      if (presence)
        {
          gabble_presence_set_capabilities (presence, resource,
              info->caps, info->per_channel_manager_caps, serial);
          DEBUG ("caps for %d (%s) now %d", handle, from, presence->caps);
        }
      else
        {
          DEBUG ("presence not found");
        }
    }
  else
    {
      GSList *waiters;
      DiscoWaiter *waiter;
      guint possible_trust;
      gboolean found;
      gpointer key;
      gpointer value = NULL;

      DEBUG ("not enough trust for URI %s", uri);

      /* Are we already waiting for responses for this URI? */
      found = g_hash_table_lookup_extended (priv->disco_pending, uri, &key,
          &value);
      waiters = (GSList *) value;

      waiter = find_matching_waiter (waiters, handle, resource);

      if (waiter != NULL)
        {
          /* We've already asked this jid about this node; just update the
           * serial.
           */
          DEBUG ("updating serial for waiter (%s, %s) from %u to %u",
              from, uri, waiter->serial, serial);
          waiter->serial = serial;
          return;
        }

      waiter = disco_waiter_new (contact_repo, handle, resource,
          hash, ver, serial);
      waiters = g_slist_prepend (waiters, waiter);

      /* If the URI was already in the hash table, steal it and re-use the same
       * URI for the following insertion. Otherwise, make a copy of the URI for
       * use as a key.
       */
      if (found)
        g_hash_table_steal (priv->disco_pending, key);
      else
        key = g_strdup (uri);

      g_hash_table_insert (priv->disco_pending, key, waiters);

      /* When all the responses we're waiting for return, will we have enough
       * trust?
       */
      possible_trust = disco_waiter_list_get_request_count (waiters);

      if (info->trust + possible_trust < CAPABILITY_BUNDLE_ENOUGH_TRUST)
        {
          /* DISCO */
          DEBUG ("only %u trust out of %u possible thus far, sending "
              "disco for URI %s", info->trust + possible_trust,
              CAPABILITY_BUNDLE_ENOUGH_TRUST, uri);
          gabble_disco_request (priv->conn->disco, GABBLE_DISCO_TYPE_INFO,
              from, uri, _caps_disco_cb, cache, G_OBJECT (cache), NULL);
          /* enough DISCO for you, buddy */
          waiter->disco_requested = TRUE;
        }
    }
}

static void
_process_caps (GabblePresenceCache *cache,
               GabblePresence *presence,
               TpHandle handle,
               const gchar *from,
               LmMessageNode *lm_node)
{
  const gchar *resource;
  GSList *uris, *i;
  GabblePresenceCachePrivate *priv;
  GabblePresenceCapabilities old_caps = 0;
  GHashTable *old_enhanced_caps;
  guint serial;
  const gchar *hash, *ver;

  priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  serial = priv->caps_serial++;

  resource = strchr (from, '/');
  if (resource != NULL)
    resource++;

  uris = _parse_cap_bundles (lm_node, &hash, &ver);

  if (presence)
    {
      old_caps = presence->caps;
      gabble_presence_cache_copy_cache_entry (&old_enhanced_caps,
          presence->per_channel_manager_caps);
    }

  for (i = uris; NULL != i; i = i->next)
    {
      _process_caps_uri (cache, from, (gchar *) i->data, hash, ver, handle,
          resource, serial);
      g_free (i->data);

    }

  if (presence)
    {
      DEBUG ("Emitting caps update: handle %u, old %u, new %u",
          handle, old_caps, presence->caps);

      emit_capabilities_update (cache, handle, old_caps, presence->caps,
          old_enhanced_caps, presence->per_channel_manager_caps);
      gabble_presence_cache_free_cache_entry (old_enhanced_caps);
    }
  else
    {
      DEBUG ("No change in caps %u for handle %u, not updating",
          presence->caps, handle);
    }

  g_slist_free (uris);
}

static LmHandlerResult
_parse_presence_message (GabblePresenceCache *cache,
                         TpHandle handle,
                         const gchar *from,
                         LmMessage *message)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  gint8 priority = 0;
  const gchar *resource, *status_message = NULL;
  gchar *my_full_jid;
  LmMessageNode *presence_node, *child_node;
  LmHandlerResult ret = LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
  GabblePresenceId presence_id;
  GabblePresence *presence;

  /* The server should not send back the presence stanza about ourself (same
   * resource). If it does, we just ignore the received stanza. We want to
   * avoid any infinite ping-pong with the server due to XEP-0153 4.2-2-3.
   */
  my_full_jid = gabble_connection_get_full_jid (priv->conn);
  if (!tp_strdiff (from, my_full_jid))
    {
      g_free (my_full_jid);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  g_free (my_full_jid);

  presence_node = message->node;
  g_assert (0 == strcmp (presence_node->name, "presence"));

  resource = strchr (from, '/');
  if (resource != NULL)
    resource++;

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL != presence)
      /* Once we've received presence from somebody, we don't need to keep the
       * presence around when it's unavailable. */
      presence->keep_unavailable = FALSE;

  child_node = lm_message_node_get_child (presence_node, "status");

  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  child_node = lm_message_node_get_child (presence_node, "priority");

  if (child_node)
    {
      const gchar *prio = lm_message_node_get_value (child_node);

      if (prio != NULL)
        priority = CLAMP (atoi (prio), G_MININT8, G_MAXINT8);
    }

  switch (lm_message_get_sub_type (message))
    {
    case LM_MESSAGE_SUB_TYPE_NOT_SET:
    case LM_MESSAGE_SUB_TYPE_AVAILABLE:
      presence_id = _presence_node_get_status (presence_node);
      gabble_presence_cache_update (cache, handle, resource, presence_id,
          status_message, priority);

      if (!presence)
          presence = gabble_presence_cache_get (cache, handle);

      _grab_nickname (cache, handle, from, presence_node);
      _grab_avatar_sha1 (cache, handle, from, presence_node);
      _process_caps (cache, presence, handle, from, presence_node);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;

    case LM_MESSAGE_SUB_TYPE_ERROR:
      NODE_DEBUG (presence_node, "Received error presence");
      gabble_presence_cache_update (cache, handle, resource,
          GABBLE_PRESENCE_ERROR, status_message, priority);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;

    case LM_MESSAGE_SUB_TYPE_UNAVAILABLE:
      if (gabble_roster_handle_get_subscription (priv->conn->roster, handle)
        & GABBLE_ROSTER_SUBSCRIPTION_FROM)
        presence_id = GABBLE_PRESENCE_OFFLINE;
      else
        presence_id = GABBLE_PRESENCE_UNKNOWN;

      gabble_presence_cache_update (cache, handle, resource,
          presence_id, status_message, priority);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;

    default:
      break;
    }

  return ret;
}

static LmHandlerResult
_parse_message_message (GabblePresenceCache *cache,
                        TpHandle handle,
                        const gchar *from,
                        LmMessage *message)
{
  LmMessageNode *node;
  GabblePresence *presence;

  switch (lm_message_get_sub_type (message))
    {
    case LM_MESSAGE_SUB_TYPE_NOT_SET:
    case LM_MESSAGE_SUB_TYPE_NORMAL:
    case LM_MESSAGE_SUB_TYPE_CHAT:
    case LM_MESSAGE_SUB_TYPE_GROUPCHAT:
      break;
    default:
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL == presence)
    {
      presence = _cache_insert (cache, handle);
      presence->keep_unavailable = TRUE;
    }

  node = lm_message_get_node (message);

  _grab_nickname (cache, handle, from, node);

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}


/**
 * gabble_presence_cache_lm_message_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
gabble_presence_cache_lm_message_cb (LmMessageHandler *handler,
                                     LmConnection *lmconn,
                                     LmMessage *message,
                                     gpointer user_data)
{
  GabblePresenceCache *cache = GABBLE_PRESENCE_CACHE (user_data);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const char *from;
  LmHandlerResult ret;
  TpHandle handle;

  g_assert (lmconn == priv->conn->lmconn);

  from = lm_message_node_get_attribute (message->node, "from");

  if (NULL == from)
    {
      NODE_DEBUG (message->node, "message without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = tp_handle_ensure (contact_repo, from, NULL, NULL);

  if (0 == handle)
    {
      NODE_DEBUG (message->node, "ignoring message from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  switch (lm_message_get_type (message))
    {
    case LM_MESSAGE_TYPE_PRESENCE:
      ret = _parse_presence_message (cache, handle, from, message);
      break;
    case LM_MESSAGE_TYPE_MESSAGE:
      ret = _parse_message_message (cache, handle, from, message);
      break;
    default:
      ret = LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
      break;
    }

  tp_handle_unref (contact_repo, handle);
  return ret;
}


GabblePresenceCache *
gabble_presence_cache_new (GabbleConnection *conn)
{
  return g_object_new (GABBLE_TYPE_PRESENCE_CACHE,
                       "connection", conn,
                       NULL);
}

GabblePresence *
gabble_presence_cache_get (GabblePresenceCache *cache, TpHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  g_assert (tp_handle_is_valid (contact_repo, handle, NULL));

  return g_hash_table_lookup (priv->presence, GUINT_TO_POINTER (handle));
}

void
gabble_presence_cache_maybe_remove (
    GabblePresenceCache *cache,
    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabblePresence *presence;

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL == presence)
    return;

  if (presence->status == GABBLE_PRESENCE_OFFLINE &&
      presence->status_message == NULL &&
      !presence->keep_unavailable)
    {
      const gchar *jid;

      jid = tp_handle_inspect (contact_repo, handle);
      DEBUG ("discarding cached presence for unavailable jid %s", jid);
      g_hash_table_remove (priv->presence, GUINT_TO_POINTER (handle));
      tp_handle_set_remove (priv->presence_handles, handle);
    }
}

static GabblePresence *
_cache_insert (
    GabblePresenceCache *cache,
    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  GabblePresence *presence;

  presence = gabble_presence_new ();
  g_hash_table_insert (priv->presence, GUINT_TO_POINTER (handle), presence);
  tp_handle_set_add (priv->presence_handles, handle);
  return presence;
}

static gboolean
gabble_presence_cache_do_update (
    GabblePresenceCache *cache,
    TpHandle handle,
    const gchar *resource,
    GabblePresenceId presence_id,
    const gchar *status_message,
    gint8 priority)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *jid;
  GabblePresence *presence;
  GabblePresenceCapabilities caps_before;
  GHashTable *enhanced_caps_before;
  gboolean ret = FALSE;

  jid = tp_handle_inspect (contact_repo, handle);
  DEBUG ("%s (%d) resource %s prio %d presence %d message \"%s\"",
      jid, handle,
      resource == NULL ? "<null>" : resource,
      priority, presence_id,
      status_message == NULL ? "<null>" : status_message);

  presence = gabble_presence_cache_get (cache, handle);

  if (presence == NULL)
    presence = _cache_insert (cache, handle);

  caps_before = presence->caps;
  enhanced_caps_before = presence->per_channel_manager_caps;
  gabble_presence_cache_copy_cache_entry (&enhanced_caps_before,
      presence->per_channel_manager_caps);

  ret = gabble_presence_update (presence, resource, presence_id,
      status_message, priority);

  emit_capabilities_update (cache, handle, caps_before, presence->caps,
      enhanced_caps_before, presence->per_channel_manager_caps);
  gabble_presence_cache_free_cache_entry (enhanced_caps_before);

  return ret;
}

void
gabble_presence_cache_update (
    GabblePresenceCache *cache,
    TpHandle handle,
    const gchar *resource,
    GabblePresenceId presence_id,
    const gchar *status_message,
    gint8 priority)
{
  if (gabble_presence_cache_do_update (cache, handle, resource, presence_id,
      status_message, priority))
    {
      GArray *handles;

      handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);

      g_array_append_val (handles, handle);
      g_signal_emit (cache, signals[PRESENCES_UPDATED], 0, handles);
      g_array_free (handles, TRUE);
    }

  gabble_presence_cache_maybe_remove (cache, handle);
}

void
gabble_presence_cache_update_many (
    GabblePresenceCache *cache,
    const GArray *contact_handles,
    const gchar *resource,
    GabblePresenceId presence_id,
    const gchar *status_message,
    gint8 priority)
{
  GArray *updated;
  guint i;

  updated = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle),
      contact_handles->len);

  for (i = 0 ; i < contact_handles->len ; i++)
    {
      TpHandle handle;

      handle = g_array_index (contact_handles, TpHandle, i);

      if (gabble_presence_cache_do_update (cache, handle, resource,
          presence_id, status_message, priority))
        {
          g_array_append_val (updated, handle);
        }
    }

  if (updated->len > 0)
    g_signal_emit (cache, signals[PRESENCES_UPDATED], 0, updated);

  g_array_free (updated, TRUE);

  for (i = 0 ; i < contact_handles->len ; i++)
    {
      TpHandle handle;

      handle = g_array_index (contact_handles, TpHandle, i);
      gabble_presence_cache_maybe_remove (cache, handle);
    }

}

void gabble_presence_cache_add_bundle_caps (GabblePresenceCache *cache,
    const gchar *node, GabblePresenceCapabilities new_caps)
{
  CapabilityInfo *info;

  info = capability_info_get (cache, node);
  info->trust = CAPABILITY_BUNDLE_ENOUGH_TRUST;
  info->caps |= new_caps;
}

void
gabble_presence_cache_add_own_caps (
    GabblePresenceCache *cache,
    const gchar *ver,
    GabblePresenceCapabilities caps,
    GHashTable *contact_caps)
{
  gchar *uri = g_strdup_printf ("%s#%s", NS_GABBLE_CAPS, ver);
  CapabilityInfo *info = capability_info_get (cache, uri);
  GHashTable *copy = NULL;

  if (info->complete)
    goto out;

  DEBUG ("caching our own caps (%s)", uri);

  /* If this node was already in the cache but not labelled as complete, either
   * the entry's correct, or someone's poisoning us with a SHA-1 collision.
   * Let's update the entry just in case.
   */
  info->caps_set = TRUE;
  info->complete = TRUE;
  info->trust = CAPABILITY_BUNDLE_ENOUGH_TRUST;
  info->caps = caps;
  tp_intset_add (info->guys, cache->priv->conn->parent.self_handle);

  if (contact_caps != NULL)
    gabble_presence_cache_copy_cache_entry (&copy, contact_caps);

  gabble_presence_cache_free_cache_entry (info->per_channel_manager_caps);
  info->per_channel_manager_caps = copy;

  /* FIXME: we should satisfy any waiters for this node now, but I think that
   * can wait till 0.9.
   */

out:
  g_free (uri);
}

/**
 * gabble_presence_cache_peek_own_caps:
 * @cache: a presence cache
 * @ver: a verification string or bundle name
 * @caps: location at which to store caps for @ver
 * @contact_caps: location at which to store contact caps for @ver
 *
 * If the capabilities corresponding to @ver have been added to the cache with
 * gabble_presence_cache_add_own_caps(), sets @caps and @contact_caps and
 * returns %TRUE; otherwise, returns %FALSE.
 *
 * Since the cache only records features Gabble understands (omitting unknown
 * features, identities, and data forms), we can only serve up disco replies
 * from the cache if we know we once advertised exactly this verification
 * string ourselves.
 *
 * Returns: %TRUE if we know exactly what @ver means.
 */
gboolean
gabble_presence_cache_peek_own_caps (
    GabblePresenceCache *cache,
    const gchar *ver,
    GabblePresenceCapabilities *caps,
    GHashTable **contact_caps)
{
  gchar *uri = g_strdup_printf ("%s#%s", NS_GABBLE_CAPS, ver);
  CapabilityInfo *info = capability_info_get (cache, uri);
  gboolean ret = FALSE;

  if (info->complete)
    {
      *caps = info->caps;
      *contact_caps = info->per_channel_manager_caps;
      ret = TRUE;
    }

  g_free (uri);
  return ret;
}

void
gabble_presence_cache_really_remove (
    GabblePresenceCache *cache,
    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *jid;

  jid = tp_handle_inspect (contact_repo, handle);
  DEBUG ("forced to discard cached presence for jid %s", jid);
  g_hash_table_remove (priv->presence, GUINT_TO_POINTER (handle));
  tp_handle_set_remove (priv->presence_handles, handle);
}

void
gabble_presence_cache_contacts_added_to_olpc_view (GabblePresenceCache *self,
                                                   TpHandleSet *handles)
{
  GArray *tmp, *changed;
  guint i;

  tmp = tp_handle_set_to_array (handles);

  changed = g_array_new (FALSE, FALSE, sizeof (TpHandle));

  for (i = 0; i < tmp->len; i++)
    {
      TpHandle handle;
      GabblePresence *presence;

      handle = g_array_index (tmp, TpHandle, i);

      presence = gabble_presence_cache_get (self, handle);
      if (presence == NULL)
        {
          presence = _cache_insert (self, handle);
        }

      if (gabble_presence_added_to_view (presence))
        {
          g_array_append_val (changed, handle);
        }
    }

  if (changed->len > 0)
    {
      g_signal_emit (self, signals[PRESENCES_UPDATED], 0, changed);
    }

  g_array_free (tmp, TRUE);
  g_array_free (changed, TRUE);
}

void
gabble_presence_cache_contacts_removed_from_olpc_view (
    GabblePresenceCache *self,
    TpHandleSet *handles)
{
  GArray *tmp, *changed;
  guint i;

  tmp = tp_handle_set_to_array (handles);

  changed = g_array_new (FALSE, FALSE, sizeof (TpHandle));

  for (i = 0; i < tmp->len; i++)
    {
      TpHandle handle;
      GabblePresence *presence;

      handle = g_array_index (tmp, TpHandle, i);

      presence = gabble_presence_cache_get (self, handle);
      if (presence == NULL)
        {
          presence = _cache_insert (self, handle);
        }

      if (gabble_presence_removed_from_view (presence))
        {
          g_array_append_val (changed, handle);
          gabble_presence_cache_maybe_remove (self, handle);
        }
    }

  if (changed->len > 0)
    {
      g_signal_emit (self, signals[PRESENCES_UPDATED], 0, changed);
    }

  g_array_free (tmp, TRUE);
  g_array_free (changed, TRUE);
}

gboolean
gabble_presence_cache_caps_pending (GabblePresenceCache *cache,
                                    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  GList *uris, *li;

  uris = g_hash_table_get_values (priv->disco_pending);

  for (li = uris; li != NULL; li = li->next)
    {
      GSList *waiters;

      for (waiters = li->data; waiters != NULL; waiters = waiters->next)
        {
          DiscoWaiter *w = waiters->data;
          if (w->handle == handle)
            {
              g_list_free (uris);
              return TRUE;
            }

        }
    }

  return FALSE;
}

gboolean
gabble_presence_cache_is_unsure (GabblePresenceCache *cache)
{
  gulong diff;
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  GTimeVal now;

  g_get_current_time (&now);
  diff = (now.tv_sec - priv->creation_time.tv_sec) * G_USEC_PER_SEC +
      (now.tv_usec - priv->creation_time.tv_usec);

  DEBUG ("Diff: %lu", diff);

  return (diff < UNSURE_PERIOD);
}

void
gabble_presence_cache_update_location (GabblePresenceCache *cache,
                                       TpHandle handle,
                                       GHashTable *new_location)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  g_hash_table_insert (priv->location, GUINT_TO_POINTER (handle), new_location);

  g_signal_emit (cache, signals[LOCATION_UPDATED], 0, handle);
}

/* The return value should be g_hash_table_unref'ed. */
GHashTable *
gabble_presence_cache_get_location (GabblePresenceCache *cache,
                                    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  GHashTable *location = NULL;

  location = g_hash_table_lookup (priv->location, GUINT_TO_POINTER (handle));
  if (location != NULL)
    {
      g_hash_table_ref (location);
      return location;
    }

  return NULL;
}
