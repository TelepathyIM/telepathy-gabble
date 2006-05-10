
#include <stdlib.h>
#include <string.h>

#include "gabble-presence.h"

#include "gabble-presence-cache.h"

G_DEFINE_TYPE (GabblePresenceCache, gabble_presence_cache, G_TYPE_OBJECT);

/* signal enum */
enum
{
  PRESENCE_UPDATE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define GABBLE_PRESENCE_CACHE_PRIV(account) ((GabblePresenceCachePrivate *)account->priv)

typedef struct _GabblePresenceCachePrivate GabblePresenceCachePrivate;

struct _GabblePresenceCachePrivate
{
  LmConnection *lmconn;
  LmMessageHandler *presence_cb;
  GHashTable *presence;
  GabbleHandleRepo *handles;
};

static gboolean
_presence_has_google_voice (LmMessageNode *pres_node)
{
  LmMessageNode *node;

  for (node = pres_node->children; node; node = node->next)
    {
      const gchar *cap_xmlns, *cap_ext;

      if (strcmp (node->name, "c") != 0)
        continue;

      cap_xmlns = lm_message_node_get_attribute (node, "xmlns");
      if (NULL == cap_xmlns ||
          0 != strcmp (cap_xmlns, "http://jabber.org/protocol/caps"))
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

static GabblePresenceId
_presence_get_status (LmMessageNode *pres_node)
{
  const gchar *presence_show;
  LmMessageNode *child_node = lm_message_node_get_child (pres_node, "show");

  if (!child_node)
    {
      /*
      HANDLER_DEBUG (pres_node,
        "<presence> without <show> received from server, "
        "setting presence to available");
      */
      return GABBLE_PRESENCE_AVAILABLE;
    }

  presence_show = lm_message_node_get_value (child_node);

  if (!presence_show)
    {
      /*
      HANDLER_DEBUG (pres_node,
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
      HANDLER_DEBUG (pres_node,
        "unrecognised <show/> value received from server, "
        "setting presence to available");
      return GABBLE_PRESENCE_AVAILABLE;
    }
}

LmHandlerResult
gabble_presence_cache_parse_message (GabblePresenceCache *cache,
                                     GabbleHandle handle,
                                     const char *from,
                                     LmMessage *message)
{
  GabblePresenceCachePrivate *priv;
  gchar *resource;
  LmMessageNode *pres_node, *child_node;
  const gchar *status_message = NULL;
  gint8 priority = 0;
  GabblePresenceId presence_id;
  GabblePresence *presence;
  LmHandlerResult ret = LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  g_assert (cache != NULL);
  priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  g_assert (gabble_handle_is_valid (priv->handles, TP_HANDLE_TYPE_CONTACT,
        handle, NULL));
  g_assert (from != NULL);
  g_assert (message != NULL);

  pres_node = message->node;

  gabble_handle_decode_jid (from, NULL, NULL, &resource);
  if (resource == NULL)
    {
      HANDLER_DEBUG (pres_node, "ignoring presence with no resource");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  child_node = lm_message_node_get_child (pres_node, "status");
  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  child_node = lm_message_node_get_child (pres_node, "priority");
  if (child_node)
    {
      const gchar *prio = lm_message_node_get_value (child_node);
      priority = CLAMP (atoi (prio), G_MININT8, G_MAXINT8);
    }

  switch (lm_message_get_sub_type (message))
    {
    case LM_MESSAGE_SUB_TYPE_NOT_SET:
    case LM_MESSAGE_SUB_TYPE_AVAILABLE:
      presence_id = _presence_get_status (pres_node);
      gabble_presence_cache_update (cache, handle, resource, presence_id,
          status_message, priority);

      if (_presence_has_google_voice (pres_node))
        {
          presence = gabble_presence_cache_get (cache, handle);
          g_assert (presence);
          g_debug ("%s: %s has voice-v1 support", G_STRFUNC, from);
          gabble_presence_set_capabilities (presence, resource,
              PRESENCE_CAP_GOOGLE_VOICE);
        }

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      goto OUT;

    case LM_MESSAGE_SUB_TYPE_ERROR:
      HANDLER_DEBUG (pres_node, "setting contact offline due to error");
    case LM_MESSAGE_SUB_TYPE_UNAVAILABLE:
      gabble_presence_cache_update (cache, handle, resource,
          GABBLE_PRESENCE_OFFLINE, status_message, priority);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      goto OUT;

    default:
      break;
    }

OUT:
  g_free (resource);
  return ret;
}


/**
 * connection_presence_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
gabble_presence_cache_presence_cb (LmMessageHandler *handler,
                                   LmConnection *lmconn,
                                   LmMessage *message,
                                   gpointer user_data)
{
  GabblePresenceCache *cache = GABBLE_PRESENCE_CACHE (user_data);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  const char *from;
  GabbleHandle handle;

  g_assert (lmconn == priv->lmconn);

  from = lm_message_node_get_attribute (message->node, "from");
  if (from == NULL)
    {
      HANDLER_DEBUG (message->node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = gabble_handle_for_contact (priv->handles, from, FALSE);
  if (handle == 0)
    {
      HANDLER_DEBUG (message->node, "ignoring presence from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  return gabble_presence_cache_parse_message (cache, handle, from, message);
}


GabblePresenceCache *
gabble_presence_cache_new (LmConnection *lmconn, GabbleHandleRepo *handles)
{
  GabblePresenceCache *new = g_object_new (GABBLE_TYPE_PRESENCE_CACHE, NULL);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (new);

  priv->lmconn = lmconn;
  priv->handles = handles;
  priv->presence_cb = lm_message_handler_new (gabble_presence_cache_presence_cb,
                                              new, NULL);
  lm_connection_register_message_handler (priv->lmconn, priv->presence_cb,
                                          LM_MESSAGE_TYPE_PRESENCE,
                                          LM_HANDLER_PRIORITY_NORMAL);

  return new;
}

static void
gabble_presence_cache_finalize (GObject *object)
{
  GabblePresenceCache *cache = GABBLE_PRESENCE_CACHE (object);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  lm_connection_unregister_message_handler (priv->lmconn, priv->presence_cb,
                                            LM_MESSAGE_TYPE_PRESENCE);
  lm_message_handler_unref (priv->presence_cb);

  g_hash_table_destroy (priv->presence);
}

static void
gabble_presence_cache_class_init (GabblePresenceCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (GabblePresenceCachePrivate));
  object_class->finalize = gabble_presence_cache_finalize;

  signals[PRESENCE_UPDATE] = g_signal_new (
    "presence-update",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET (GabblePresenceCacheClass, presence_update),
    NULL, NULL,
    g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
gabble_presence_cache_init (GabblePresenceCache *cache)
{
  cache->priv = G_TYPE_INSTANCE_GET_PRIVATE (cache,
      GABBLE_TYPE_PRESENCE_CACHE, GabblePresenceCachePrivate);
  ((GabblePresenceCachePrivate *)cache->priv)->presence = g_hash_table_new_full (
    NULL, NULL, NULL, g_object_unref);
}

GabblePresence *
gabble_presence_cache_get (GabblePresenceCache *cache, GabbleHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  return g_hash_table_lookup (priv->presence, GINT_TO_POINTER (handle));
}

void
gabble_presence_cache_update (GabblePresenceCache *cache, GabbleHandle handle, const gchar *resource, GabblePresenceId presence_id, const gchar *status_message, gint8 priority)
{
  GabblePresence *presence = gabble_presence_cache_get (cache, handle);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);
  const gchar *jid = gabble_handle_inspect (priv->handles,
      TP_HANDLE_TYPE_CONTACT, handle);

  g_debug ("%s: %s (%d) resource %s presence %d message %s prio %d", G_STRFUNC,
      jid, handle, resource, presence_id, status_message, priority);

  if (presence == NULL)
    {
      presence = gabble_presence_new ();
      g_hash_table_insert (priv->presence, GINT_TO_POINTER (handle), presence);
    }

  if (gabble_presence_update (presence, resource, presence_id, status_message, priority))
    g_signal_emit (cache, signals[PRESENCE_UPDATE], 0, handle);
}

