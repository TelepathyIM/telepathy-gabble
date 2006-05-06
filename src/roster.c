/*
 * roster.c - Source for Gabble roster helper
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <string.h>

#include "tp-channel-factory-iface.h"

#include "gabble-connection.h"
#include "gabble-roster-channel.h"
#include "roster.h"

#define NS_ROSTER             "jabber:iq:roster"

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleRosterPrivate GabbleRosterPrivate;
struct _GabbleRosterPrivate
{
  GabbleConnection *conn;

  LmMessageHandler *iq_cb;
  LmMessageHandler *presence_cb;

  GHashTable *channels;

  gboolean roster_received;
  gboolean dispose_has_run;
};

static void gabble_roster_factory_iface_init ();
static void gabble_roster_init (GabbleRoster *roster);
static GObject * gabble_roster_constructor (GType type, guint n_props, GObjectConstructParam *props);
static void gabble_roster_dispose (GObject *object);
static void gabble_roster_finalize (GObject *object);
static void gabble_roster_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gabble_roster_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static LmHandlerResult gabble_roster_iq_cb (LmMessageHandler *, LmConnection *, LmMessage *, gpointer);
static LmHandlerResult gabble_roster_presence_cb (LmMessageHandler *, LmConnection *, LmMessage *, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleRoster, gabble_roster, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, gabble_roster_factory_iface_init));

#define GABBLE_ROSTER_GET_PRIVATE(o)     ((GabbleRosterPrivate*) ((o)->priv));

static void
gabble_roster_class_init (GabbleRosterClass *gabble_roster_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roster_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_roster_class, sizeof (GabbleRosterPrivate));

  object_class->constructor = gabble_roster_constructor;

  object_class->dispose = gabble_roster_dispose;
  object_class->finalize = gabble_roster_finalize;

  object_class->get_property = gabble_roster_get_property;
  object_class->set_property = gabble_roster_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "XMPP roster object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_CONNECTION,
                                   param_spec);
}

static void
gabble_roster_init (GabbleRoster *obj)
{
  GabbleRosterPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_ROSTER, GabbleRosterPrivate);

  obj->priv = priv;

  priv->channels = g_hash_table_new_full (g_direct_hash,
                                          g_direct_equal,
                                          NULL,
                                          g_object_unref);
}

static GObject *
gabble_roster_constructor (GType type, guint n_props,
                           GObjectConstructParam *props)
{
  GObject *obj;
  GabbleRosterPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_roster_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_ROSTER_GET_PRIVATE (GABBLE_ROSTER (obj));

  priv->iq_cb = lm_message_handler_new (gabble_roster_iq_cb,
                                        obj, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
                                          priv->iq_cb,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_HANDLER_PRIORITY_NORMAL);

  priv->presence_cb = lm_message_handler_new (gabble_roster_presence_cb,
                                              obj, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
                                          priv->presence_cb,
                                          LM_MESSAGE_TYPE_PRESENCE,
                                          LM_HANDLER_PRIORITY_NORMAL);

  return obj;
}

void
gabble_roster_dispose (GObject *object)
{
  GabbleRoster *self = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  g_debug ("%s: dispose called", G_STRFUNC);

  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_disconnected (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->iq_cb == NULL);
  g_assert (priv->presence_cb == NULL);

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_roster_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_parent_class)->dispose (object);
}

void
gabble_roster_finalize (GObject *object)
{
  g_debug ("%s called with %p", G_STRFUNC, object);

  G_OBJECT_CLASS (gabble_roster_parent_class)->finalize (object);
}

static void
gabble_roster_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleRoster *chan = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (chan);

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
gabble_roster_set_property (GObject     *object,
                           guint        property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GabbleRoster *chan = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

GabbleRoster *
gabble_roster_new (GabbleConnection *conn)
{
  g_return_val_if_fail (conn != NULL, NULL);

  return g_object_new (GABBLE_TYPE_ROSTER,
                       "connection", conn,
                       NULL);
}

static GabbleRosterChannel *
_gabble_roster_create_channel (GabbleRoster *roster, GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterChannel *chan;
  const char *name;
  char *object_path;

  g_assert (priv->channels != NULL);
  g_assert (g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)) == NULL);

  name = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_LIST, handle);
  object_path = g_strdup_printf ("%s/RosterChannel/%s", priv->conn->object_path, name);
  chan = g_object_new (GABBLE_TYPE_ROSTER_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  g_debug ("%s: created %s", G_STRFUNC, object_path);
  g_free (object_path);

  g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

  if (priv->roster_received)
    {
      g_debug ("%s: roster already received, emitting signal for %s list channel",
          G_STRFUNC, name);

      g_signal_emit_by_name (roster, "new-channel", chan);
    }
  else
    {
      g_debug ("%s: roster not yet received, not emitting signal for %s list channel",
          G_STRFUNC, name);
    }

  return chan;
}

static GabbleRosterChannel *
_gabble_roster_get_channel (GabbleRoster *roster, GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterChannel *chan;

  g_assert (priv->channels != NULL);
  g_assert (gabble_handle_is_valid (priv->conn->handles, TP_HANDLE_TYPE_LIST, handle, NULL));

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

  if (chan == NULL)
    chan = _gabble_roster_create_channel (roster, handle);

  return chan;
}

static void
_gabble_roster_emit_one (gpointer key, gpointer value, gpointer data)
{
  GabbleRoster *roster = GABBLE_ROSTER (data);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (value);
  GabbleHandle handle = GPOINTER_TO_INT (key);
  const gchar *name = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_LIST, handle);

  g_debug ("%s: roster now received, emitting signal signal for %s list channel",
      G_STRFUNC, name);

  g_signal_emit_by_name (roster, "new-channel", chan);
}

static void
_gabble_roster_received (GabbleRoster *roster)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  g_assert (priv->channels != NULL);

  if (!priv->roster_received)
    {
      priv->roster_received = TRUE;

      g_hash_table_foreach (priv->channels, _gabble_roster_emit_one, roster);
    }
}

/**
 * gabble_roster_iq_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with roster queries, and allows other handlers
 * if queries other than rosters are received.
 */
static LmHandlerResult
gabble_roster_iq_cb (LmMessageHandler *handler,
                     LmConnection *lmconn,
                     LmMessage *message,
                     gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessageNode *iq_node, *query_node;

  g_assert (lmconn == priv->conn->lmconn);

  if (priv->channels == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  iq_node = lm_message_get_node (message);
  query_node = lm_message_node_get_child (iq_node, "query");

  if (!query_node || strcmp (NS_ROSTER,
        lm_message_node_get_attribute (query_node, "xmlns")))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* TODO: check it's from the server or us */

  /* if this is a result, it's from our initial query. if it's a set,
   * it's a roster push. either way, parse the items. */
  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_RESULT ||
      lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_SET)
    {
      LmMessageNode *item_node;
      GIntSet *empty, *pub_add, *pub_rem,
              *sub_add, *sub_rem, *sub_rp;

      /* asymmetry is because we don't get locally pending subscription
       * requests via <roster>, we get it via <presence> */
      empty = g_intset_new ();
      pub_add = g_intset_new ();
      pub_rem = g_intset_new ();
      sub_add = g_intset_new ();
      sub_rem = g_intset_new ();
      sub_rp = g_intset_new ();

      /* iterate every sub-node, which we expect to be <item>s */
      for (item_node = query_node->children;
           item_node;
           item_node = item_node->next)
        {
          const char *jid, *subscription, *ask;
          GabbleHandle handle;

          if (strcmp (item_node->name, "item"))
            {
              HANDLER_DEBUG (item_node, "query sub-node is not item, skipping");
              continue;
            }

          jid = lm_message_node_get_attribute (item_node, "jid");
          if (!jid)
            {
              HANDLER_DEBUG (item_node, "item node has no jid, skipping");
              continue;
            }

          handle = gabble_handle_for_contact (priv->conn->handles, jid, FALSE);
          if (handle == 0)
            {
              HANDLER_DEBUG (item_node, "item jid is malformed, skipping");
              continue;
            }

          subscription = lm_message_node_get_attribute (item_node, "subscription");
          if (!subscription)
            {
              HANDLER_DEBUG (item_node, "item node has no subscription, skipping");
              continue;
            }

          ask = lm_message_node_get_attribute (item_node, "ask");

          if (!strcmp (subscription, "both"))
            {
              g_intset_add (pub_add, handle);
              g_intset_add (sub_add, handle);
            }
          else if (!strcmp (subscription, "from"))
            {
              g_intset_add (pub_add, handle);
              if (ask != NULL && !strcmp (ask, "subscribe"))
                g_intset_add (sub_rp, handle);
              else
                g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "none"))
            {
              g_intset_add (pub_rem, handle);
              if (ask != NULL && !strcmp (ask, "subscribe"))
                g_intset_add (sub_rp, handle);
              else
                g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "remove"))
            {
              g_intset_add (pub_rem, handle);
              g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "to"))
            {
              g_intset_add (pub_rem, handle);
              g_intset_add (sub_add, handle);
            }
          else
            {
              HANDLER_DEBUG (item_node, "got unexpected subscription value");
            }
        }

      if (g_intset_size (pub_add) > 0 ||
          g_intset_size (pub_rem) > 0)
        {
          GabbleHandle handle = gabble_handle_for_list_publish (priv->conn->handles);
          GabbleRosterChannel *publish = _gabble_roster_get_channel (roster, handle);

          g_debug ("%s: calling change members on publish channel", G_STRFUNC);
          gabble_group_mixin_change_members (G_OBJECT (publish),
              "", pub_add, pub_rem, empty, empty);
        }

      if (g_intset_size (sub_add) > 0 ||
          g_intset_size (sub_rem) > 0 ||
          g_intset_size (sub_rp) > 0)
        {
          GabbleHandle handle = gabble_handle_for_list_subscribe (priv->conn->handles);
          GabbleRosterChannel *subscribe = _gabble_roster_get_channel (roster, handle);

          g_debug ("%s: calling change members on subscribe channel", G_STRFUNC);
          gabble_group_mixin_change_members (G_OBJECT (subscribe),
              "", sub_add, sub_rem, empty, sub_rp);
        }

      g_intset_destroy (empty);
      g_intset_destroy (pub_add);
      g_intset_destroy (pub_rem);
      g_intset_destroy (sub_add);
      g_intset_destroy (sub_rem);
      g_intset_destroy (sub_rp);
    }
  else
    {
      HANDLER_DEBUG (iq_node, "unhandled roster IQ");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* if this is a SET, it's a roster push and the roster is now complete.
   * we should also send an acknowledgement if the IQ had an id */
  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_SET)
    {
      _gabble_roster_received (roster);
      _gabble_connection_send_iq_ack (priv->conn, iq_node, LM_MESSAGE_SUB_TYPE_RESULT);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

// TODO
// static void
// _gabble_roster_presence_ack ()
// {
// }

/**
 * connection_presence_roster_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
gabble_roster_presence_cb (LmMessageHandler *handler,
                           LmConnection *lmconn,
                           LmMessage *message,
                           gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessageNode *pres_node, *child_node;
  const char *from;
  LmMessageSubType sub_type;
  GIntSet *empty, *tmp;
  GabbleHandle handle;
  LmMessage *reply = NULL;
  const gchar *status_message = NULL;
  GabbleRosterChannel *chan = NULL;

  g_assert (lmconn == priv->conn->lmconn);

  if (priv->channels == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  pres_node = lm_message_get_node (message);

  from = lm_message_node_get_attribute (pres_node, "from");

  if (from == NULL)
    {
      HANDLER_DEBUG (pres_node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (message);

/* TODO: if (node_is_for_muc (pres_node, NULL))
    {
      HANDLER_DEBUG (pres_node, "ignoring MUC presence");

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    } */

  handle = gabble_handle_for_contact (priv->conn->handles, from, FALSE);

  if (handle == 0)
    {
      HANDLER_DEBUG (pres_node, "ignoring presence from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (handle == priv->conn->self_handle)
    {
      HANDLER_DEBUG (pres_node, "ignoring presence from ourselves on another resource");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  g_assert (handle != 0);

  child_node = lm_message_node_get_child (pres_node, "status");
  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  switch (sub_type)
    {
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBE:
      g_debug ("%s: making %s (handle %u) local pending on the publish channel",
          G_STRFUNC, from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = gabble_handle_for_list_publish (priv->conn->handles);
      chan = _gabble_roster_get_channel (roster, handle);
      gabble_group_mixin_change_members (G_OBJECT (chan), status_message,
          empty, empty, tmp, empty);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE:
      g_debug ("%s: removing %s (handle %u) from the publish channel",
          G_STRFUNC, from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = gabble_handle_for_list_publish (priv->conn->handles);
      chan = _gabble_roster_get_channel (roster, handle);
      gabble_group_mixin_change_members (G_OBJECT (chan), status_message,
          empty, tmp, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED);
      _gabble_connection_send (priv->conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBED:
      g_debug ("%s: adding %s (handle %u) to the subscribe channel",
          G_STRFUNC, from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = gabble_handle_for_list_subscribe (priv->conn->handles);
      chan = _gabble_roster_get_channel (roster, handle);
      gabble_group_mixin_change_members (G_OBJECT (chan), status_message, tmp,
          empty, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_SUBSCRIBE);
      _gabble_connection_send (priv->conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED:
      g_debug ("%s: removing %s (handle %u) from the subscribe channel",
          G_STRFUNC, from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = gabble_handle_for_list_subscribe (priv->conn->handles);
      chan = _gabble_roster_get_channel (roster, handle);
      gabble_group_mixin_change_members (G_OBJECT (chan), status_message,
          empty, tmp, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);
      _gabble_connection_send (priv->conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    default:
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
}

static void
gabble_roster_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  if (priv->channels)
    {
      g_hash_table_destroy (priv->channels);
      priv->channels = NULL;
    }
}

static void
gabble_roster_factory_iface_connected (TpChannelFactoryIface *iface)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessage *message;
  LmMessageNode *msgnode;

  g_debug ("%s: requesting roster", G_STRFUNC);

  message = lm_message_new_with_sub_type (NULL,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_MESSAGE_SUB_TYPE_GET);

  msgnode = lm_message_node_add_child (lm_message_get_node (message),
                                       "query", NULL);

  lm_message_node_set_attribute (msgnode, "xmlns", NS_ROSTER);

  _gabble_connection_send (priv->conn, message, NULL);

  lm_message_unref (message);
}

void
gabble_roster_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  g_debug ("%s: removing callbacks", G_STRFUNC);

  if (priv->iq_cb)
    {
      lm_connection_unregister_message_handler (priv->conn->lmconn,
                                                priv->iq_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_cb);
      priv->iq_cb = NULL;
    }

  if (priv->presence_cb)
    {
      lm_connection_unregister_message_handler (priv->conn->lmconn,
                                                priv->presence_cb,
                                                LM_MESSAGE_TYPE_PRESENCE);
      lm_message_handler_unref (priv->presence_cb);
      priv->presence_cb = NULL;
    }
}

void
gabble_roster_factory_iface_foreach (TpChannelFactoryIface *iface,
                                     TpChannelFunc func,
                                     gpointer data)
{
  // TODO
//   GabbleRoster *roster = GABBLE_ROSTER (iface);
//   GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
}

TpChannelFactoryRequestStatus
gabble_roster_factory_iface_request (TpChannelFactoryIface *iface,
                                     const gchar *chan_type,
                                     TpHandleType handle_type,
                                     guint handle,
                                     TpChannelIface **ret)
{
  // TODO
//   GabbleRoster *roster = GABBLE_ROSTER (iface);
//   GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

//       GabbleRosterChannel *chan;
// 
//       if (handle_type != TP_HANDLE_TYPE_LIST)
//         goto NOT_AVAILABLE;
// 
//       if (!gabble_handle_is_valid (obj->handles,
//                                    handle_type,
//                                    handle,
//                                    error))
//         return FALSE;
// 
//       if (handle == gabble_handle_for_list_publish (obj->handles))
//         chan = priv->publish_channel;
//       else if (handle == gabble_handle_for_list_subscribe (obj->handles))
//         chan = priv->subscribe_channel;
//       else
//         g_assert_not_reached ();
// 
//       g_object_get (chan, "object-path", ret, NULL);

  return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
}

static void
gabble_roster_factory_iface_init (gpointer g_iface,
                                  gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_roster_factory_iface_close_all;
  klass->connected = gabble_roster_factory_iface_connected;
  klass->disconnected = gabble_roster_factory_iface_disconnected;
  klass->foreach = gabble_roster_factory_iface_foreach;
  klass->request = gabble_roster_factory_iface_request;
}
