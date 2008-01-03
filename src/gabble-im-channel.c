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

#include "gabble-im-channel.h"

#include <dbus/dbus-glib.h>
#include <loudmouth/loudmouth.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEBUG_FLAG GABBLE_DEBUG_IM

#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "presence.h"
#include "presence-cache.h"
#include "roster.h"
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>

static void channel_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);
static void chat_state_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleIMChannel, gabble_im_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE,
      chat_state_iface_init));


/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleIMChannelPrivate GabbleIMChannelPrivate;

struct _GabbleIMChannelPrivate
{
  GabbleConnection *conn;
  char *object_path;
  TpHandle handle;

  gchar *peer_jid;

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_IM_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleIMChannelPrivate *)obj->priv)

static void
gabble_im_channel_init (GabbleIMChannel *self)
{
  GabbleIMChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_IM_CHANNEL, GabbleIMChannelPrivate);

  self->priv = priv;
}

static GObject *
gabble_im_channel_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  GabbleIMChannelPrivate *priv;
  TpBaseConnection *conn;
  DBusGConnection *bus;
  gboolean send_nick;
  TpHandleRepoIface *contact_handles;

  obj = G_OBJECT_CLASS (gabble_im_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (GABBLE_IM_CHANNEL (obj));
  conn = (TpBaseConnection *)priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_handles, priv->handle);

  priv->peer_jid = g_strdup (tp_handle_inspect (contact_handles,
        priv->handle));

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  if (gabble_roster_handle_get_subscription (priv->conn->roster, priv->handle)
        & GABBLE_ROSTER_SUBSCRIPTION_FROM)
    send_nick = FALSE;
  else
    send_nick = TRUE;

  gabble_text_mixin_init (obj, G_STRUCT_OFFSET (GabbleIMChannel, text),
      contact_handles, send_nick);

  tp_text_mixin_set_message_types (obj,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
      G_MAXUINT);

  return obj;
}

static void
gabble_im_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_im_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      /* we don't ref it here because we don't necessarily have access to the
       * contact repo yet - instead we ref it in the constructor.
       */
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_HANDLE_TYPE:
    case PROP_CHANNEL_TYPE:
      /* these properties are writable in the interface, but not actually
       * meaningfully changeable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_im_channel_dispose (GObject *object);
static void gabble_im_channel_finalize (GObject *object);

static void
gabble_im_channel_class_init (GabbleIMChannelClass *gabble_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_im_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_im_channel_class,
      sizeof (GabbleIMChannelPrivate));

  object_class->constructor = gabble_im_channel_constructor;

  object_class->get_property = gabble_im_channel_get_property;
  object_class->set_property = gabble_im_channel_set_property;

  object_class->dispose = gabble_im_channel_dispose;
  object_class->finalize = gabble_im_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "IM channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  tp_text_mixin_class_init (object_class, G_STRUCT_OFFSET (GabbleIMChannelClass, text_class));
}

static void
gabble_im_channel_dispose (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);
  GabblePresence *presence;
  GabbleRosterSubscription subscription;
  gboolean cap_chat_states = FALSE;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  subscription = gabble_roster_handle_get_subscription (priv->conn->roster,
      priv->handle);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->handle);

  if (presence && presence->caps & PRESENCE_CAP_CHAT_STATES)
    {
      cap_chat_states = TRUE;
    }

  if ((GABBLE_ROSTER_SUBSCRIPTION_TO & subscription) == 0)
    {
      if (NULL != presence)
        {
          presence->keep_unavailable = FALSE;
          gabble_presence_cache_maybe_remove (priv->conn->presence_cache,
              priv->handle);
        }
    }

  if (!priv->closed)
      {
        if (cap_chat_states)
          {
          /* Set the chat state of the channel on gone
           * (Channel.Interface.ChatState) */
          gabble_text_mixin_send (G_OBJECT (self),
              TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, 0,
              TP_CHANNEL_CHAT_STATE_GONE, priv->peer_jid, NULL, priv->conn,
              FALSE /* emit_signal */, NULL);
          }

        tp_svc_channel_emit_closed (self);
      }

  if (G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose (object);
}

static void
gabble_im_channel_finalize (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection *)priv->conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  /* free any data held directly by the object here */

  DEBUG ("%p", object);

  tp_handle_unref (contact_handles, priv->handle);

  g_free (priv->object_path);
  g_free (priv->peer_jid);

  tp_text_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_im_channel_parent_class)->finalize (object);
}

/**
 * _gabble_im_channel_receive
 *
 */
void
_gabble_im_channel_receive (GabbleIMChannel *chan,
                            TpChannelTextMessageType type,
                            TpHandle sender,
                            const char *from,
                            time_t timestamp,
                            const char *text)
{
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  /* update peer's full JID if it's changed */
  if (0 != strcmp (from, priv->peer_jid))
    {
      g_free (priv->peer_jid);
      priv->peer_jid = g_strdup (from);
    }

  if (timestamp == 0)
      timestamp = time (NULL);

  tp_text_mixin_receive (G_OBJECT (chan), type, sender, timestamp, text);
}

/**
 * _gabble_im_channel_state_receive
 *
 * Send the D-BUS signal ChatStateChanged
 * on org.freedesktop.Telepathy.Channel.Interface.ChatState
 */

void
_gabble_im_channel_state_receive (GabbleIMChannel *chan,
                                  guint state)
{
  GabbleIMChannelPrivate *priv;

  g_assert (state < NUM_TP_CHANNEL_CHAT_STATES);
  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  tp_svc_channel_interface_chat_state_emit_chat_state_changed (
      (TpSvcChannelInterfaceChatState*)chan,
      priv->handle, state);
}

/**
 * gabble_im_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_im_channel_close (TpSvcChannel *iface,
                         DBusGMethodInvocation *context)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (iface);
  GabbleIMChannelPrivate *priv;
  GabblePresence *presence;

  g_assert (GABBLE_IS_IM_CHANNEL (self));

  DEBUG ("called on %p", self);

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->handle);

  if (!priv->closed)
    {
      tp_svc_channel_emit_closed (self);

      if (presence && (presence->caps & PRESENCE_CAP_CHAT_STATES))
        {
          /* Set the chat state of the channel on gone
           * (Channel.Interface.ChatState) */
          gabble_text_mixin_send (G_OBJECT (self),
              TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, 0,
              TP_CHANNEL_CHAT_STATE_GONE, priv->peer_jid, NULL, priv->conn,
              FALSE /* emit_signal */, NULL);
        }

      priv->closed = TRUE;
    }

  tp_svc_channel_return_from_close (context);
}


/**
 * gabble_im_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_im_channel_get_channel_type (TpSvcChannel *iface,
                                    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}


/**
 * gabble_im_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_im_channel_get_handle (TpSvcChannel *iface,
                              DBusGMethodInvocation *context)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (iface);
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (self));
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
      priv->handle);
}


/**
 * gabble_im_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_im_channel_get_interfaces (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  const char *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE,
      NULL };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


/**
 * gabble_im_channel_send
 *
 * Implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
gabble_im_channel_send (TpSvcChannelTypeText *iface,
                        guint type,
                        const gchar *text,
                        DBusGMethodInvocation *context)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (iface);
  GabbleIMChannelPrivate *priv;
  GabblePresence *presence;
  gint state = -1;
  GError *error = NULL;

  g_assert (GABBLE_IS_IM_CHANNEL (self));
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->handle);

  if (presence && (presence->caps & PRESENCE_CAP_CHAT_STATES))
    {
      state = TP_CHANNEL_CHAT_STATE_ACTIVE;
    }

  if (!gabble_text_mixin_send (G_OBJECT (self), type, 0,
      state, priv->peer_jid, text, priv->conn,
      TRUE /* emit_signal */, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  tp_svc_channel_type_text_return_from_send (context);
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
  GabbleIMChannelPrivate *priv;
  GabblePresence *presence;
  GError *error = NULL;

  g_assert (GABBLE_IS_IM_CHANNEL (self));
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->handle);

  if (presence && (presence->caps & PRESENCE_CAP_CHAT_STATES))
    {
      if (state >= NUM_TP_CHANNEL_CHAT_STATES)
        {
          DEBUG ("invalid state %u", state);

          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "invalid state: %u", state);
        }

      if (state == TP_CHANNEL_CHAT_STATE_GONE)
        {
          /* We cannot explicitly set the Gone state */
          DEBUG ("you may not explicitly set the Gone state");

          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "you may not explicitly set the Gone state");
        }

      if (error != NULL || !gabble_text_mixin_send (G_OBJECT (self),
          TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, 0, state, priv->peer_jid, NULL,
          priv->conn, FALSE /* emit_signal */, &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);

          return;
        }

      /* Send the ChatStateChanged signal for the local user */
      tp_svc_channel_interface_chat_state_emit_chat_state_changed (iface,
          priv->conn->parent.self_handle, state);
    }

  tp_svc_channel_interface_chat_state_return_from_set_chat_state (context);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_im_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void
text_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)g_iface;

  tp_text_mixin_iface_init (g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
    klass, gabble_im_channel_##x)
  IMPLEMENT(send);
#undef IMPLEMENT
}

static void
chat_state_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceChatStateClass *klass =
    (TpSvcChannelInterfaceChatStateClass *)g_iface;
#define IMPLEMENT(x) tp_svc_channel_interface_chat_state_implement_##x (\
    klass, gabble_im_channel_##x)
  IMPLEMENT(set_chat_state);
#undef IMPLEMENT
}
