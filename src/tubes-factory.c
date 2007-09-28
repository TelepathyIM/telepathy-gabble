/*
 * tubes-factory.c - Source for GabbleTubesFactory
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

/* FIXME: This file should probably be renamed to private-tubes-factory.c as
 * it's used for private tubes only */

#include "tubes-factory.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "debug.h"
#include "extensions/extensions.h"
#include "gabble-connection.h"
#include "tubes-channel.h"
#include "namespaces.h"
#include "util.h"
#include "muc-factory.h"
#include "gabble-muc-channel.h"
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-factory-iface.h>

static GabbleTubesChannel *new_tubes_channel (GabbleTubesFactory *fac,
    TpHandle handle);

static void tubes_channel_closed_cb (GabbleTubesChannel *chan,
    gpointer user_data);

static LmHandlerResult tubes_factory_msg_tube_cb (LmMessageHandler *handler,
    LmConnection *lmconn, LmMessage *msg, gpointer user_data);

static void gabble_tubes_factory_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleTubesFactory, gabble_tubes_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
        gabble_tubes_factory_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleTubesFactoryPrivate GabbleTubesFactoryPrivate;
struct _GabbleTubesFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *msg_tube_cb;

  GHashTable *channels;

  gboolean dispose_has_run;
};

#define GABBLE_TUBES_FACTORY_GET_PRIVATE(obj) \
    ((GabbleTubesFactoryPrivate *) obj->priv)

static void
gabble_tubes_factory_init (GabbleTubesFactory *self)
{
  GabbleTubesFactoryPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBES_FACTORY, GabbleTubesFactoryPrivate);

  self->priv = priv;

  priv->msg_tube_cb = NULL;
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                          NULL, g_object_unref);
  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

static GObject *
gabble_tubes_factory_constructor (GType type,
                                  guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleTubesFactory *self;
  GabbleTubesFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_tubes_factory_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_TUBES_FACTORY (obj);
  priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (self);

  priv->msg_tube_cb = lm_message_handler_new (tubes_factory_msg_tube_cb,
      self, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
      priv->msg_tube_cb, LM_MESSAGE_TYPE_MESSAGE, LM_HANDLER_PRIORITY_FIRST);

  return obj;
}

static void
gabble_tubes_factory_dispose (GObject *object)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (object);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->channels == NULL);

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->msg_tube_cb, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->msg_tube_cb);

  if (G_OBJECT_CLASS (gabble_tubes_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_tubes_factory_parent_class)->dispose (object);
}

static void
gabble_tubes_factory_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (object);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);

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
gabble_tubes_factory_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (object);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);

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
gabble_tubes_factory_class_init (
    GabbleTubesFactoryClass *gabble_tubes_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_tubes_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_tubes_factory_class,
      sizeof (GabbleTubesFactoryPrivate));

  object_class->constructor = gabble_tubes_factory_constructor;
  object_class->dispose = gabble_tubes_factory_dispose;

  object_class->get_property = gabble_tubes_factory_get_property;
  object_class->set_property = gabble_tubes_factory_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this Tubes channel factory object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}


/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when an Tubes channel is closed. Removes the references
 * that TubesFactory holds to them.
 */
static void
tubes_channel_closed_cb (GabbleTubesChannel *chan,
                         gpointer user_data)
{
  GabbleTubesFactory *conn = GABBLE_TUBES_FACTORY (user_data);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (conn);
  TpHandle contact_handle;

  if (priv->channels == NULL)
    return;

  g_object_get (chan, "handle", &contact_handle, NULL);

  DEBUG ("removing tubes channel with handle %d", contact_handle);

  g_hash_table_remove (priv->channels, GUINT_TO_POINTER (contact_handle));
}

/**
 * new_tubes_channel
 *
 * Creates the GabbleTubes object associated with the given parameters
 */
static GabbleTubesChannel *
new_tubes_channel (GabbleTubesFactory *fac,
                   TpHandle handle)
{
  GabbleTubesFactoryPrivate *priv;
  TpBaseConnection *conn;
  GabbleTubesChannel *chan;
  char *object_path;

  g_assert (GABBLE_IS_TUBES_FACTORY (fac));

  priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *) priv->conn;

  object_path = g_strdup_printf ("%s/SITubesChannel%u", conn->object_path,
      handle);

  chan = g_object_new (GABBLE_TYPE_TUBES_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "handle-type", TP_HANDLE_TYPE_CONTACT,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", G_CALLBACK (tubes_channel_closed_cb), fac);

  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  g_free (object_path);

  return chan;
}

static void
gabble_tubes_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (iface);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);
  GHashTable *tmp;

  DEBUG ("closing 1-1 tubes channels");

  if (priv->channels == NULL)
    return;

  tmp = priv->channels;
  priv->channels = NULL;
  g_hash_table_destroy (tmp);
}

static void
gabble_tubes_factory_iface_connecting (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
gabble_tubes_factory_iface_connected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
gabble_tubes_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

struct _ForeachData
{
  TpChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key,
                gpointer value,
                gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);

  data->foreach (chan, data->user_data);
}

static void
gabble_tubes_factory_iface_foreach (TpChannelFactoryIface *iface,
                                    TpChannelFunc foreach,
                                    gpointer user_data)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (iface);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
gabble_tubes_factory_iface_request (TpChannelFactoryIface *iface,
                                    const gchar *chan_type,
                                    TpHandleType handle_type,
                                    guint handle,
                                    gpointer request,
                                    TpChannelIface **ret,
                                    GError **error)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (iface);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *contacts_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubesChannel *chan;
  TpChannelFactoryRequestStatus status;

  if (tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_CONTACT)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!tp_handle_is_valid (contacts_repo, handle, NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  /* Don't support opening a channel to our self handle */
  if (handle == ((TpBaseConnection*) priv->conn)->self_handle)
    {
     g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
         "Can't open a channel to your self handle");
     return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;
    }

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));

  status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
  if (chan == NULL)
    {
      status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
      chan = new_tubes_channel (fac, handle);
      tp_channel_factory_iface_emit_new_channel (fac, (TpChannelIface *)chan,
          request);
    }

  g_assert (chan);
  *ret = TP_CHANNEL_IFACE (chan);
  return status;
}

void
gabble_tubes_factory_handle_si_tube_request (GabbleTubesFactory *self,
                                             GabbleBytestreamIface *bytestream,
                                             TpHandle handle,
                                             const gchar *stream_id,
                                             LmMessage *msg)
{
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubesChannel *chan;

  DEBUG ("contact#%u stream %s", handle, stream_id);
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));
  if (chan == NULL)
    {
      chan = new_tubes_channel (self, handle);
      tp_channel_factory_iface_emit_new_channel (self,
          (TpChannelIface *) chan, NULL);

      /* FIXME we should probably only emit the new channel signal only
       * if the call below returns TRUE and if not, close the channel.
       */
    }

  gabble_tubes_channel_tube_si_offered (chan, bytestream, msg);
}

void
gabble_tubes_factory_handle_si_stream_request (GabbleTubesFactory *self,
                                               GabbleBytestreamIface *bytestream,
                                               TpHandle handle,
                                               const gchar *stream_id,
                                               LmMessage *msg)
{
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (self);
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
tubes_factory_msg_tube_cb (LmMessageHandler *handler,
                           LmConnection *lmconn,
                           LmMessage *msg,
                           gpointer user_data)
{
  GabbleTubesFactory *self = GABBLE_TUBES_FACTORY (user_data);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *tube_node;
  GabbleTubesChannel *chan;
  const gchar *from;
  TpHandle handle;

  tube_node = lm_message_node_get_child_with_namespace (msg->node, "tube",
      NS_TUBES);

  if (tube_node == NULL)
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
      chan = new_tubes_channel (self, handle);
      tp_channel_factory_iface_emit_new_channel (self,
          (TpChannelIface *) chan, NULL);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

GabbleTubesFactory *
gabble_tubes_factory_new (GabbleConnection *conn)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  return g_object_new (
      GABBLE_TYPE_TUBES_FACTORY,
      "connection", conn,
      NULL);
}

static void
gabble_tubes_factory_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_tubes_factory_iface_close_all;
  klass->connecting = gabble_tubes_factory_iface_connecting;
  klass->connected = gabble_tubes_factory_iface_connected;
  klass->disconnected = gabble_tubes_factory_iface_disconnected;
  klass->foreach = gabble_tubes_factory_iface_foreach;
  klass->request = gabble_tubes_factory_iface_request;
}
