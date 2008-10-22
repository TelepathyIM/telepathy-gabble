/*
 * private-tubes-factory.c - Source for GabblePrivateTubesFactory
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
#include "private-tubes-factory.h"

#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "connection.h"
#include "debug.h"
#include "muc-channel.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "tubes-channel.h"
#include "util.h"

static GabbleTubesChannel *new_tubes_channel (GabblePrivateTubesFactory *fac,
    TpHandle handle, TpHandle initiator, gpointer request_token);

static void tubes_channel_closed_cb (GabbleTubesChannel *chan,
    gpointer user_data);

static LmHandlerResult private_tubes_factory_msg_tube_cb (
    LmMessageHandler *handler, LmConnection *lmconn, LmMessage *msg,
    gpointer user_data);

static void gabble_private_tubes_factory_iface_init (gpointer g_iface,
    gpointer iface_data);
static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabblePrivateTubesFactory,
    gabble_private_tubes_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
        gabble_private_tubes_factory_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

struct _GabblePrivateTubesFactoryPrivate
{
  GabbleConnection *conn;
  gulong status_changed_id;
  LmMessageHandler *msg_tube_cb;

  GHashTable *channels;

  gboolean dispose_has_run;
};

#define GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE(obj) ((obj)->priv)

static void
gabble_private_tubes_factory_init (GabblePrivateTubesFactory *self)
{
  GabblePrivateTubesFactoryPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_PRIVATE_TUBES_FACTORY, GabblePrivateTubesFactoryPrivate);

  self->priv = priv;

  priv->msg_tube_cb = NULL;
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}


static void gabble_private_tubes_factory_close_all (
    GabblePrivateTubesFactory *fac);


static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabblePrivateTubesFactory *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_private_tubes_factory_close_all (self);
      break;
    }
}


static GObject *
gabble_private_tubes_factory_constructor (GType type,
                                          guint n_props,
                                          GObjectConstructParam *props)
{
  GObject *obj;
  GabblePrivateTubesFactory *self;
  GabblePrivateTubesFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_private_tubes_factory_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_PRIVATE_TUBES_FACTORY (obj);
  priv = GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);

  priv->msg_tube_cb = lm_message_handler_new (
      private_tubes_factory_msg_tube_cb, self, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
      priv->msg_tube_cb, LM_MESSAGE_TYPE_MESSAGE, LM_HANDLER_PRIORITY_FIRST);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

  return obj;
}


static void
gabble_private_tubes_factory_dispose (GObject *object)
{
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (object);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_private_tubes_factory_close_all (fac);
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_private_tubes_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_private_tubes_factory_parent_class)->dispose (
        object);
}

static void
gabble_private_tubes_factory_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (object);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_private_tubes_factory_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (object);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_private_tubes_factory_class_init (
    GabblePrivateTubesFactoryClass *gabble_private_tubes_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gabble_private_tubes_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_private_tubes_factory_class,
      sizeof (GabblePrivateTubesFactoryPrivate));

  object_class->constructor = gabble_private_tubes_factory_constructor;
  object_class->dispose = gabble_private_tubes_factory_dispose;

  object_class->get_property = gabble_private_tubes_factory_get_property;
  object_class->set_property = gabble_private_tubes_factory_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this Tubes channel factory object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}


/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when an Tubes channel is closed. Removes the references
 * that PrivateTubesFactory holds to them.
 */
static void
tubes_channel_closed_cb (GabbleTubesChannel *chan,
                         gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (user_data);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandle contact_handle;

  if (priv->channels == NULL)
    return;

  g_object_get (chan, "handle", &contact_handle, NULL);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (chan));

  DEBUG ("removing tubes channel with handle %d", contact_handle);

  g_hash_table_remove (priv->channels, GUINT_TO_POINTER (contact_handle));
}

/**
 * new_tubes_channel
 *
 * Creates the GabbleTubes object associated with the given parameters
 */
static GabbleTubesChannel *
new_tubes_channel (GabblePrivateTubesFactory *fac,
                   TpHandle handle,
                   TpHandle initiator,
                   gpointer request_token)
{
  GabblePrivateTubesFactoryPrivate *priv;
  TpBaseConnection *conn;
  GabbleTubesChannel *chan;
  char *object_path;
  GSList *request_tokens;

  g_assert (GABBLE_IS_PRIVATE_TUBES_FACTORY (fac));
  g_assert (handle != 0);
  g_assert (initiator != 0);

  priv = GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *) priv->conn;

  object_path = g_strdup_printf ("%s/SITubesChannel%u", conn->object_path,
      handle);

  chan = g_object_new (GABBLE_TYPE_TUBES_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "handle-type", TP_HANDLE_TYPE_CONTACT,
                       "initiator-handle", initiator,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", G_CALLBACK (tubes_channel_closed_cb), fac);

  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  tp_channel_factory_iface_emit_new_channel (fac, TP_CHANNEL_IFACE (chan),
      request_token);

  g_free (object_path);

  if (request_token != NULL)
    request_tokens = g_slist_prepend (NULL, request_token);
  else
    request_tokens = NULL;

  tp_channel_manager_emit_new_channel (fac,
      TP_EXPORTABLE_CHANNEL (chan), request_tokens);

  g_slist_free (request_tokens);

  return chan;
}

static void
gabble_private_tubes_factory_close_all (GabblePrivateTubesFactory *fac)
{
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  DEBUG ("closing 1-1 tubes channels");

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (priv->msg_tube_cb != NULL)
    {
      lm_connection_unregister_message_handler (priv->conn->lmconn,
        priv->msg_tube_cb, LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (priv->msg_tube_cb);
      priv->msg_tube_cb = NULL;
    }

  if (priv->channels != NULL)
    {
      GHashTable *tmp = priv->channels;

      priv->channels = NULL;
      g_hash_table_destroy (tmp);
    }
}

struct _ForeachData
{
  TpExportableChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key,
                gpointer value,
                gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

  g_assert (TP_IS_CHANNEL_IFACE (chan));

  data->foreach (chan, data->user_data);
}

static void
gabble_private_tubes_factory_foreach_channel (TpChannelManager *manager,
    TpExportableChannelFunc foreach,
    gpointer user_data)
{
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (manager);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
gabble_private_tubes_factory_iface_request (TpChannelFactoryIface *iface,
                                    const gchar *chan_type,
                                    TpHandleType handle_type,
                                    guint handle,
                                    gpointer request,
                                    TpChannelIface **ret,
                                    GError **error)
{
  return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
}

void
gabble_private_tubes_factory_handle_si_tube_request (
    GabblePrivateTubesFactory *self,
    GabbleBytestreamIface *bytestream,
    TpHandle handle,
    const gchar *stream_id,
    LmMessage *msg)
{
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubesChannel *chan;

  DEBUG ("contact#%u stream %s", handle, stream_id);
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));
  if (chan == NULL)
    {
      chan = new_tubes_channel (self, handle, handle, NULL);

      /* FIXME: Should we close the channel if the request is not properly
       * handled by the newly created channel ? */
    }

  gabble_tubes_channel_tube_si_offered (chan, bytestream, msg);
}

void
gabble_private_tubes_factory_handle_si_stream_request (
    GabblePrivateTubesFactory *self,
    GabbleBytestreamIface *bytestream,
    TpHandle handle,
    const gchar *stream_id,
    LmMessage *msg)
{
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubesChannel *chan;

  DEBUG ("contact#%u stream %s", handle, stream_id);
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));
  if (chan == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "No tubes channel available for this contact" };

      DEBUG ("tubes channel with contact %d doesn't exist", handle);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  gabble_tubes_channel_bytestream_offered (chan, bytestream, msg);
}

static LmHandlerResult
private_tubes_factory_msg_tube_cb (LmMessageHandler *handler,
                                   LmConnection *lmconn,
                                   LmMessage *msg,
                                   gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (user_data);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *tube_node, *close_node;
  GabbleTubesChannel *chan;
  const gchar *from;
  TpHandle handle;

  tube_node = lm_message_node_get_child_with_namespace (msg->node, "tube",
      NS_TUBES);
  close_node = lm_message_node_get_child_with_namespace (msg->node, "close",
      NS_TUBES);

  if (tube_node == NULL && close_node == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  from = lm_message_node_get_attribute (msg->node, "from");
  if (from == NULL)
    {
      NODE_DEBUG (msg->node, "got a message without a from field");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (handle == 0)
    {
      DEBUG ("Invalid from field");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  /* Tube offer */
  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      if (tube_node != NULL)
        {
          /* We create the tubes channel only if the message is a new tube
           * offer */
          chan = new_tubes_channel (self, handle, handle, NULL);
        }
      else
        {
          DEBUG ("Ignore tube close message as there is no tubes channel"
             " to handle it");
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  gabble_tubes_channel_tube_msg (chan, msg);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

GabblePrivateTubesFactory *
gabble_private_tubes_factory_new (GabbleConnection *conn)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  return g_object_new (
      GABBLE_TYPE_PRIVATE_TUBES_FACTORY,
      "connection", conn,
      NULL);
}

static void
gabble_private_tubes_factory_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all =
      (TpChannelFactoryIfaceProc) gabble_private_tubes_factory_close_all;
  klass->foreach = (TpChannelFactoryIfaceForeachImpl)
      gabble_private_tubes_factory_foreach_channel;
  klass->request = gabble_private_tubes_factory_iface_request;
}


static const gchar * const tubes_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const tubes_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};


static void
gabble_private_tubes_factory_foreach_channel_class (
    TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TUBES);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      value);

  func (manager, table, tubes_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}


static gboolean
gabble_private_tubes_factory_requestotron (GabblePrivateTubesFactory *self,
                                           gpointer request_token,
                                           GHashTable *request_properties,
                                           gboolean require_new)
{
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  GError *error = NULL;
  TpExportableChannel *channel;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"), TP_IFACE_CHANNEL_TYPE_TUBES))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  if (!tp_handle_is_valid (contact_repo, handle, &error))
    goto error;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          tubes_channel_fixed_properties, tubes_channel_allowed_properties,
          &error))
    goto error;

  /* Don't support opening a channel to our self handle */
  if (handle == base_conn->self_handle)
    {
     g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
         "Can't open a channel to your self handle");
     goto error;
    }

  channel = g_hash_table_lookup (self->priv->channels,
      GUINT_TO_POINTER (handle));

  if (channel == NULL)
    {
      new_tubes_channel (self, handle, base_conn->self_handle, request_token);
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
gabble_private_tubes_factory_create_channel (TpChannelManager *manager,
                                             gpointer request_token,
                                             GHashTable *request_properties)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);

  return gabble_private_tubes_factory_requestotron (self, request_token,
      request_properties, TRUE);
}


static gboolean
gabble_private_tubes_factory_request_channel (TpChannelManager *manager,
                                              gpointer request_token,
                                              GHashTable *request_properties)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);

  return gabble_private_tubes_factory_requestotron (self, request_token,
      request_properties, FALSE);
}


static gboolean
gabble_private_tubes_factory_ensure_channel (TpChannelManager *manager,
                                             gpointer request_token,
                                             GHashTable *request_properties)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);

  return gabble_private_tubes_factory_requestotron (self, request_token,
      request_properties, FALSE);
}


static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_private_tubes_factory_foreach_channel;
  iface->foreach_channel_class =
      gabble_private_tubes_factory_foreach_channel_class;
  iface->create_channel = gabble_private_tubes_factory_create_channel;
  iface->request_channel = gabble_private_tubes_factory_request_channel;
  iface->ensure_channel = gabble_private_tubes_factory_ensure_channel;
}
