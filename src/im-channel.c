/*
 * gabble-im-channel.c - Source for GabbleIMChannel
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
#include "im-channel.h"

#include <string.h>

#include <dbus/dbus-glib.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define DEBUG_FLAG GABBLE_DEBUG_IM
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "message-util.h"
#include "namespaces.h"
#include "presence.h"
#include "presence-cache.h"
#include "roster.h"
#include "util.h"

static void destroyable_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleIMChannel, gabble_im_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE,
      tp_message_mixin_chat_state_iface_init)
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE,
      destroyable_iface_init));

static void _gabble_im_channel_send_message (GObject *object,
    TpMessage *message, TpMessageSendingFlags flags);
static void gabble_im_channel_close (TpBaseChannel *base_chan);
static gboolean _gabble_im_channel_send_chat_state (GObject *object,
    TpChannelChatState state,
    GError **error);


/* private structure */

typedef enum {
  CHAT_STATES_UNKNOWN,
  CHAT_STATES_SUPPORTED,
  CHAT_STATES_NOT_SUPPORTED
} ChatStateSupport;

struct _GabbleIMChannelPrivate
{
  gchar *peer_jid;
  gboolean send_nick;
  ChatStateSupport chat_states_supported;
  GHashTable *pending_messages;
  gboolean send_chat_markers;
  gboolean force_receipts;
  gboolean force_chat_markers;

  gboolean dispose_has_run;
};

typedef struct {
  GabbleIMChannel *channel;
  TpMessage *message;
  gchar *token;
  TpMessageSendingFlags flags;
} _GabbleIMSendMessageCtx;

static GPtrArray *
gabble_im_channel_get_interfaces (TpBaseChannel *base)
{
  GPtrArray *interfaces;

  interfaces = TP_BASE_CHANNEL_CLASS (
      gabble_im_channel_parent_class)->get_interfaces (base);

  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE);
  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_MESSAGES);
  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE);

  return interfaces;
}

static void
gabble_im_channel_init (GabbleIMChannel *self)
{
  GabbleIMChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_IM_CHANNEL, GabbleIMChannelPrivate);

  self->priv = priv;
}

static void _gabble_im_channel_pending_messages_removed_cb ( TpSvcChannelInterfaceMessages *iface,
                                GArray *arg_Message_IDs,
                                gpointer user_data)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (user_data);
  GabbleIMChannelPrivate *priv = self->priv;

  int size = g_array_get_element_size (arg_Message_IDs);

  for (int i=0; i<size; i++)
    {
      guint id = g_array_index (arg_Message_IDs, guint, i);
      gchar* token;

      DEBUG ("lookup messageid: %u", id);
      token = g_hash_table_lookup (priv->pending_messages, GINT_TO_POINTER (id));

      if (token)
        {
          TpBaseChannel *base = TP_BASE_CHANNEL (self);
          TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
          GabbleConnection *conn = GABBLE_CONNECTION (base_conn);

          WockyStanza *report = wocky_stanza_build (
            WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_CHAT,
            NULL, priv->peer_jid,
            '(', "displayed", ':', NS_CHAT_MARKERS,
              '@', "id", token,
            ')', NULL);

          _gabble_connection_send (conn, report, NULL);

          DEBUG ("messageid: %u = %s", id, token);

          g_hash_table_remove (priv->pending_messages, GINT_TO_POINTER (id));
        }
      else
          DEBUG ("messageid: %u not found", id);
    }

  return;
}

static void
gabble_im_channel_constructed (GObject *obj)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (obj);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GabbleIMChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  TpHandleRepoIface *contact_handles =
      tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle target = tp_base_channel_get_target_handle (base);

  TpChannelTextMessageType types[] = {
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
  };
  const gchar * supported_content_types[] = {
      "text/plain",
      NULL
  };
  void (*chain_up) (GObject *) =
    ((GObjectClass *) gabble_im_channel_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (obj);

  priv->peer_jid = g_strdup (tp_handle_inspect (contact_handles, target));

  if (gabble_roster_handle_gets_presence_from_us (conn->roster, target))
    priv->send_nick = FALSE;
  else
    priv->send_nick = TRUE;

  tp_message_mixin_init (obj, G_STRUCT_OFFSET (GabbleIMChannel, message_mixin),
      base_conn);

  /* We deliberately do not include
   * TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_SUCCESSES here, even though we
   * support requesting receipts, because XEP-0184 provides no guarantees.
   */
  tp_message_mixin_implement_sending (obj, _gabble_im_channel_send_message,
      G_N_ELEMENTS (types), types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES,
      supported_content_types);

  priv->chat_states_supported = CHAT_STATES_UNKNOWN;
  tp_message_mixin_implement_send_chat_state (obj,
      _gabble_im_channel_send_chat_state);

  priv->pending_messages = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  g_object_get (conn, "send-chat-markers", &priv->send_chat_markers, NULL);
  g_object_get (conn, "force-chat-markers", &priv->force_chat_markers, NULL);
  g_object_get (conn, "force-receipts", &priv->force_receipts, NULL);

  if (priv->send_chat_markers)
    g_signal_connect (obj, "pending-messages-removed", (GCallback)_gabble_im_channel_pending_messages_removed_cb, self);
}

static void gabble_im_channel_dispose (GObject *object);
static void gabble_im_channel_finalize (GObject *object);

static void
gabble_im_channel_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      gabble_im_channel_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
      NULL);
}

static gchar *
gabble_im_channel_get_object_path_suffix (TpBaseChannel *chan)
{
  return g_strdup_printf ("ImChannel%u",
      tp_base_channel_get_target_handle (chan));
}

static void
gabble_im_channel_class_init (GabbleIMChannelClass *gabble_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_im_channel_class);
  TpBaseChannelClass *base_class =
      TP_BASE_CHANNEL_CLASS (gabble_im_channel_class);

  g_type_class_add_private (gabble_im_channel_class,
      sizeof (GabbleIMChannelPrivate));

  object_class->constructed = gabble_im_channel_constructed;
  object_class->dispose = gabble_im_channel_dispose;
  object_class->finalize = gabble_im_channel_finalize;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
  base_class->get_interfaces = gabble_im_channel_get_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_class->close = gabble_im_channel_close;
  base_class->fill_immutable_properties =
    gabble_im_channel_fill_immutable_properties;
  base_class->get_object_path_suffix = gabble_im_channel_get_object_path_suffix;

  tp_message_mixin_init_dbus_properties (object_class);
}

static gboolean
chat_states_supported (GabbleIMChannel *self,
                       gboolean include_unknown)
{
  GabbleIMChannelPrivate *priv = self->priv;
  TpBaseChannel *base = (TpBaseChannel *) self;
  GabbleConnection *conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));
  GabblePresence *presence;

  presence = gabble_presence_cache_get (conn->presence_cache,
      tp_base_channel_get_target_handle (base));

  if (presence != NULL && gabble_presence_has_cap (presence, NS_CHAT_STATES))
    return TRUE;

  switch (priv->chat_states_supported)
    {
      case CHAT_STATES_UNKNOWN:
        return include_unknown;
      case CHAT_STATES_SUPPORTED:
        return TRUE;
      case CHAT_STATES_NOT_SUPPORTED:
        return FALSE;
      default:
        g_assert_not_reached ();
        return FALSE;
    }
}

static gboolean
receipts_conceivably_supported (
    GabbleIMChannel *self)
{
  TpBaseChannel *base = (TpBaseChannel *) self;
  GabbleConnection *conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));
  GabblePresence *presence;

  presence = gabble_presence_cache_get (conn->presence_cache,
      tp_base_channel_get_target_handle (base));

  /* ...except it's never null because _parse_message_message() in
   * presence-cache.c. I hate this exactly as much as I did when I wrote the
   * FIXME on that function. */
  if (presence != NULL)
    return gabble_presence_has_cap (presence, NS_RECEIPTS);

  /* Otherwise ... who knows. Why not ask for one? */
  return TRUE;
}

static void
gabble_im_channel_dispose (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  TpBaseChannel *base = (TpBaseChannel *) self;
  GabbleIMChannelPrivate *priv = self->priv;
  GabbleConnection *conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));
  TpHandle target = tp_base_channel_get_target_handle (base);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!gabble_roster_handle_sends_presence_to_us (conn->roster, target))
    {
      GabblePresence *presence = gabble_presence_cache_get (
          conn->presence_cache, target);

      if (NULL != presence)
        {
          presence->keep_unavailable = FALSE;
          gabble_presence_cache_maybe_remove (conn->presence_cache, target);
        }
    }

  tp_message_mixin_maybe_send_gone (object);

  if (G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose (object);
}

static void
gabble_im_channel_finalize (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = self->priv;

  /* free any data held directly by the object here */

  DEBUG ("%p", object);

  g_free (priv->peer_jid);

  tp_message_mixin_finalize (object);

  g_hash_table_unref (priv->pending_messages);

  G_OBJECT_CLASS (gabble_im_channel_parent_class)->finalize (object);
}

static void
_gabble_im_channel_message_sent_cb (GObject *source,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
    WockyPorter *porter = WOCKY_PORTER (source);
    GError *error = NULL;
    _GabbleIMSendMessageCtx *context = user_data;
    GabbleIMChannel *chan = context->channel;
    TpMessage *message = context->message;

    if (wocky_porter_send_finish (porter, res, &error))
      {
        tp_message_mixin_sent ((GObject *) chan, message, context->flags,
            context->token, NULL);
      }
    else
      {
        tp_message_mixin_sent ((GObject *) chan, context->message,
            0, NULL, error);
      }

    g_object_unref (context->channel);
    g_object_unref (context->message);
    g_free (context->token);
    g_slice_free (_GabbleIMSendMessageCtx, context);
}

static void
_gabble_im_channel_send_message (GObject *object,
                                 TpMessage *message,
                                 TpMessageSendingFlags flags)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  TpBaseChannel *base = (TpBaseChannel *) self;
  TpBaseConnection *base_conn;
  GabbleConnection *gabble_conn;
  GabbleIMChannelPrivate *priv;
  TpChannelChatState state = -1;
  WockyStanza *stanza = NULL;
  gchar *id = NULL;
  GError *error = NULL;
  WockyPorter *porter;
  _GabbleIMSendMessageCtx *context;

  g_assert (GABBLE_IS_IM_CHANNEL (self));
  priv = self->priv;

  base_conn = tp_base_channel_get_connection (base);
  gabble_conn = GABBLE_CONNECTION (base_conn);

  if (chat_states_supported (self, TRUE))
    {
      state = TP_CHANNEL_CHAT_STATE_ACTIVE;
      tp_message_mixin_change_chat_state (object,
          tp_base_connection_get_self_handle (base_conn), state);
    }

  stanza = gabble_message_util_build_stanza (message,
      gabble_conn, 0, state, priv->peer_jid,
      priv->send_nick, &id, &error);

  if (stanza != NULL)
    {
      TpMessageSendingFlags supportedflags = 0;
      if (((flags & TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY) &&
          receipts_conceivably_supported (self)) || (priv->force_receipts))
        {
          wocky_node_add_child_ns (wocky_stanza_get_top_node (stanza),
              "request", NS_RECEIPTS);
          supportedflags |= TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY;
        }
      if (((flags & TP_MESSAGE_SENDING_FLAG_REPORT_READ) &&
          receipts_conceivably_supported (self)) || (priv->force_chat_markers))
        {
          wocky_node_add_child_ns (wocky_stanza_get_top_node (stanza),
              "markable", NS_CHAT_MARKERS);
          supportedflags |= TP_MESSAGE_SENDING_FLAG_REPORT_READ;
        }

      porter = gabble_connection_dup_porter (gabble_conn);
      context = g_slice_new0 (_GabbleIMSendMessageCtx);
      context->channel = g_object_ref (base);
      context->message = g_object_ref (message);
      context->token = id;
      context->flags = supportedflags;
      wocky_porter_send_async (porter, stanza, NULL,
          _gabble_im_channel_message_sent_cb, context);
      g_object_unref (porter);
      g_object_unref (stanza);
    }
  else
    {
      tp_message_mixin_sent (object, message, 0, NULL, error);
      g_error_free (error);
    }


  if (priv->send_nick)
    priv->send_nick = FALSE;
}

static TpMessage *
build_message (
    GabbleIMChannel *self,
    TpChannelTextMessageType type,
    time_t timestamp,
    const char *text)
{
  TpBaseChannel *base_chan = (TpBaseChannel *) self;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base_chan);
  TpMessage *msg = tp_cm_message_new (base_conn, 2);

  if (type != TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL)
    tp_message_set_uint32 (msg, 0, "message-type", type);

  if (timestamp != 0)
    tp_message_set_int64 (msg, 0, "message-sent", timestamp);

  /* Body */
  tp_message_set_string (msg, 1, "content-type", "text/plain");
  tp_message_set_string (msg, 1, "content", text);

  return msg;
}

static void
maybe_send_delivery_report (
    GabbleIMChannel *self,
    WockyStanza *message,
    const gchar *jid,
    const gchar *id)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpHandle target = tp_base_channel_get_target_handle (base);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  WockyStanza *report;

  if (id == NULL)
    return;

  if (wocky_node_get_child_ns (wocky_stanza_get_top_node (message),
          "request", NS_RECEIPTS) == NULL)
    return;

  if (conn->self_presence->status == GABBLE_PRESENCE_HIDDEN ||
      !gabble_roster_handle_gets_presence_from_us (conn->roster, target))
    return;

  report = wocky_stanza_build (
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      NULL, jid,
      '(', "received", ':', NS_RECEIPTS,
        '@', "id", id,
      ')', NULL);

  _gabble_connection_send (conn, report, NULL);
  g_object_unref (report);
}

/*
 * _gabble_im_channel_sent:
 * @chan: a channel
 * @type: the message type
 * @to: the full JID we sent the message to
 * @timestamp: the time at which the message was sent
 * @id: the id='' attribute from the <message/> stanza, if any
 * @text: the plaintext body of the message
 *
 * Shoves an outgoing message into @chan.
 */
void
_gabble_im_channel_sent (GabbleIMChannel *chan,
                         TpChannelTextMessageType type,
                         time_t timestamp,
                         const gchar *id,
                         const char *text)
{
  TpBaseChannel *base_chan;
  TpBaseConnection *base_conn;
  TpMessage *msg;

  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  base_chan = (TpBaseChannel *) chan;
  base_conn = tp_base_channel_get_connection (base_chan);

  msg = build_message (chan, type, timestamp, text);
  tp_cm_message_set_sender (msg, tp_base_connection_get_self_handle (base_conn));
  tp_message_set_int64 (msg, 0, "message-received", time (NULL));

  if (id != NULL)
    tp_message_set_string (msg, 0, "message-token", id);

  tp_message_mixin_take_received (G_OBJECT (chan), msg);
}

/*
 * _gabble_im_channel_receive:
 * @chan: a channel
 * @message: the <message> stanza, from which all the following arguments were
 *           extracted.
 * @type: the message type
 * @from: the full JID we received the message from
 * @timestamp: the time at which the message was sent (not the time it was
 *             received)
 * @id: the id='' attribute from the <message/> stanza, if any
 * @text: the plaintext body of the message
 * @state: a #TpChannelChatState, or -1 if there was no chat state in the
 *         message.
 *
 * Shoves an incoming message into @chan, possibly updating the chat state at
 * the same time.
 */
void
_gabble_im_channel_receive (GabbleIMChannel *chan,
                            WockyStanza *message,
                            TpChannelTextMessageType type,
                            const char *from,
                            time_t timestamp,
                            const gchar *id,
                            const char *text,
                            gint state)
{
  GabbleIMChannelPrivate *priv;
  TpBaseChannel *base_chan;
  TpHandle peer;
  TpMessage *msg;
  guint nid;

  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  priv = chan->priv;
  base_chan = (TpBaseChannel *) chan;
  peer = tp_base_channel_get_target_handle (base_chan);

  /* update peer's full JID if it's changed */
  if (tp_strdiff (from, priv->peer_jid))
    {
      g_free (priv->peer_jid);
      priv->peer_jid = g_strdup (from);
    }

  if (state == -1)
    priv->chat_states_supported = CHAT_STATES_NOT_SUPPORTED;
  else
    _gabble_im_channel_state_receive (chan, state);

  msg = build_message (chan, type, timestamp, text);
  tp_cm_message_set_sender (msg, peer);
  tp_message_set_int64 (msg, 0, "message-received", time (NULL));

  if (id != NULL)
    tp_message_set_string (msg, 0, "message-token", id);

  nid = tp_message_mixin_take_received (G_OBJECT (chan), msg);

  if ((id) && (priv->send_chat_markers))
    {
      DEBUG ("insert %d = %s", nid, id);
      g_hash_table_insert (priv->pending_messages, GINT_TO_POINTER (nid), g_strdup (id));
    }
  maybe_send_delivery_report (chan, message, from, id);
}

void
_gabble_im_channel_report_delivery (
    GabbleIMChannel *self,
    TpChannelTextMessageType type,
    time_t timestamp,
    const gchar *id,
    const char *text,
    TpChannelTextSendError send_error,
    TpDeliveryStatus delivery_status)
{
  GabbleIMChannelPrivate *priv;
  TpBaseChannel *base_chan = (TpBaseChannel *) self;
  TpBaseConnection *base_conn;
  TpHandle peer;
  TpMessage *delivery_report;
  gchar *tmp;

  g_return_if_fail (GABBLE_IS_IM_CHANNEL (self));
  priv = self->priv;
  peer = tp_base_channel_get_target_handle (base_chan);
  base_conn = tp_base_channel_get_connection (base_chan);

  if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      /* strip off the resource (if any), since we just failed to send to it */
      char *slash = strchr (priv->peer_jid, '/');

      if (slash != NULL)
        *slash = '\0';

      priv->chat_states_supported = CHAT_STATES_UNKNOWN;
    }

  delivery_report = tp_cm_message_new (base_conn, 1);
  tp_message_set_uint32 (delivery_report, 0, "message-type",
      TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT);
  tp_cm_message_set_sender (delivery_report, peer);
  tp_message_set_int64 (delivery_report, 0, "message-received",
      time (NULL));

  tmp = gabble_generate_id ();
  tp_message_set_string (delivery_report, 0, "message-token", tmp);
  g_free (tmp);

  tp_message_set_uint32 (delivery_report, 0, "delivery-status",
      delivery_status);
  tp_message_set_uint32 (delivery_report, 0, "delivery-error", send_error);

  if (id != NULL)
    tp_message_set_string (delivery_report, 0, "delivery-token", id);

  if (text != NULL)
    {
      TpMessage *msg = build_message (self, type, timestamp, text);
      /* This is a delivery report, so the original sender of the echoed message
       * must be us! */
      tp_cm_message_set_sender (msg, tp_base_connection_get_self_handle (base_conn));

      /* Since this is a delivery report, we can trust the id on the message. */
      if (id != NULL)
        tp_message_set_string (msg, 0, "message-token", id);

      tp_cm_message_take_message (delivery_report, 0, "delivery-echo", msg);
    }

  tp_message_mixin_take_received (G_OBJECT (self), delivery_report);
}

/**
 * _gabble_im_channel_state_receive
 *
 * Send the D-BUS signal ChatStateChanged
 * on org.freedesktop.Telepathy.Channel.Interface.ChatState
 */

void
_gabble_im_channel_state_receive (GabbleIMChannel *chan,
                                  TpChannelChatState state)
{
  GabbleIMChannelPrivate *priv;
  TpBaseChannel *base_chan;

  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  base_chan = (TpBaseChannel *) chan;
  priv = chan->priv;

  priv->chat_states_supported = CHAT_STATES_SUPPORTED;

  tp_message_mixin_change_chat_state ((GObject *) chan,
      tp_base_channel_get_target_handle (base_chan), state);
}

void
gabble_im_channel_receive_receipt (
    GabbleIMChannel *self,
    const gchar *receipt_id,
    TpDeliveryStatus status)
{
  _gabble_im_channel_report_delivery (self,
        TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL, 0, receipt_id, NULL,
        GABBLE_TEXT_CHANNEL_SEND_NO_ERROR, status);
}

static void
gabble_im_channel_close (TpBaseChannel *base_chan)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (base_chan);

  tp_message_mixin_maybe_send_gone ((GObject *) self);

  /* The IM factory will resurrect the channel if we have pending
   * messages. When we're resurrected, we want the initiator
   * to be the contact who sent us those messages, if it isn't already */
  if (tp_message_mixin_has_pending_messages ((GObject *) self, NULL))
    {
      DEBUG ("Not really closing, I still have pending messages");
      tp_message_mixin_set_rescued ((GObject *) self);
      tp_base_channel_reopened (base_chan,
          tp_base_channel_get_target_handle (base_chan));
    }
  else
    {
      DEBUG ("Actually closing, I have no pending messages");
      tp_base_channel_destroyed (base_chan);
    }
}

/**
 * gabble_im_channel_destroy
 *
 * Implements D-Bus method Destroy
 * on interface org.freedesktop.Telepathy.Channel.Interface.Destroyable
 */
static void
gabble_im_channel_destroy (TpSvcChannelInterfaceDestroyable *iface,
                           DBusGMethodInvocation *context)
{
  g_assert (GABBLE_IS_IM_CHANNEL (iface));

  DEBUG ("called on %p, clearing pending messages", iface);
  tp_message_mixin_clear ((GObject *) iface);
  gabble_im_channel_close (TP_BASE_CHANNEL (iface));

  tp_svc_channel_interface_destroyable_return_from_destroy (context);
}

static gboolean
_gabble_im_channel_send_chat_state (GObject *object,
    TpChannelChatState state,
    GError **error)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = self->priv;
  TpBaseChannel *base = (TpBaseChannel *) self;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

  /* Only send anything to the peer if we actually know they support chat
   * states. */
  if (!chat_states_supported (self, FALSE))
    return TRUE;

  return gabble_message_util_send_chat_state (G_OBJECT (self),
      GABBLE_CONNECTION (base_conn),
      WOCKY_STANZA_SUB_TYPE_CHAT, state, priv->peer_jid, error);
}

static void
destroyable_iface_init (gpointer g_iface,
                        gpointer iface_data)
{
  TpSvcChannelInterfaceDestroyableClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_destroyable_implement_##x (\
    klass, gabble_im_channel_##x)
  IMPLEMENT(destroy);
#undef IMPLEMENT
}
