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

#include <stdlib.h>
#include <string.h>

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "debug.h"
#include "gabble-presence.h"
#include "namespaces.h"
#include "util.h"
#include "handle-set.h"

#include "gabble-presence-cache.h"

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
  PRESENCE_UPDATE,
  NICKNAME_UPDATE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define GABBLE_PRESENCE_CACHE_PRIV(account) ((GabblePresenceCachePrivate *)account->priv)

typedef struct _GabblePresenceCachePrivate GabblePresenceCachePrivate;

struct _GabblePresenceCachePrivate
{
  GabbleConnection *conn;

  gulong status_changed_cb;
  LmMessageHandler *lm_message_cb;

  GHashTable *presence;
  GabbleHandleSet *presence_handles;

  gboolean dispose_has_run;
};

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
    GabbleHandle handle);

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
                                    "Gabble connection object that owns this "
                                    "presence cache.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_CONNECTION,
                                   param_spec);

  signals[PRESENCE_UPDATE] = g_signal_new (
    "presence-update",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
  signals[NICKNAME_UPDATE] = g_signal_new (
    "nickname-update",
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

  handle_set_destroy (priv->presence_handles);
  priv->presence_handles = NULL;

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
  GabbleHandleSet *new_presence_handles;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      new_presence_handles = handle_set_new (priv->conn->handles, TP_HANDLE_TYPE_CONTACT);

      if (priv->presence_handles)
        {
          const GIntSet *add;
          GIntSet *tmp;
          add = handle_set_peek (priv->presence_handles);
          tmp = handle_set_update (new_presence_handles, add);
          handle_set_destroy (priv->presence_handles);
          g_intset_destroy (tmp);
        }
      priv->presence_handles = new_presence_handles;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static gboolean
_presence_node_has_google_voice (LmMessageNode *pres_node)
{
  LmMessageNode *node;

  for (node = pres_node->children; node; node = node->next)
    {
      const gchar *cap_xmlns, *cap_ext;

      if (strcmp (node->name, "c") != 0)
        continue;

      cap_xmlns = lm_message_node_get_attribute (node, "xmlns");
      if (NULL == cap_xmlns || 0 != strcmp (cap_xmlns, NS_CAPS))
        continue;

      cap_ext = lm_message_node_get_attribute (node, "ext");
      if (NULL != cap_ext)
        {
          gchar **features, **tmp;
          gboolean found = FALSE;

          features = g_strsplit (cap_ext, " ", 0);

          for (tmp = features; *tmp; tmp++)
            {
              if (0 == strcmp (*tmp, "voice-v1"))
                {
                  found = TRUE;
                  break;
                }
            }

          g_strfreev (features);

          if (found)
            {
              return TRUE;
            }
        }
      else
        {
          continue;
        }
    }

  return FALSE;
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
    case TP_CONN_STATUS_CONNECTING:
      g_assert (priv->lm_message_cb == NULL);

      priv->lm_message_cb = lm_message_handler_new (gabble_presence_cache_lm_message_cb,
                                                    cache, NULL);
      lm_connection_register_message_handler (priv->conn->lmconn,
                                              priv->lm_message_cb,
                                              LM_MESSAGE_TYPE_PRESENCE,
                                              LM_HANDLER_PRIORITY_LAST);
      lm_connection_register_message_handler (priv->conn->lmconn,
                                              priv->lm_message_cb,
                                              LM_MESSAGE_TYPE_MESSAGE,
                                              LM_HANDLER_PRIORITY_FIRST);
      break;
    case TP_CONN_STATUS_CONNECTED:
      /* TODO: emit self presence */
      break;
    case TP_CONN_STATUS_DISCONNECTED:
      g_assert (priv->lm_message_cb != NULL);

      lm_connection_unregister_message_handler (conn->lmconn,
                                                priv->lm_message_cb,
                                                LM_MESSAGE_TYPE_PRESENCE);
      lm_connection_unregister_message_handler (conn->lmconn,
                                                priv->lm_message_cb,
                                                LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (priv->lm_message_cb);
      priv->lm_message_cb = NULL;
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
                GabbleHandle handle,
                const gchar *from,
                LmMessageNode *node)
{
  const gchar *nickname;
  const gchar *xmlns;
  GabblePresence *presence;

  node = lm_message_node_find_child (node, "nick");

  if (NULL == node)
    return;

  xmlns = lm_message_node_get_attribute (node, "xmlns");

  if (0 != strcmp(xmlns, NS_NICK))
    return;

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL == presence)
    return;

  nickname = lm_message_node_get_value (node);
  DEBUG ("got nickname \"%s\" for %s", nickname, from);

  if (g_strdiff (presence->nickname, nickname))
    {
      if (NULL != presence->nickname)
        g_free (presence->nickname);

      presence->nickname = g_strdup (nickname);
      g_signal_emit (cache, signals[NICKNAME_UPDATE], 0, handle);
    }
}

static LmHandlerResult
_parse_presence_message (GabblePresenceCache *cache,
                         GabbleHandle handle,
                         const gchar *from,
                         LmMessage *message)
{
  gint8 priority = 0;
  gchar *resource = NULL;
  const gchar *status_message = NULL;
  LmMessageNode *presence_node, *child_node;
  LmHandlerResult ret = LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
  GabblePresenceId presence_id;
  GabblePresence *presence;

  presence_node = message->node;
  g_assert (0 == strcmp (presence_node->name, "presence"));

  gabble_decode_jid (from, NULL, NULL, &resource);

  if (resource == NULL)
    {
       NODE_DEBUG (presence_node, "ignoring presence with no resource");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL != presence)
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

      if (_presence_node_has_google_voice (presence_node))
        {
          presence = gabble_presence_cache_get (cache, handle);
          g_assert (NULL != presence);
          DEBUG ("%s has voice-v1 support", from);
          gabble_presence_set_capabilities (presence, resource,
              PRESENCE_CAP_GOOGLE_VOICE);
        }

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;

    case LM_MESSAGE_SUB_TYPE_ERROR:
       NODE_DEBUG (presence_node, "setting contact offline due to error");
      /* fall through */

    case LM_MESSAGE_SUB_TYPE_UNAVAILABLE:
      gabble_presence_cache_update (cache, handle, resource,
          GABBLE_PRESENCE_OFFLINE, status_message, priority);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;

    default:
      break;
    }

  _grab_nickname (cache, handle, from, presence_node);
  g_free (resource);
  return ret;
}

static LmHandlerResult
_parse_message_message (GabblePresenceCache *cache,
                        GabbleHandle handle,
                        const gchar *from,
                        LmMessage *message)
{
  LmMessageNode *node;
  GabblePresence *presence;

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
  const char *from;
  GabbleHandle handle;

  g_assert (lmconn == priv->conn->lmconn);

  from = lm_message_node_get_attribute (message->node, "from");

  if (NULL == from)
    {
      HANDLER_DEBUG (message->node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = gabble_handle_for_contact (priv->conn->handles, from, FALSE);

  if (0 == handle)
    {
      HANDLER_DEBUG (message->node, "ignoring presence from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (handle == priv->conn->self_handle)
    {
      HANDLER_DEBUG (message->node, "ignoring presence from ourselves on another resource");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  switch (lm_message_get_type (message))
    {
    case LM_MESSAGE_TYPE_PRESENCE:
      return _parse_presence_message (cache, handle, from, message);
    case LM_MESSAGE_TYPE_MESSAGE:
      return _parse_message_message (cache, handle, from, message);
    default:
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
}


GabblePresenceCache *
gabble_presence_cache_new (GabbleConnection *conn)
{
  return g_object_new (GABBLE_TYPE_PRESENCE_CACHE,
                       "connection", conn,
                       NULL);
}

GabblePresence *
gabble_presence_cache_get (GabblePresenceCache *cache, GabbleHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  g_assert (gabble_handle_is_valid (priv->conn->handles,
        TP_HANDLE_TYPE_CONTACT, handle, NULL));

  return g_hash_table_lookup (priv->presence, GINT_TO_POINTER (handle));
}

void
gabble_presence_cache_maybe_remove (
    GabblePresenceCache *cache,
    GabbleHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  GabblePresence *presence;

  presence = gabble_presence_cache_get (cache, handle);

  if (NULL == presence)
    return;

  if (presence->status == GABBLE_PRESENCE_OFFLINE &&
      presence->status_message == NULL &&
      !presence->keep_unavailable)
    {
      const gchar *jid;

      jid = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_CONTACT,
          handle);
      DEBUG ("discarding cached presence for unavailable jid %s", jid);
      g_hash_table_remove (priv->presence, GINT_TO_POINTER (handle));
      gabble_handle_unref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
    }
}

static GabblePresence *
_cache_insert (
    GabblePresenceCache *cache,
    GabbleHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  GabblePresence *presence;

  presence = gabble_presence_new ();
  g_hash_table_insert (priv->presence, GINT_TO_POINTER (handle), presence);
  handle_set_add (priv->presence_handles, handle);
  return presence;
}

void
gabble_presence_cache_update (
    GabblePresenceCache *cache,
    GabbleHandle handle,
    const gchar *resource,
    GabblePresenceId presence_id,
    const gchar *status_message,
    gint8 priority)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  const gchar *jid;
  GabblePresence *presence;

  jid = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_CONTACT,
      handle);
  DEBUG ("%s (%d) resource %s prio %d presence %d message \"%s\"",
      jid, handle, resource, priority, presence_id, status_message);

  presence = gabble_presence_cache_get (cache, handle);

  if (presence == NULL)
    presence = _cache_insert (cache, handle);

  if (gabble_presence_update (presence, resource, presence_id, status_message,
        priority))
    g_signal_emit (cache, signals[PRESENCE_UPDATE], 0, handle);

  gabble_presence_cache_maybe_remove (cache, handle);
}

