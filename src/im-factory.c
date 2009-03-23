/*
 * im-factory.c - Source for GabbleImFactory
 * Copyright (C) 2006 Collabora Ltd.
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
#include "im-factory.h"

#define DBUS_API_SUBJECT_TO_CHANGE

#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_IM

#include "extensions/extensions.h"

#include "caps-channel-manager.h"
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "im-channel.h"
#include "message-util.h"

static void channel_manager_iface_init (gpointer, gpointer);
static void caps_channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleImFactory, gabble_im_factory, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      caps_channel_manager_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

struct _GabbleImFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *message_cb;
  GHashTable *channels;

  gulong status_changed_id;

  gboolean dispose_has_run;
};

#define GABBLE_IM_FACTORY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_IM_FACTORY,\
                                GabbleImFactoryPrivate))


static void
gabble_im_factory_init (GabbleImFactory *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_IM_FACTORY,
      GabbleImFactoryPrivate);

  self->priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                          NULL, g_object_unref);

  self->priv->message_cb = NULL;

  self->priv->conn = NULL;
  self->priv->dispose_has_run = FALSE;
}


static void connection_status_changed_cb (GabbleConnection *conn,
    guint status, guint reason, GabbleImFactory *self);


static GObject *
gabble_im_factory_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj = G_OBJECT_CLASS (gabble_im_factory_parent_class)->
           constructor (type, n_props, props);
  GabbleImFactory *self = GABBLE_IM_FACTORY (obj);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

  return obj;
}


static void gabble_im_factory_close_all (GabbleImFactory *);


static void
gabble_im_factory_dispose (GObject *object)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (object);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_im_factory_close_all (fac);
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_im_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_im_factory_parent_class)->dispose (object);
}

static void
gabble_im_factory_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (object);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

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
gabble_im_factory_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (object);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_im_factory_class_init (GabbleImFactoryClass *gabble_im_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_im_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_im_factory_class,
      sizeof (GabbleImFactoryPrivate));

  object_class->constructor = gabble_im_factory_constructor;
  object_class->dispose = gabble_im_factory_dispose;

  object_class->get_property = gabble_im_factory_get_property;
  object_class->set_property = gabble_im_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this IM channel manager object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static GabbleIMChannel *new_im_channel (GabbleImFactory *fac,
    TpHandle handle, TpHandle initiator, gpointer request_token);

static void im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data);


/**
 * im_factory_message_cb:
 *
 * Called by loudmouth when we get an incoming <message>.
 */
static LmHandlerResult
im_factory_message_cb (LmMessageHandler *handler,
                       LmConnection *lmconn,
                       LmMessage *message,
                       gpointer user_data)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (user_data);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  const gchar *from, *body, *id;
  time_t stamp;
  TpChannelTextMessageType msgtype;
  TpHandle handle;
  GabbleIMChannel *chan;
  gint state;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;

  if (!gabble_message_util_parse_incoming_message (message, &from, &stamp,
        &msgtype, &id, &body, &state, &send_error, &delivery_status))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (body == NULL && state == -1)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = tp_handle_ensure (contact_repo, from, NULL, NULL);
  if (handle == 0)
    {
      NODE_DEBUG (message->node, "ignoring message node from malformed jid");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
        {
          DEBUG ("ignoring message error; no sending channel");
          tp_handle_unref (contact_repo, handle);
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      if (body == NULL)
        {
          /* don't create a new channel if all we have is a chat state */
          DEBUG ("ignoring message without body; no existing channel");
          tp_handle_unref (contact_repo, handle);
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      DEBUG ("found no IM channel, creating one");

      chan = new_im_channel (fac, handle, handle, NULL);
    }

  g_assert (chan != NULL);

  /* now the channel is referencing the handle, so if we unref it, that's
   * not a problem */
  tp_handle_unref (contact_repo, handle);

  if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      if (body == NULL)
        {
          DEBUG ("ignoring error sending chat state to %s (handle %u)", from,
              handle);
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      DEBUG ("got error sending to %s (handle %u), msgtype %u, body:\n%s",
         from, handle, msgtype, body);
    }

  if (state != -1 && send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    _gabble_im_channel_state_receive (chan, state);

  if (body != NULL)
    _gabble_im_channel_receive (chan, msgtype, handle, from, stamp, id, body,
        send_error, delivery_status);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * im_channel_closed_cb:
 *
 * Signal callback for when an IM channel is closed. Removes the references
 * that #GabbleConnection holds to them - unless the channel has pending
 * messages, in which case it is re-announced (so from the perspective of the
 * D-Bus API, it was replaced by an identical channel).
 */
static void
im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data)
{
  GabbleImFactory *self = GABBLE_IM_FACTORY (user_data);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (self);
  TpHandle contact_handle;
  gboolean really_destroyed;

  DEBUG ("%p, channel %p", self, chan);

  tp_channel_manager_emit_channel_closed_for_object (self,
      (TpExportableChannel *) chan);

  if (priv->channels != NULL)
    {
      g_object_get (chan,
          "handle", &contact_handle,
          "channel-destroyed", &really_destroyed,
          NULL);

      if (really_destroyed)
        {
          DEBUG ("removing channel with handle %u", contact_handle);
          g_hash_table_remove (priv->channels,
              GUINT_TO_POINTER (contact_handle));
        }
      else
        {

          DEBUG ("reopening channel with handle %u due to pending messages",
              contact_handle);
          tp_channel_manager_emit_new_channel (self,
              (TpExportableChannel *) chan, NULL);
        }
    }
}

/**
 * new_im_channel
 */
static GabbleIMChannel *
new_im_channel (GabbleImFactory *fac,
                TpHandle handle,
                TpHandle initiator,
                gpointer request_token)
{
  GabbleImFactoryPrivate *priv;
  TpBaseConnection *conn;
  GabbleIMChannel *chan;
  char *object_path;
  GSList *request_tokens;

  g_return_val_if_fail (GABBLE_IS_IM_FACTORY (fac), NULL);
  g_return_val_if_fail (handle != 0, NULL);
  g_return_val_if_fail (initiator != 0, NULL);

  priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *) priv->conn;

  object_path = g_strdup_printf ("%s/ImChannel%u",
      conn->object_path, handle);

  chan = g_object_new (GABBLE_TYPE_IM_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "initiator-handle", initiator,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_free (object_path);

  g_signal_connect (chan, "closed", (GCallback) im_channel_closed_cb, fac);

  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  if (request_token != NULL)
    request_tokens = g_slist_prepend (NULL, request_token);
  else
    request_tokens = NULL;

  tp_channel_manager_emit_new_channel (fac,
      (TpExportableChannel *) chan, request_tokens);

  g_slist_free (request_tokens);

  return chan;
}

static void
gabble_im_factory_close_all (GabbleImFactory *self)
{
  /* Use a temporary variable because we don't want
   * im_channel_closed_cb to remove the channel from the hash table a
   * second time */
  if (self->priv->channels != NULL)
    {
      GHashTable *tmp = self->priv->channels;

      DEBUG ("closing channels");
      self->priv->channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (self->priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (self->priv->conn,
          self->priv->status_changed_id);
      self->priv->status_changed_id = 0;
    }

  if (self->priv->message_cb != NULL)
    {
      DEBUG ("removing callbacks");
      lm_connection_unregister_message_handler (self->priv->conn->lmconn,
          self->priv->message_cb, LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (self->priv->message_cb);
      self->priv->message_cb = NULL;
    }
}


static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleImFactory *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      DEBUG ("adding callbacks");
      g_assert (self->priv->message_cb == NULL);

      self->priv->message_cb = lm_message_handler_new (im_factory_message_cb,
          self, NULL);
      lm_connection_register_message_handler (self->priv->conn->lmconn,
          self->priv->message_cb, LM_MESSAGE_TYPE_MESSAGE,
          LM_HANDLER_PRIORITY_LAST);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_im_factory_close_all (self);
      break;
    }
}

static void
gabble_im_factory_get_contact_caps (GabbleCapsChannelManager *manager,
                                    GabbleConnection *conn,
                                    TpHandle handle,
                                    GPtrArray *arr)
{
  /* We don't need to check this contact's capabilities, we assume every
   * contact support text channels. */

  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;
  gchar *text_allowed_properties[] =
      {
        TP_IFACE_CHANNEL ".TargetHandle",
        NULL
      };

  g_assert (handle != 0);

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".TargetHandleType",
      target_handle_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, text_allowed_properties,
      G_MAXUINT);

  g_hash_table_destroy (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

struct _ForeachData
{
  TpExportableChannelFunc func;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
  struct _ForeachData *data = user_data;
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

  data->func (chan, data->user_data);
}

static void
gabble_im_factory_foreach_channel (TpChannelManager *manager,
                                   TpExportableChannelFunc func,
                                   gpointer user_data)
{
  GabbleImFactory *self = GABBLE_IM_FACTORY (manager);
  struct _ForeachData data;

  data.user_data = user_data;
  data.func = func;

  g_hash_table_foreach (self->priv->channels, _foreach_slave, &data);
}


static const gchar * const im_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const im_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};


static void
gabble_im_factory_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (table, (gchar *) im_channel_fixed_properties[0],
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, (gchar *) im_channel_fixed_properties[1],
      value);

  func (manager, table, im_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}


static gboolean
gabble_im_factory_requestotron (GabbleImFactory *self,
                                gpointer request_token,
                                GHashTable *request_properties,
                                gboolean require_new)
{
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->conn;
  TpHandle handle;
  GError *error = NULL;
  TpExportableChannel *channel;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"), TP_IFACE_CHANNEL_TYPE_TEXT))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  /* validity already checked by TpBaseConnection */
  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);
  g_assert (handle != 0);

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          im_channel_fixed_properties, im_channel_allowed_properties,
          &error))
    goto error;

  channel = g_hash_table_lookup (self->priv->channels,
      GUINT_TO_POINTER (handle));

  if (channel == NULL)
    {
      new_im_channel (self, handle, base_conn->self_handle, request_token);
      return TRUE;
    }

  if (require_new)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Already chatting with contact #%u in another channel", handle);
      goto error;
    }

  tp_channel_manager_emit_request_already_satisfied (self, request_token,
      channel);
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
gabble_im_factory_create_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  GabbleImFactory *self = GABBLE_IM_FACTORY (manager);

  return gabble_im_factory_requestotron (self, request_token,
      request_properties, TRUE);
}


static gboolean
gabble_im_factory_request_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  GabbleImFactory *self = GABBLE_IM_FACTORY (manager);

  return gabble_im_factory_requestotron (self, request_token,
      request_properties, FALSE);
}


static gboolean
gabble_im_factory_ensure_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  GabbleImFactory *self = GABBLE_IM_FACTORY (manager);

  return gabble_im_factory_requestotron (self, request_token,
      request_properties, FALSE);
}


static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_im_factory_foreach_channel;
  iface->foreach_channel_class = gabble_im_factory_foreach_channel_class;
  iface->create_channel = gabble_im_factory_create_channel;
  iface->request_channel = gabble_im_factory_request_channel;
  iface->ensure_channel = gabble_im_factory_ensure_channel;
}

static void
caps_channel_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  GabbleCapsChannelManagerIface *iface = g_iface;

  iface->get_contact_caps = gabble_im_factory_get_contact_caps;
}
