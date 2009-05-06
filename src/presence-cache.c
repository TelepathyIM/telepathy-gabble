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
    g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
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
_grab_avatar_sha1 (GabblePresenceCache *cache,
                   TpHandle handle,
                   const gchar *from,
                   LmMessageNode *node)
{
  const gchar *sha1;
  LmMessageNode *x_node, *photo_node;
  GabblePresence *presence;

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL == presence)
    return;

  x_node = lm_message_node_get_child_with_namespace (node, "x",
      NS_VCARD_TEMP_UPDATE);

  if (NULL == x_node)
    {
      /* If (handle == priv->conn->parent.self_handle), then this means
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

  if (tp_strdiff (presence->avatar_sha1, sha1))
    {
      g_free (presence->avatar_sha1);
      presence->avatar_sha1 = g_strdup (sha1);

      /* FIXME: according to XEP-0153,
       * if (handle == priv->conn->parent.self_handle), then we MUST
       * immediately send a presence update with an empty update child
       * element (no photo node), then re-download our own vCard;
       * when that arrives, we may start setting the photo node in our
       * presence again.
       *
       * At the moment we ignore that requirement and trust that our other
       * resource is getting its sha1 right - but it's a good policy to not
       * trust anyone's XMPP implementation :-) */

      g_signal_emit (cache, signals[AVATAR_UPDATE], 0, handle);
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
  g_hash_table_foreach (in, update_caps_helper,
      out);
}

static void _caps_disco_cb (GabbleDisco *disco,
    GabbleDiscoRequest *request,
    const gchar *jid,
    const gchar *node,
    LmMessageNode *query_result,
    GError *error,
    gpointer user_data);

static void
disco_failed (GabblePresenceCache *cache,
    GabbleDisco *disco,
    const gchar *node,
    GSList *waiters,
    TpHandleRepoIface *contact_repo)
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
          const gchar *waiter_jid;

          waiter_jid = tp_handle_inspect (contact_repo, waiter->handle);
          full_jid = g_strdup_printf ("%s/%s", waiter_jid, waiter->resource);

          gabble_disco_request (disco, GABBLE_DISCO_TYPE_INFO, full_jid,
              node, _caps_disco_cb, cache, G_OBJECT(cache), NULL);
          waiter->disco_requested = TRUE;
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
    TpHandle handle)
{
  GSList *i;

  for (i = waiters; NULL != i; i = i->next)
    {
      DiscoWaiter *waiter = i->data;

      if (waiter->handle == handle)
        return waiter;
    }

  return NULL;
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
  gchar *full_jid = NULL;
  GabblePresenceCapabilities caps = 0;
  GHashTable *per_channel_manager_caps;
  guint trust, trust_inc;
  TpHandle handle = 0;
  gboolean bad_hash = FALSE;
  TpBaseConnection *base_conn;
  TpChannelManagerIter iter;
  TpChannelManager *manager;

  cache = GABBLE_PRESENCE_CACHE (user_data);
  priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  base_conn = TP_BASE_CONNECTION (priv->conn);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (NULL == node)
    {
      DEBUG ("got disco response with NULL node, ignoring");
      return;
    }

  waiters = g_hash_table_lookup (priv->disco_pending, node);

  if (NULL != error)
    {
      DEBUG ("disco query failed: %s", error->message);

      disco_failed (cache, disco, node, waiters, contact_repo);

      return;
    }

  handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);

  if (handle == 0)
    {
      DEBUG ("Ignoring presence from invalid JID %s", jid);
      return;
    }

  waiter_self = find_matching_waiter (waiters, handle);

  if (NULL == waiter_self)
    {
      DEBUG ("Ignoring non requested disco reply");
      goto OUT;
    }

  per_channel_manager_caps = g_hash_table_new (NULL, NULL);

  /* parsing for Connection.Interface.ContactCapabilities.DRAFT */
  tp_base_connection_channel_manager_iter_init (&iter, base_conn);
  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      gpointer *factory_caps;

      /* all channel managers must implement the capability interface */
      g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

      factory_caps = gabble_caps_channel_manager_parse_capabilities
          (GABBLE_CAPS_CHANNEL_MANAGER (manager), query_result->children);
      if (factory_caps != NULL)
        g_hash_table_insert (per_channel_manager_caps,
            GABBLE_CAPS_CHANNEL_MANAGER (manager), factory_caps);
    }

  /* parsing for Connection.Interface.Capabilities*/
  caps = capabilities_parse (query_result);

  /* Only 'sha-1' is mandatory to implement by XEP-0115. If the remote contact
   * uses another hash algorithm, don't check the hash and fallback to the old
   * method. The hash method is not included in the discovery request nor
   * response but we saved it in disco_pending when we received the presence
   * stanza. */
  if (!tp_strdiff (waiter_self->hash, "sha-1"))
    {
      gchar *computed_hash;
      trust_inc = CAPABILITY_BUNDLE_ENOUGH_TRUST;

      computed_hash = caps_hash_compute_from_lm_node (query_result);

      if (g_str_equal (waiter_self->ver, computed_hash))
        {
          trust = capability_info_recvd (cache, node, handle, caps,
              per_channel_manager_caps, trust_inc);
        }
      else
        {
          /* The received reply does not match the */
          DEBUG ("The announced verification string '%s' does not match "
              "our hash '%s'.", waiter_self->ver, computed_hash);
          trust = 0;
          bad_hash = TRUE;
          gabble_presence_cache_free_cache_entry (per_channel_manager_caps);
          per_channel_manager_caps = NULL;
        }

      g_free (computed_hash);
    }
  else
    {
      trust_inc = 1;
      trust = capability_info_recvd (cache, node, handle, caps, NULL,
          trust_inc);

      /* Do not allow tubes caps if the contact does not observe XEP-0115
       * version 1.5: we don't need to bother being compatible with both version
       * 1.3 and tubes caps */
      gabble_presence_cache_free_cache_entry (per_channel_manager_caps);
      per_channel_manager_caps = NULL;
    }

  for (i = waiters; NULL != i;)
    {
      DiscoWaiter *waiter;
      GabblePresence *presence;

      waiter = (DiscoWaiter *) i->data;

      if (trust >= CAPABILITY_BUNDLE_ENOUGH_TRUST || waiter->handle == handle)
        {
          GSList *tmp;
          gpointer key;
          gpointer value;

          if (!bad_hash)
            {
              /* trusted reply */
              presence = gabble_presence_cache_get (cache, waiter->handle);

              if (presence)
              {
                GabblePresenceCapabilities save_caps = presence->caps;
                GHashTable *save_enhanced_caps;
                gabble_presence_cache_copy_cache_entry (&save_enhanced_caps,
                    presence->per_channel_manager_caps);

                DEBUG ("setting caps for %d (thanks to %d %s) to "
                    "%d (save_caps %d)",
                    waiter->handle, handle, jid, caps, save_caps);
                gabble_presence_set_capabilities (presence, waiter->resource,
                    caps, per_channel_manager_caps, waiter->serial);
                DEBUG ("caps for %d (thanks to %d %s) now %d", waiter->handle,
                    handle, jid, presence->caps);
                g_signal_emit (cache, signals[CAPABILITIES_UPDATE], 0,
                  waiter->handle, save_caps, presence->caps,
                  save_enhanced_caps, presence->per_channel_manager_caps);
                gabble_presence_cache_free_cache_entry (save_enhanced_caps);
              }
            }

          tmp = i;
          i = i->next;

          waiters = g_slist_delete_link (waiters, tmp);

          if (!g_hash_table_lookup_extended (priv->disco_pending, node, &key,
                &value))
            g_assert_not_reached ();

          g_hash_table_steal (priv->disco_pending, node);
          g_hash_table_insert (priv->disco_pending, key, waiters);

          g_signal_emit (cache, signals[CAPABILITIES_DISCOVERED], 0, waiter->handle);
          disco_waiter_free (waiter);
        }
      else if (trust + disco_waiter_list_get_request_count (waiters) - trust_inc
          < CAPABILITY_BUNDLE_ENOUGH_TRUST)
        {
          /* if the possible trust, not counting this guy, is too low,
           * we have been poisoned and reset our trust meters - disco
           * anybody we still haven't to be able to get more trusted replies */

          if (!waiter->disco_requested)
            {
              const gchar *waiter_jid;

              waiter_jid = tp_handle_inspect (contact_repo, waiter->handle);
              full_jid = g_strdup_printf ("%s/%s", waiter_jid,
                  waiter->resource);

              gabble_disco_request (disco, GABBLE_DISCO_TYPE_INFO, full_jid,
                  node, _caps_disco_cb, cache, G_OBJECT(cache), NULL);
              waiter->disco_requested = TRUE;

              g_free (full_jid);
              full_jid = NULL;
            }

          i = i->next;
        }
      else
        {
          /* trust level still uncertain, don't do nothing */
          i = i->next;
        }
    }

  if (trust >= CAPABILITY_BUNDLE_ENOUGH_TRUST)
    g_hash_table_remove (priv->disco_pending, node);

OUT:

  if (handle)
    tp_handle_unref (contact_repo, handle);
  g_free (full_jid);
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
      /* Append the (handle, resource) pair to the list of such pairs
       * waiting for capabilities for this uri, and send a disco request
       * if we don't have enough possible trust yet */

      GSList *waiters;
      DiscoWaiter *waiter;
      guint possible_trust;
      gpointer key;
      gpointer value = NULL;

      DEBUG ("not enough trust for URI %s", uri);

      /* If the URI is in the hash table, steal it and its value; we can
       * reuse the same URI for the following insertion. Otherwise, make a
       * copy of the URI for use as a key.
       */

      if (g_hash_table_lookup_extended (priv->disco_pending, uri, &key,
            &value))
        {
          g_hash_table_steal (priv->disco_pending, key);
        }
      else
        {
          key = g_strdup (uri);
        }

      waiters = (GSList *) value;
      waiter = disco_waiter_new (contact_repo, handle, resource,
          hash, ver, serial);
      waiters = g_slist_prepend (waiters, waiter);
      g_hash_table_insert (priv->disco_pending, key, waiters);

      possible_trust = disco_waiter_list_get_request_count (waiters);

      if (!value
          || info->trust + possible_trust < CAPABILITY_BUNDLE_ENOUGH_TRUST)
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

      g_signal_emit (cache, signals[CAPABILITIES_UPDATE], 0,
          handle, old_caps, presence->caps, old_enhanced_caps,
          presence->per_channel_manager_caps);
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
  LmMessageNode *presence_node, *child_node;
  LmHandlerResult ret = LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
  GabblePresenceId presence_id;
  GabblePresence *presence;

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
      jid, handle, resource, priority, presence_id, status_message);

  presence = gabble_presence_cache_get (cache, handle);

  if (presence == NULL)
    presence = _cache_insert (cache, handle);

  caps_before = presence->caps;
  enhanced_caps_before = presence->per_channel_manager_caps;
  gabble_presence_cache_copy_cache_entry (&enhanced_caps_before,
      presence->per_channel_manager_caps);

  ret = gabble_presence_update (presence, resource, presence_id,
      status_message, priority);

  g_signal_emit (cache, signals[CAPABILITIES_UPDATE], 0, handle,
      caps_before, presence->caps, enhanced_caps_before,
      presence->per_channel_manager_caps);
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
