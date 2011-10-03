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
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

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

static void chat_state_iface_init (gpointer, gpointer);
static void destroyable_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleIMChannel, gabble_im_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE,
      chat_state_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE,
      destroyable_iface_init));

static void _gabble_im_channel_send_message (GObject *object,
    TpMessage *message, TpMessageSendingFlags flags);
static void gabble_im_channel_close (TpBaseChannel *base_chan);

static const gchar *gabble_im_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE,
    TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
    TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE,
    NULL
};


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

  /* FALSE unless at least one chat state notification has been sent; <gone/>
   * will only be sent when the channel closes if this is TRUE. This prevents
   * opening a channel and closing it immediately sending a spurious <gone/> to
   * the peer.
   */
  gboolean send_gone;

  gboolean dispose_has_run;
};

typedef struct {
  GabbleIMChannel *channel;
  TpMessage *message;
  gchar *token;
} _GabbleIMSendMessageCtx;

static void
gabble_im_channel_init (GabbleIMChannel *self)
{
  GabbleIMChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_IM_CHANNEL, GabbleIMChannelPrivate);

  self->priv = priv;
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

  priv->chat_states_supported = CHAT_STATES_UNKNOWN;

  tp_message_mixin_init (obj, G_STRUCT_OFFSET (GabbleIMChannel, message_mixin),
      base_conn);

  tp_message_mixin_implement_sending (obj, _gabble_im_channel_send_message,
      G_N_ELEMENTS (types), types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES,
      supported_content_types);
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
  base_class->interfaces = gabble_im_channel_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_class->close = gabble_im_channel_close;
  base_class->fill_immutable_properties =
    gabble_im_channel_fill_immutable_properties;

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

static void
im_channel_send_gone (GabbleIMChannel *self)
{
  GabbleIMChannelPrivate *priv = self->priv;
  TpBaseChannel *base = (TpBaseChannel *) self;

  if (priv->send_gone)
    {
      if (chat_states_supported (self, FALSE))
        gabble_message_util_send_chat_state (G_OBJECT (self),
            GABBLE_CONNECTION (tp_base_channel_get_connection (base)),
            LM_MESSAGE_SUB_TYPE_CHAT, TP_CHANNEL_CHAT_STATE_GONE,
            priv->peer_jid, NULL);

      priv->send_gone = FALSE;
    }
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

  im_channel_send_gone (self);

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
        tp_message_mixin_sent ((GObject *) chan, message, 0,
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
  GabbleConnection *gabble_conn;
  GabbleIMChannelPrivate *priv;
  gint state = -1;
  WockyStanza *stanza = NULL;
  gchar *id = NULL;
  GError *error = NULL;
  WockyPorter *porter;
  _GabbleIMSendMessageCtx *context;

  g_assert (GABBLE_IS_IM_CHANNEL (self));
  priv = self->priv;

  if (chat_states_supported (self, TRUE))
    {
      state = TP_CHANNEL_CHAT_STATE_ACTIVE;
      priv->send_gone = TRUE;
    }

  /* We don't support providing successful delivery reports. */
  flags = 0;
  gabble_conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));

  stanza = gabble_message_util_build_stanza (message,
      gabble_conn, 0, state, priv->peer_jid,
      priv->send_nick, &id, &error);


  if (stanza != NULL)
    {
      porter = gabble_connection_dup_porter (gabble_conn);
      context = g_slice_new0 (_GabbleIMSendMessageCtx);
      context->channel = g_object_ref (base);
      context->message = g_object_ref (message);
      context->token = id;
      wocky_porter_send_async (porter, stanza, NULL,
          _gabble_im_channel_message_sent_cb, context);
      g_object_unref (porter);
      g_object_unref (stanza);
    }
  else
    {
      tp_message_mixin_sent (object, message, flags, NULL, error);
      g_error_free (error);
    }


  if (priv->send_nick)
    priv->send_nick = FALSE;
}

/*
 * _gabble_im_channel_receive:
 * @chan: a channel
 * @type: the message type
 * @sender: the sender of the message (which may not be 0)
 * @from: the full JID we received the message from
 * @timestamp: the time at which the message was sent (not the time it was
 *             received)
 * @id: the id='' attribute from the <message/> stanza, if any
 * @text: the plaintext body of the message
 * @send_error: the reason why sending @text to @sender failed, or
 *              GABBLE_TEXT_CHANNEL_SEND_NO_ERROR if this call is not to report
 *              a failure to send.
 * @delivery_status: if @send_error is GABBLE_TEXT_CHANNEL_SEND_NO_ERROR,
 *                   ignored; else the delivery status to attach to the report.
 * @state: a #TpChannelChatState, or -1 if there was no chat state in the
 *         message.
 *
 * Shoves an incoming message into @chan, possibly updating the chat state at
 * the same time; or maybe this is a delivery report? Who knows! It's a magical
 * adventure.
 */
void
_gabble_im_channel_receive (GabbleIMChannel *chan,
                            TpChannelTextMessageType type,
                            TpHandle sender,
                            const char *from,
                            time_t timestamp,
                            const gchar *id,
                            const char *text,
                            TpChannelTextSendError send_error,
                            TpDeliveryStatus delivery_status,
                            gint state)
{
  GabbleIMChannelPrivate *priv;
  TpBaseChannel *base_chan;
  TpBaseConnection *base_conn;
  TpMessage *msg;
  gchar *tmp;

  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  priv = chan->priv;
  base_chan = (TpBaseChannel *) chan;
  base_conn = tp_base_channel_get_connection (base_chan);

  if (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      /* update peer's full JID if it's changed */
      if (tp_strdiff (from, priv->peer_jid))
        {
          g_free (priv->peer_jid);
          priv->peer_jid = g_strdup (from);
        }

      if (state == -1)
        {
          priv->chat_states_supported = CHAT_STATES_NOT_SUPPORTED;
        }
      else
        {
          _gabble_im_channel_state_receive (chan, state);
        }
    }
  else
    {
      /* strip off the resource (if any), since we just failed to send to it */
      char *slash = strchr (priv->peer_jid, '/');

      if (slash != NULL)
        *slash = '\0';

      priv->chat_states_supported = CHAT_STATES_UNKNOWN;
    }

  msg = tp_cm_message_new (base_conn, 2);

  /* Header */
  if (type != TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL)
    tp_message_set_uint32 (msg, 0, "message-type", type);

  if (timestamp != 0)
    tp_message_set_int64 (msg, 0, "message-sent", timestamp);

  /* Body */
  tp_message_set_string (msg, 1, "content-type", "text/plain");
  tp_message_set_string (msg, 1, "content", text);

  if (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      tp_cm_message_set_sender (msg, sender);
      tp_message_set_int64 (msg, 0, "message-received", time (NULL));

      if (id != NULL)
        tp_message_set_string (msg, 0, "message-token", id);

      tp_message_mixin_take_received (G_OBJECT (chan), msg);
    }
  else
    {
      TpMessage *delivery_report = tp_cm_message_new (base_conn, 1);

      tp_message_set_uint32 (delivery_report, 0, "message-type",
          TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT);
      tp_cm_message_set_sender (delivery_report, sender);
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

      /* We're getting a send error, so the original sender of the echoed
       * message must be us! */
      tp_cm_message_set_sender (msg, base_conn->self_handle);

      /* Since this is a send error, we can trust the id on the message. */
      if (id != NULL)
        tp_message_set_string (msg, 0, "message-token", id);

      tp_cm_message_take_message (delivery_report, 0, "delivery-echo", msg);

      tp_message_mixin_take_received (G_OBJECT (chan), delivery_report);
    }
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

  g_assert (state < NUM_TP_CHANNEL_CHAT_STATES);
  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  base_chan = (TpBaseChannel *) chan;
  priv = chan->priv;

  priv->chat_states_supported = CHAT_STATES_SUPPORTED;

  tp_svc_channel_interface_chat_state_emit_chat_state_changed (
      (TpSvcChannelInterfaceChatState *) chan,
      tp_base_channel_get_target_handle (base_chan), state);
}

static void
gabble_im_channel_close (TpBaseChannel *base_chan)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (base_chan);

  im_channel_send_gone (self);

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


/**
 * gabble_im_channel_set_chat_state
 *
 * Implements D-Bus method SetChatState
 * on interface org.freedesktop.Telepathy.Channel.Interface.ChatState
 */
static void
gabble_im_channel_set_chat_state (TpSvcChannelInterfaceChatState *iface,
                                  guint state,
                                  DBusGMethodInvocation *context)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (iface);
  TpBaseChannel *base = (TpBaseChannel *) self;
  GabbleIMChannelPrivate *priv;
  GError *error = NULL;

  g_assert (GABBLE_IS_IM_CHANNEL (self));
  priv = self->priv;

  if (state >= NUM_TP_CHANNEL_CHAT_STATES)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid state: %u", state);
    }
  else if (state == TP_CHANNEL_CHAT_STATE_GONE)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "you may not explicitly set the Gone state");
    }
  /* Only send anything to the peer if we actually know they support chat
   * states.
   */
  else if (chat_states_supported (self, FALSE))
    {
      TpBaseConnection *base_conn = tp_base_channel_get_connection (base);

      if (gabble_message_util_send_chat_state (G_OBJECT (self),
              GABBLE_CONNECTION (base_conn),
              LM_MESSAGE_SUB_TYPE_CHAT, state, priv->peer_jid, &error))
        {
          priv->send_gone = TRUE;

          /* Send the ChatStateChanged signal for the local user */
          tp_svc_channel_interface_chat_state_emit_chat_state_changed (iface,
              base_conn->self_handle, state);
        }
    }

  if (error != NULL)
    {
      DEBUG ("%s", error->message);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
  else
    {
      tp_svc_channel_interface_chat_state_return_from_set_chat_state (context);
    }
}

static void
chat_state_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceChatStateClass *klass =
    (TpSvcChannelInterfaceChatStateClass *) g_iface;
#define IMPLEMENT(x) tp_svc_channel_interface_chat_state_implement_##x (\
    klass, gabble_im_channel_##x)
  IMPLEMENT(set_chat_state);
#undef IMPLEMENT
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
