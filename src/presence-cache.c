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
#include "caps-cache.h"
#include "caps-channel-manager.h"
#include "caps-hash.h"
#include "conn-presence.h"
#include "debug.h"
#include "disco.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"
#include "roster.h"
#include "types.h"

/* Time period from the cache creation in which we're unsure whether we
 * got initial presence from all the contacts. */
#define UNSURE_PERIOD 5

/* Time period from a de-cloak request in which we're unsure whether the
 * contact will disclose their presence later, or not at all. */
#define DECLOAK_PERIOD 5

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
  UNSURE_PERIOD_ENDED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _GabblePresenceCachePrivate
{
  GabbleConnection *conn;

  gulong status_changed_cb;
  LmMessageHandler *lm_message_cb;
  LmMessageHandler *lm_presence_cb;

  GHashTable *presence;
  TpHandleSet *presence_handles;

  GHashTable *capabilities;
  GHashTable *disco_pending;
  guint caps_serial;

  guint unsure_id;
  /* handle => DecloakContext */
  GHashTable *decloak_requests;
  TpHandleSet *decloak_handles;

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

static GabbleCapabilityInfo *
capability_info_get (GabblePresenceCache *cache, const gchar *node)
{
  GabblePresenceCachePrivate *priv = cache->priv;
  GabbleCapabilityInfo *info = g_hash_table_lookup (priv->capabilities, node);

  if (NULL == info)
    {
      info = g_slice_new0 (GabbleCapabilityInfo);
      info->cap_set = NULL;
      info->guys = tp_intset_new ();
      g_hash_table_insert (priv->capabilities, g_strdup (node), info);
    }

  return info;
}

static void
capability_info_free (GabbleCapabilityInfo *info)
{
  if (info->cap_set != NULL)
    {
      gabble_capability_set_free (info->cap_set);
      info->cap_set = NULL;
    }

  gabble_disco_identity_array_free (info->identities);
  info->identities = NULL;

  tp_intset_destroy (info->guys);

  g_slice_free (GabbleCapabilityInfo, info);
}

static guint
capability_info_recvd (GabblePresenceCache *cache,
    const gchar *node,
    TpHandle handle,
    GabbleCapabilitySet *cap_set,
    guint trust_inc)
{
  GabbleCapabilityInfo *info = capability_info_get (cache, node);

  if (info->cap_set == NULL ||
      !gabble_capability_set_equals (cap_set, info->cap_set))
    {
      /* The caps are not valid, either because we detected inconsistency
       * between several contacts using the same node (when the hash is not
       * used), or because this is the first caps report and the caps were
       * never set.
       */
      tp_intset_clear (info->guys);

      if (info->cap_set == NULL)
        info->cap_set = gabble_capability_set_new ();
      else
        gabble_capability_set_clear (info->cap_set);

      gabble_capability_set_update (info->cap_set, cap_set);
      info->trust = 0;
    }

  if (!tp_intset_is_member (info->guys, handle))
    {
      tp_intset_add (info->guys, handle);
      info->trust += trust_inc;
    }

  return info->trust;
}

typedef struct {
    GabblePresenceCache *cache;
    TpHandle handle;
    guint timeout_id;
    const gchar *reason;
} DecloakContext;

static DecloakContext *
decloak_context_new (GabblePresenceCache *cache,
    TpHandle handle,
    const gchar *reason)
{
  DecloakContext *dc = g_slice_new0 (DecloakContext);

  dc->cache = cache;
  dc->handle = handle;
  dc->reason = reason;
  dc->timeout_id = 0;
  return dc;
}

static void
decloak_context_free (gpointer data)
{
  DecloakContext *dc = data;

  tp_handle_set_remove (dc->cache->priv->decloak_handles, dc->handle);

  if (dc->timeout_id != 0)
    g_source_remove (dc->timeout_id);

  g_slice_free (DecloakContext, dc);
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
    gabble_marshal_VOID__UINT_POINTER_POINTER, G_TYPE_NONE,
    3, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER);
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

  signals[UNSURE_PERIOD_ENDED] = g_signal_new (
    "unsure-period-ended",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
    0);
}

static gboolean
gabble_presence_cache_end_unsure_period (gpointer data)
{
  GabblePresenceCache *self = GABBLE_PRESENCE_CACHE (data);

  DEBUG ("%p", data);
  self->priv->unsure_id = 0;
  g_signal_emit (self, signals[UNSURE_PERIOD_ENDED], 0);
  return FALSE;
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

  priv->decloak_requests = g_hash_table_new_full (NULL, NULL, NULL,
      decloak_context_free);

  priv->location = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_hash_table_destroy);
}

static void gabble_presence_cache_add_bundle_caps (GabblePresenceCache *cache,
    const gchar *node, const gchar *ns);

static void
gabble_presence_cache_add_bundles (GabblePresenceCache *cache)
{
#define GOOGLE_BUNDLE(cap, features) \
  gabble_presence_cache_add_bundle_caps (cache, \
      "http://www.google.com/xmpp/client/caps#" cap, features); \
  gabble_presence_cache_add_bundle_caps (cache, \
      "http://talk.google.com/xmpp/client/caps#" cap, features);

  /* Cache various bundle from the Google Talk clients as trusted.  Some old
   * versions of Google Talk do not reply correctly to discovery requests.
   * Plus, we know what Google's bundles mean, so it's a waste of time to disco
   * them, particularly the ones for features we don't support. The desktop
   * client doesn't currently have all of these, but it doesn't hurt to cache
   * them anyway.
   */
  GOOGLE_BUNDLE ("voice-v1", NS_GOOGLE_FEAT_VOICE);
  GOOGLE_BUNDLE ("video-v1", NS_GOOGLE_FEAT_VIDEO);

  /* File transfer support */
  GOOGLE_BUNDLE ("share-v1", NS_GOOGLE_FEAT_SHARE);

  /* Not really sure what this ones is. */
  GOOGLE_BUNDLE ("sms-v1", NULL);

  /* TODO: remove this when we fix fd.o#22768. */
  GOOGLE_BUNDLE ("pmuc-v1", NULL);

  /* The camera-v1 bundle seems to mean "I have a camera plugged in". Not
   * having it doesn't seem to affect anything, and we have no way of exposing
   * that information anyway.
   */
  GOOGLE_BUNDLE ("camera-v1", NULL);

#undef GOOGLE_BUNDLE

  /* We should also cache the ext='' bundles Gabble advertises: older Gabbles
   * advertise these and don't support hashed caps, and we shouldn't need to
   * disco them.
   */
  gabble_presence_cache_add_bundle_caps (cache,
      NS_GABBLE_CAPS "#" BUNDLE_VOICE_V1, NS_GOOGLE_FEAT_VOICE);
  gabble_presence_cache_add_bundle_caps (cache,
      NS_GABBLE_CAPS "#" BUNDLE_VIDEO_V1, NS_GOOGLE_FEAT_VIDEO);
  gabble_presence_cache_add_bundle_caps (cache,
      NS_GABBLE_CAPS "#" BUNDLE_SHARE_V1, NS_GOOGLE_FEAT_SHARE);
}

static GObject *
gabble_presence_cache_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabblePresenceCachePrivate *priv;

  obj = G_OBJECT_CLASS (gabble_presence_cache_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_PRESENCE_CACHE (obj)->priv;

  g_assert (priv->conn != NULL);
  g_assert (priv->presence_handles != NULL);
  g_assert (priv->decloak_handles != NULL);

  gabble_presence_cache_add_bundles ((GabblePresenceCache *) obj);

  priv->status_changed_cb = g_signal_connect (priv->conn, "status-changed",
      G_CALLBACK (gabble_presence_cache_status_changed_cb), obj);

  return obj;
}

static void
gabble_presence_cache_dispose (GObject *object)
{
  GabblePresenceCache *self = GABBLE_PRESENCE_CACHE (object);
  GabblePresenceCachePrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");

  priv->dispose_has_run = TRUE;

  if (priv->unsure_id != 0)
    {
      g_source_remove (priv->unsure_id);
      priv->unsure_id = 0;
    }

  tp_clear_pointer (&priv->decloak_requests, g_hash_table_destroy);
  tp_clear_pointer (&priv->decloak_handles, tp_handle_set_destroy);

  g_assert (priv->lm_message_cb == NULL);
  g_assert (priv->lm_presence_cb == NULL);

  g_signal_handler_disconnect (priv->conn, priv->status_changed_cb);

  tp_clear_pointer (&priv->presence, g_hash_table_destroy);
  tp_clear_pointer (&priv->capabilities, g_hash_table_destroy);
  tp_clear_pointer (&priv->disco_pending, g_hash_table_destroy);
  tp_clear_pointer (&priv->presence_handles, tp_handle_set_destroy);
  tp_clear_pointer (&priv->location, g_hash_table_destroy);

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
  GabblePresenceCachePrivate *priv = cache->priv;

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
  GabblePresenceCachePrivate *priv = cache->priv;
  TpHandleRepoIface *contact_repo;

  switch (property_id) {
    case PROP_CONNECTION:
      g_assert (priv->conn == NULL);              /* construct-only */
      g_assert (priv->presence_handles == NULL);  /* construct-only */
      g_assert (priv->decloak_handles == NULL);   /* construct-only */

      priv->conn = g_value_get_object (value);
      contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
      priv->presence_handles = tp_handle_set_new (contact_repo);
      priv->decloak_handles = tp_handle_set_new (contact_repo);
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
  GabblePresenceCachePrivate *priv = cache->priv;

  g_assert (conn == priv->conn);

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_assert (priv->lm_message_cb == NULL);
      g_assert (priv->lm_presence_cb == NULL);

      /* these are separate despite having the same callback and user_data,
       * because the Wocky fake-Loudmouth compat layer only lets you register
       * each handler once */
      priv->lm_message_cb = lm_message_handler_new (
          gabble_presence_cache_lm_message_cb, cache, NULL);
      priv->lm_presence_cb = lm_message_handler_new (
          gabble_presence_cache_lm_message_cb, cache, NULL);

      lm_connection_register_message_handler (priv->conn->lmconn,
                                              priv->lm_presence_cb,
                                              LM_MESSAGE_TYPE_PRESENCE,
                                              LM_HANDLER_PRIORITY_LAST);
      lm_connection_register_message_handler (priv->conn->lmconn,
                                              priv->lm_message_cb,
                                              LM_MESSAGE_TYPE_MESSAGE,
                                              LM_HANDLER_PRIORITY_FIRST);
      break;

    case TP_CONNECTION_STATUS_CONNECTED:
      /* After waiting UNSURE_PERIOD seconds for initial presences to trickle
       * in, the "unsure period" ends. */
      priv->unsure_id = g_timeout_add_seconds (UNSURE_PERIOD,
          gabble_presence_cache_end_unsure_period, cache);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      if (priv->lm_message_cb != NULL)
        lm_connection_unregister_message_handler (conn->lmconn,
            priv->lm_message_cb, LM_MESSAGE_TYPE_MESSAGE);

      if (priv->lm_presence_cb != NULL)
        lm_connection_unregister_message_handler (conn->lmconn,
            priv->lm_presence_cb, LM_MESSAGE_TYPE_PRESENCE);

      tp_clear_pointer (&priv->lm_message_cb, lm_message_handler_unref);
      tp_clear_pointer (&priv->lm_presence_cb, lm_message_handler_unref);
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
  GabblePresenceCachePrivate *priv = cache->priv;
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
  GabblePresenceCachePrivate *priv = cache->priv;
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
  GabblePresence *presence = priv->conn->self_presence;
  GError *error = NULL;

  if (base_conn->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      DEBUG ("no longer connected");
      return;
    }

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
  if (!conn_presence_signal_own_presence (priv->conn, NULL, &error))
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
  GabblePresenceCachePrivate *priv = cache->priv;
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
      else if (base_conn->status == TP_CONNECTION_STATUS_CONNECTED)
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
_parse_node (GabblePresence *presence,
    LmMessageNode *lm_node,
    const gchar *resource,
    guint serial)
{
  LmMessageNode *cap_node;
  const gchar *node;

  cap_node = lm_message_node_get_child_with_namespace (lm_node, "c", NS_CAPS);

  if (NULL == cap_node)
    return;

  node = lm_message_node_get_attribute (cap_node, "node");

  if (!tp_strdiff (node, "http://mail.google.com/xmpp/client/caps"))
    {
      GabbleCapabilitySet *cap_set = gabble_capability_set_new ();

      DEBUG ("Client is Google Web Client");

      gabble_capability_set_add (cap_set, QUIRK_GOOGLE_WEBMAIL_CLIENT);
      gabble_presence_set_capabilities (presence, resource, cap_set, serial);
      gabble_capability_set_free (cap_set);
    }
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
  GabblePresenceCachePrivate *priv = cache->priv;
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

static void
emit_capabilities_update (GabblePresenceCache *cache,
    TpHandle handle,
    const GabbleCapabilitySet *old_cap_set,
    const GabbleCapabilitySet *new_cap_set)
{
  if (gabble_capability_set_equals (old_cap_set, new_cap_set))
    {
      DEBUG ("no change in caps for handle %u", handle);
    }
  else
    {
      if (DEBUGGING)
        {
          gchar *diff = gabble_capability_set_dump_diff (old_cap_set,
              new_cap_set, "  ");

          DEBUG ("Emitting caps update for handle %u\n%s", handle, diff);
          g_free (diff);
        }

      g_signal_emit (cache, signals[CAPABILITIES_UPDATE], 0,
          handle, old_cap_set, new_cap_set);
    }
}

/**
 * set_caps_for:
 *
 * Sets caps for @waiter to (@caps, @cap_set), having received
 * a trusted reply from @responder_{handle,jid}.
 */
static void
set_caps_for (DiscoWaiter *waiter,
    GabblePresenceCache *cache,
    GabbleCapabilitySet *cap_set,
    TpHandle responder_handle,
    const gchar *responder_jid)
{
  GabblePresence *presence = gabble_presence_cache_get (cache, waiter->handle);
  GabbleCapabilitySet *old_cap_set;
  const GabbleCapabilitySet *new_cap_set;

  if (presence == NULL)
    return;

  old_cap_set = gabble_presence_dup_caps (presence);

  DEBUG ("setting caps for %d (thanks to %d %s)",
      waiter->handle, responder_handle, responder_jid);

  gabble_presence_set_capabilities (presence, waiter->resource, cap_set,
      waiter->serial);

  new_cap_set = gabble_presence_peek_caps (presence);

  emit_capabilities_update (cache, waiter->handle, old_cap_set, new_cap_set);

  gabble_capability_set_free (old_cap_set);
}

static void
emit_capabilities_discovered (GabblePresenceCache *cache,
    TpHandle handle)
{
  g_signal_emit (cache, signals[CAPABILITIES_DISCOVERED], 0, handle);
}

static GPtrArray *
client_types_from_message (TpHandle handle,
    LmMessageNode *lm_node,
    const gchar *resource)
{
  WockyNode *identity, *query_result = (WockyNode *) lm_node;
  WockyNodeIter iter;
  GPtrArray *array;

  array = g_ptr_array_new_with_free_func (g_free);

  /* Find all identity nodes in the result. */
  wocky_node_iter_init (&iter, query_result,
      "identity", NS_DISCO_INFO);
  while (wocky_node_iter_next (&iter, &identity))
    {
      const gchar *category, *type;

      category = wocky_node_get_attribute (identity, "category");
      if (category == NULL)
        continue;

      /* Now get the client type */
      type = wocky_node_get_attribute (identity, "type");
      if (type == NULL)
        continue;

      /* So, turns out if you disco a specific resource of a gtalk
      contact, the Google servers will reply with the identity node as
      if you disco'd the bare jid, so will get something like:

          <identity category='account' type='registered' name='Google Talk User Account'/>

      which is just great. So, let's special case android phones as
      their resources will start with "android" and let's just say
      they're phones. */

      if (!tp_strdiff (category, "account")
          && g_str_has_prefix (resource, "android")
          && !tp_strdiff (type, "registered"))
        {
          type = "phone";
        }

      DEBUG ("Got type for %u: %s", handle, type);

      g_ptr_array_add (array, g_strdup (type));
    }

  if (array->len == 0)
    {
      DEBUG ("How very odd, we didn't get any client types");
      g_ptr_array_unref (array);
      return NULL;
    }

  return array;
}

static void
_signal_presences_updated (GabblePresenceCache *cache,
    TpHandle handle)
{
  GArray *handles;

  handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
  g_array_append_val (handles, handle);
  g_signal_emit (cache, signals[PRESENCES_UPDATED], 0, handles);
  g_array_free (handles, TRUE);
}

static void
process_client_types (
    GabblePresenceCache *cache,
    LmMessageNode *query_result,
    TpHandle handle,
    DiscoWaiter *waiter_self)
{
  GabblePresence *presence = gabble_presence_cache_get (cache, handle);
  GPtrArray *client_types;

  /* If the contact's gone offline since we sent the disco request, we have no
   * presence to attach their freshly-discovered client types to.
   */
  if (presence == NULL)
    return;

  client_types = client_types_from_message (handle, query_result,
      waiter_self->resource);

  if (waiter_self->resource != NULL)
    gabble_presence_update_client_types (presence, waiter_self->resource,
        client_types);

  if (client_types != NULL)
    {
      g_ptr_array_unref (client_types);
      _signal_presences_updated (cache, handle);
    }
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
  GabbleCapabilitySet *cap_set;
  guint trust;
  TpHandle handle = 0;
  gboolean bad_hash = FALSE;
  TpBaseConnection *base_conn;
  gchar *resource;
  gboolean jid_is_valid;
  gpointer key;

  cache = GABBLE_PRESENCE_CACHE (user_data);
  priv = cache->priv;
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

  process_client_types (cache, query_result, handle, waiter_self);

  /* Now onto caps */
  cap_set = gabble_capability_set_new_from_stanza (query_result);

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
          trust = capability_info_recvd (cache, node, handle, cap_set,
              CAPABILITY_BUNDLE_ENOUGH_TRUST);
        }
      else
        {
          DEBUG ("The verification string '%s' announced by '%s' does not "
              "match our hash of their disco reply '%s'.", waiter_self->ver,
              jid, computed_hash);
          trust = 0;
          bad_hash = TRUE;
        }

      g_free (computed_hash);
    }
  else
    {
      trust = capability_info_recvd (cache, node, handle, cap_set, 1);
    }

  /* Remove the node from the hash table without freeing the key or list of
   * waiters.
   *
   * In the 'enough trust' case, this needs to be done before emitting the
   * signal, so that when recipients of the capabilities-discovered signal ask
   * whether we're unsure about the handle, there is no pending disco request
   * that would make us unsure.
   *
   * In the 'not enough trust' branch, we re-use 'key' when updating the table.
   */
  if (!g_hash_table_lookup_extended (priv->disco_pending, node, &key, NULL))
    g_assert_not_reached ();
  g_hash_table_steal (priv->disco_pending, node);

  if (trust >= CAPABILITY_BUNDLE_ENOUGH_TRUST)
    {
      WockyNodeTree *query_node = wocky_node_tree_new_from_node (query_result);
      GabbleCapsCache *caps_cache = gabble_caps_cache_dup_shared ();

      if (DEBUGGING)
        {
          gchar *tmp = gabble_capability_set_dump (cap_set, "  ");

          DEBUG ("trusting %s to mean:\n%s", node, tmp);
          g_free (tmp);
        }

      /* Update external cache. */
      gabble_caps_cache_insert (caps_cache, node, query_node);
      g_object_unref (caps_cache);
      g_object_unref (query_node);

      /* We trust this caps node. Serve all its waiters. */
      for (i = waiters; NULL != i; i = i->next)
        {
          DiscoWaiter *waiter = (DiscoWaiter *) i->data;

          set_caps_for (waiter, cache, cap_set, handle, jid);
          emit_capabilities_discovered (cache, waiter->handle);
        }

      g_free (key);
      disco_waiter_list_free (waiters);
    }
  else
    {
      /* We don't trust this yet (either the hash was bad, or we haven't had
       * enough responses, as appropriate).
       */

      /* Set caps for the contact that replied (if the hash was correct) and
       * remove them from the list of waiters.
       * FIXME I think we should respect the caps, even if the hash is wrong,
       *       for the jid that answered the query.
       */
      if (!bad_hash)
        {
          if (DEBUGGING)
            {
              gchar *tmp = gabble_capability_set_dump (cap_set, "  ");

              DEBUG ("%s not yet fully trusted to mean:\n%s", node, tmp);
              g_free (tmp);
            }

          set_caps_for (waiter_self, cache, cap_set, handle, jid);
        }

      waiters = g_slist_remove (waiters, waiter_self);
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

  gabble_capability_set_free (cap_set);

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
  GabbleCapabilityInfo *info;
  WockyNodeTree *cached_query_reply;
  GabbleCapabilitySet *cached_caps = NULL;
  GabblePresenceCachePrivate *priv;
  TpHandleRepoIface *contact_repo;
  GabbleCapsCache *caps_cache;

  priv = cache->priv;
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  info = capability_info_get (cache, uri);

  caps_cache = gabble_caps_cache_dup_shared ();
  cached_query_reply = gabble_caps_cache_lookup (caps_cache, uri);

  if (cached_query_reply != NULL)
    {
      WockyNode *query = wocky_node_tree_get_top_node (cached_query_reply);

      cached_caps = gabble_capability_set_new_from_stanza (query);

      if (cached_caps == NULL)
        {
          gchar *query_str = wocky_node_to_string (query);

          g_warning ("couldn't re-parse cached query node, which was: %s",
              query_str);
          g_free (query_str);
        }
    }

  g_object_unref (caps_cache);

  if (cached_caps != NULL ||
      info->trust >= CAPABILITY_BUNDLE_ENOUGH_TRUST ||
      tp_intset_is_member (info->guys, handle))
    {
      GabblePresence *presence = gabble_presence_cache_get (cache, handle);
      GabbleCapabilitySet *cap_set = cached_caps ? cached_caps : info->cap_set;

      /* we already have enough trust for this node; apply the cached value to
       * the (handle, resource) */
      DEBUG ("enough trust for URI %s, setting caps for %u (%s)", uri, handle,
          from);

      if (presence)
        {
          gabble_presence_set_capabilities (
              presence, resource, cap_set, serial);

          /* We can only get this information from actual disco replies,
           * so we depend on having this information from the caps cache. */
          if (cached_query_reply != NULL)
            {
              WockyNode *query = wocky_node_tree_get_top_node (cached_query_reply);
              GPtrArray *types = client_types_from_message (handle, query, resource);

              if (resource != NULL)
                gabble_presence_update_client_types (presence, resource, types);

              if (types != NULL)
                {
                  g_ptr_array_unref (types);

                  _signal_presences_updated (cache, handle);
                }
            }
        }
      else
        DEBUG ("presence not found");

      if (cached_caps != NULL)
        gabble_capability_set_free (cached_caps);
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
          goto out;
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

out:
  if (cached_query_reply != NULL)
    g_object_unref (cached_query_reply);
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
  GabbleCapabilitySet *old_cap_set = NULL;
  guint serial;
  const gchar *hash, *ver;

  priv = cache->priv;
  serial = priv->caps_serial++;

  resource = strchr (from, '/');
  if (resource != NULL)
    resource++;

  uris = _parse_cap_bundles (lm_node, &hash, &ver);

  if (presence)
    {
      old_cap_set = gabble_presence_dup_caps (presence);

      _parse_node (presence, lm_node, resource, serial);
    }

  /* XEP-0115 §8.4 allows a server to strip out <c/> from presences it relays
   * to a client if it knows that the <c/> hasn't changed since the last time
   * it relayed one for this resource to the client. Thus, the client MUST NOT
   * expect to get <c/> on every <presence/>, and shouldn't erase previous caps
   * in that case.
   *
   * If the <presence/> stanza didn't contain a <c/> node at all, then there
   * will be no iterations of this loop, and hence no calls to
   * gabble_presence_set_capabilities(), and hence the caps will be preserved.
   * Not pretty, but it seems to work.
   */
  for (i = uris; NULL != i; i = i->next)
    {
      _process_caps_uri (cache, from, (gchar *) i->data, hash, ver, handle,
          resource, serial);
      g_free (i->data);

    }

  if (presence)
    {
      const GabbleCapabilitySet *new_cap_set =
          gabble_presence_peek_caps (presence);

      emit_capabilities_update (cache, handle, old_cap_set, new_cap_set);
    }
  else
    {
      DEBUG ("No presence for handle %u, not updating caps", handle);
    }

  if (old_cap_set != NULL)
    gabble_capability_set_free (old_cap_set);

  g_slist_free (uris);
}

LmHandlerResult
gabble_presence_parse_presence_message (GabblePresenceCache *cache,
                         TpHandle handle,
                         const gchar *from,
                         LmMessage *message)
{
  GabblePresenceCachePrivate *priv = cache->priv;
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

  presence_node = wocky_stanza_get_top_node (message);
  g_assert (0 == strcmp (presence_node->name, "presence"));

  resource = strchr (from, '/');
  if (resource != NULL)
    resource++;

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL != presence)
      /* Once we've received presence from somebody, we don't need to keep the
       * presence around when it's unavailable. */
      presence->keep_unavailable = FALSE;

  /* If we receive (directed or broadcast) presence of any sort from someone,
   * it counts as a reply to any pending de-cloak request we might have been
   * tracking */
  g_hash_table_remove (priv->decloak_requests, GUINT_TO_POINTER (handle));

  child_node = lm_message_node_get_child (presence_node, "status");

  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  child_node = lm_message_node_get_child (presence_node, "priority");

  if (child_node)
    {
      const gchar *prio = lm_message_node_get_value (child_node);

      if (prio != NULL)
        priority = CLAMP (atoi (prio), G_MININT8, G_MAXINT8);
    }

  child_node = wocky_node_get_child_ns (presence_node, "temppres",
      NS_TEMPPRES);

  if (child_node != NULL)
    {
      gboolean decloak;
      const gchar *reason;

      /* this is a request to de-cloak, i.e. leak a minimal version of our
       * presence to the peer */
      g_object_get (priv->conn,
          "decloak-automatically", &decloak,
          NULL);

      reason = lm_message_node_get_attribute (child_node, "reason");

      if (reason == NULL)
        reason = "";

      DEBUG ("Considering whether to decloak, reason='%s', conclusion=%d",
          reason, decloak);

      conn_decloak_emit_requested (priv->conn, handle, reason, decloak);

      if (decloak)
        gabble_connection_send_capabilities (priv->conn, from, NULL);
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
      if (gabble_roster_handle_sends_presence_to_us (priv->conn->roster,
            handle))
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
  GabblePresenceCachePrivate *priv = cache->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const char *from;
  LmHandlerResult ret;
  TpHandle handle;

  g_assert (lmconn == priv->conn->lmconn);

  from = lm_message_node_get_attribute (wocky_stanza_get_top_node (message),
      "from");

  if (NULL == from)
    {
      STANZA_DEBUG (message, "message without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = tp_handle_ensure (contact_repo, from, NULL, NULL);

  if (0 == handle)
    {
      STANZA_DEBUG (message, "ignoring message from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  switch (lm_message_get_type (message))
    {
    case LM_MESSAGE_TYPE_PRESENCE:
      ret = gabble_presence_parse_presence_message (cache, handle,
        from, message);
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
  GabblePresenceCachePrivate *priv = cache->priv;
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
  GabblePresenceCachePrivate *priv = cache->priv;
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
  GabblePresenceCachePrivate *priv = cache->priv;
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
  GabblePresenceCachePrivate *priv = cache->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *jid;
  GabblePresence *presence;
  GabbleCapabilitySet *old_cap_set;
  const GabbleCapabilitySet *new_cap_set;
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

  old_cap_set = gabble_presence_dup_caps (presence);

  ret = gabble_presence_update (presence, resource, presence_id,
      status_message, priority);

  new_cap_set = gabble_presence_peek_caps (presence);

  emit_capabilities_update (cache, handle, old_cap_set, new_cap_set);

  gabble_capability_set_free (old_cap_set);

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
      _signal_presences_updated (cache, handle);
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

static void
gabble_presence_cache_add_bundle_caps (GabblePresenceCache *cache,
    const gchar *node,
    const gchar *namespace)
{
  GabbleCapabilityInfo *info;

  info = capability_info_get (cache, node);

  /* The caps are immediately valid, because we already know this bundle */
  if (info->cap_set == NULL)
    info->cap_set = gabble_capability_set_new ();

  info->trust = CAPABILITY_BUNDLE_ENOUGH_TRUST;

  if (namespace != NULL)
    gabble_capability_set_add (info->cap_set, namespace);
}

void
gabble_presence_cache_add_own_caps (
    GabblePresenceCache *cache,
    const gchar *ver,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities)
{
  gchar *uri = g_strdup_printf ("%s#%s", NS_GABBLE_CAPS, ver);
  GabbleCapabilityInfo *info = capability_info_get (cache, uri);

  if (info->complete)
    goto out;

  DEBUG ("caching our own caps (%s)", uri);

  /* If this node was already in the cache but not labelled as complete, either
   * the entry's correct, or someone's poisoning us with a SHA-1 collision.
   * Let's update the entry just in case.
   */
  if (info->cap_set == NULL)
    {
      info->cap_set = gabble_capability_set_copy (cap_set);
    }
  else
    {
      gabble_capability_set_clear (info->cap_set);
      gabble_capability_set_update (info->cap_set, cap_set);
    }

  gabble_disco_identity_array_free (info->identities);
  info->identities = gabble_disco_identity_array_copy (identities);

  info->complete = TRUE;
  info->trust = CAPABILITY_BUNDLE_ENOUGH_TRUST;
  tp_intset_add (info->guys, cache->priv->conn->parent.self_handle);

  /* FIXME: we should satisfy any waiters for this node now. fd.o bug #24619. */

out:
  g_free (uri);
}

/**
 * gabble_presence_cache_peek_own_caps:
 * @cache: a presence cache
 * @ver: a verification string or bundle name
 *
 * If the capabilities corresponding to @ver have been added to the cache with
 * gabble_presence_cache_add_own_caps(), returns a set of those capabilities;
 * otherwise, returns %NULL.
 *
 * Since the cache only records features Gabble understands (omitting unknown
 * features, identities, and data forms), we can only serve up disco replies
 * from the cache if we know we once advertised exactly this verification
 * string ourselves.
 *
 * Returns: a set of capabilities, if we know exactly what @ver means.
 */
const GabbleCapabilityInfo *
gabble_presence_cache_peek_own_caps (
    GabblePresenceCache *cache,
    const gchar *ver)
{
  gchar *uri = g_strdup_printf ("%s#%s", NS_GABBLE_CAPS, ver);
  GabbleCapabilityInfo *info = capability_info_get (cache, uri);

  g_free (uri);

  if (info->complete)
    {
      g_assert (info->cap_set != NULL);
      return info;
    }
  else
    {
      return NULL;
    }
}

void
gabble_presence_cache_really_remove (
    GabblePresenceCache *cache,
    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = cache->priv;
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

static gboolean
gabble_presence_cache_caps_pending (GabblePresenceCache *cache,
                                    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = cache->priv;
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

  g_list_free (uris);
  return FALSE;
}

/* Return whether we're "unsure" about the capabilities of @handle.
 * Currently, this means either of:
 *
 * - we've connected within the last UNSURE_PERIOD seconds and haven't
 *   received presence for @handle yet
 * - we know what @handle's caps hash/bundles are, but we're still
 *   performing service discovery to find out what they mean
 */
gboolean
gabble_presence_cache_is_unsure (GabblePresenceCache *cache,
    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = cache->priv;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);

  /* we might not have had any presence at all - if we're not connected yet, or
   * are still in the "unsure period", assume we might get initial presence
   * soon.
   *
   * Presences with keep_unavailable are the result of caching someone's
   * nick from <message> stanzas, so they don't count as real presence - if
   * someone sends us a <message>, their presence might still follow. */
  if (base_conn->status != TP_CONNECTION_STATUS_CONNECTED ||
      priv->unsure_id != 0)
    {
      GabblePresence *presence = gabble_presence_cache_get (cache, handle);

      if (presence == NULL || presence->keep_unavailable)
        {
          DEBUG ("No presence for %u yet, still waiting for possible initial "
              "presence burst", handle);
          return TRUE;
        }
    }

  /* FIXME: if we've had the roster, we can be sure that people who're
   * not in it won't be sending us an initial presence, so ideally the
   * above should be roster-aware? */

  /* if we don't know what the caps mean, we're unsure */
  if (gabble_presence_cache_caps_pending (cache, handle))
    {
      DEBUG ("Still working out what %u's caps hash means", handle);
      return TRUE;
    }

  /* if we're waiting for a de-cloak response, we're unsure */
  if (tp_handle_set_is_member (priv->decloak_handles, handle))
    {
      DEBUG ("Waiting to see if %u will decloak", handle);
      return TRUE;
    }

  DEBUG ("No, I'm sure about %u by now", handle);
  return FALSE;
}

static gboolean
gabble_presence_cache_decloak_timeout_cb (gpointer data)
{
  DecloakContext *dc = data;
  GabblePresenceCache *self = dc->cache;
  TpHandle handle = dc->handle;

  DEBUG ("De-cloak request for %u timed out", handle);

  /* This frees @dc, do not dereference it afterwards. This needs to be done
   * before emitting the signal, so that when recipients of the channel ask
   * whether we're unsure about the handle, there is no pending decloak
   * request that would make us unsure. */
  g_hash_table_remove (self->priv->decloak_requests,
      GUINT_TO_POINTER (handle));
  /* As a side-effect of freeing @dc, this should have happened. */
  g_assert (!tp_handle_set_is_member (self->priv->decloak_handles, handle));

  /* FIXME: this is an abuse of this signal, but it serves the same
   * purpose: poking any pending media channels to tell them that @handle
   * might have left the "unsure" state */
  emit_capabilities_discovered (self, handle);

  return FALSE;
}

/* @reason must be a statically-allocated string. */
gboolean
gabble_presence_cache_request_decloaking (GabblePresenceCache *self,
    TpHandle handle,
    const gchar *reason)
{
  DecloakContext *dc;
  GabblePresence *presence;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);

  presence = gabble_presence_cache_get (self, handle);

  if (presence != NULL &&
      presence->status != GABBLE_PRESENCE_OFFLINE &&
      presence->status != GABBLE_PRESENCE_UNKNOWN)
    {
      DEBUG ("We know that this contact is online, no point asking for "
          "decloak");
      return FALSE;
    }

  /* if we've already asked them to de-cloak for the same reason, do nothing */
  if (tp_handle_set_is_member (self->priv->decloak_handles, handle))
    {
      dc = g_hash_table_lookup (self->priv->decloak_requests,
          GUINT_TO_POINTER (handle));

      if (dc != NULL && !tp_strdiff (reason, dc->reason))
        {
          DEBUG ("Already asked %u to decloak for reason '%s'", handle,
              reason);
          return TRUE;
        }
    }

  DEBUG ("Asking %u to decloak", handle);

  dc = decloak_context_new (self, handle, reason);
  dc->timeout_id = g_timeout_add_seconds (DECLOAK_PERIOD,
      gabble_presence_cache_decloak_timeout_cb, dc);
  g_hash_table_insert (self->priv->decloak_requests, GUINT_TO_POINTER (handle),
      dc);
  tp_handle_set_add (self->priv->decloak_handles, handle);

  gabble_connection_request_decloak (self->priv->conn,
      tp_handle_inspect (contact_repo, handle), reason, NULL);

  return TRUE;
}

void
gabble_presence_cache_update_location (GabblePresenceCache *cache,
                                       TpHandle handle,
                                       GHashTable *new_location)
{
  GabblePresenceCachePrivate *priv = cache->priv;

  g_hash_table_insert (priv->location, GUINT_TO_POINTER (handle), new_location);

  g_signal_emit (cache, signals[LOCATION_UPDATED], 0, handle);
}

/* The return value should be g_hash_table_unref'ed. */
GHashTable *
gabble_presence_cache_get_location (GabblePresenceCache *cache,
                                    TpHandle handle)
{
  GabblePresenceCachePrivate *priv = cache->priv;
  GHashTable *location = NULL;

  location = g_hash_table_lookup (priv->location, GUINT_TO_POINTER (handle));
  if (location != NULL)
    {
      g_hash_table_ref (location);
      return location;
    }

  return NULL;
}

gboolean
gabble_presence_cache_disco_in_progress (GabblePresenceCache *cache,
    TpHandle handle,
    const gchar *resource)
{
  GabblePresenceCachePrivate *priv = cache->priv;
  GList *l, *waiters;
  gboolean out = FALSE;

  waiters = g_hash_table_get_values (priv->disco_pending);

  for (l = waiters; l != NULL; l = l->next)
    {
      DiscoWaiter *w = l->data;

      if (w != NULL && w->handle == handle && !tp_strdiff (w->resource, resource))
        {
          out = TRUE;
          break;
        }
    }

  g_list_free (waiters);

  return out;
}
