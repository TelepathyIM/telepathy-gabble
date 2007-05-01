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
#include "gabble-connection.h"
#include "tubes-channel.h"
#include "namespaces.h"
#include "util.h"
#include "muc-factory.h"
#include "gabble-muc-channel.h"
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/svc-unstable.h>

static GabbleTubesChannel *new_tubes_channel (GabbleTubesFactory *fac,
    TpHandle handle, TpHandleType handle_type);

static void tubes_channel_closed_cb (GabbleTubesChannel *chan,
    gpointer user_data);

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
  LmMessageHandler *presence_cb;

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

  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                          NULL, g_object_unref);
  priv->presence_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
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
 * tubes_factory_presence_cb
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
tubes_factory_presence_cb (LmMessageHandler *handler,
                           LmConnection *lmconn,
                           LmMessage *msg,
                           gpointer user_data)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (user_data);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  const gchar *from;
  gchar *room;
  LmMessageNode *tubes_node;
  TpHandle handle;
  GabbleTubesChannel *chan;

  g_assert (lmconn == priv->conn->lmconn);

  from = lm_message_node_get_attribute (msg->node, "from");

  if (from == NULL)
    {
      NODE_DEBUG (msg->node,
          "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  room = gabble_remove_resource (from);
  handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  g_free (room);

  if (handle == 0)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  tubes_node = lm_message_node_get_child_with_namespace (msg->node, "tubes",
      NS_TUBES);
  if (tubes_node == NULL)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      chan = new_tubes_channel (fac, handle, TP_HANDLE_TYPE_ROOM);
      tp_channel_factory_iface_emit_new_channel (fac, (TpChannelIface *) chan,
          NULL);
    }

  handle = tp_handle_ensure (contact_repo, from,
    GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
  gabble_tubes_channel_presence_updated (chan, handle, tubes_node);
  tp_handle_unref (contact_repo, handle);

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
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

  g_hash_table_remove (priv->channels, GINT_TO_POINTER (contact_handle));
}

/**
 * new_tubes_channel
 *
 * Creates the GabbleTubes object associated with the given parameters
 */
static GabbleTubesChannel *
new_tubes_channel (GabbleTubesFactory *fac,
                   TpHandle handle,
                   TpHandleType handle_type)
{
  GabbleTubesFactoryPrivate *priv;
  TpBaseConnection *conn;
  GabbleTubesChannel *chan;
  char *object_path;
  TpHandle self_handle;

  g_assert (GABBLE_IS_TUBES_FACTORY (fac));

  priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *) priv->conn;

  object_path = g_strdup_printf ("%s/TubesChannel%u_%u", conn->object_path,
      handle_type, handle);

  if (handle_type == TP_HANDLE_TYPE_ROOM)
    {
      GabbleMucChannel *channel;

      channel = gabble_muc_factory_find_channel (priv->conn->muc_factory,
          handle);
      /* XXX: requesting a tubes channel for a room we haven't joined */
      g_assert (channel);
      self_handle = channel->group.self_handle;
    }
  else
    {
      self_handle = conn->self_handle;
    }

  chan = g_object_new (GABBLE_TYPE_TUBES_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "handle-type", handle_type,
                       "self-handle", self_handle,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", G_CALLBACK (tubes_channel_closed_cb), fac);

  g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

  g_free (object_path);

  return chan;
}

static void
gabble_tubes_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (iface);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);
  GHashTable *tmp;

  DEBUG ("closing tubes channels");

  if (priv->channels == NULL)
    return;

  tmp = priv->channels;
  priv->channels = NULL;
  g_hash_table_destroy (tmp);
}

static void
gabble_tubes_factory_iface_connecting (TpChannelFactoryIface *iface)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (iface);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);

  DEBUG ("adding callbacks");

  g_assert (priv->presence_cb == NULL);

  priv->presence_cb = lm_message_handler_new (tubes_factory_presence_cb, fac,
      NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
                                          priv->presence_cb,
                                          LM_MESSAGE_TYPE_PRESENCE,
                                          LM_HANDLER_PRIORITY_NORMAL);
}

static void
gabble_tubes_factory_iface_connected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
gabble_tubes_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  GabbleTubesFactory *fac = GABBLE_TUBES_FACTORY (iface);
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (fac);

  DEBUG ("removing callbacks");

  g_assert (priv->presence_cb != NULL);

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->presence_cb, LM_MESSAGE_TYPE_PRESENCE);
  lm_message_handler_unref (priv->presence_cb);
  priv->presence_cb = NULL;

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
  TpHandleRepoIface *handles_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, handle_type);
  GabbleTubesChannel *chan;
  TpChannelFactoryRequestStatus status;

  if (tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_CONTACT &&
      handle_type != TP_HANDLE_TYPE_ROOM)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!tp_handle_is_valid (handles_repo, handle, NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

  status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
  if (chan == NULL)
    {
      status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
      chan = new_tubes_channel (fac, handle, handle_type);
      tp_channel_factory_iface_emit_new_channel (fac, (TpChannelIface *)chan,
          request);
    }

  g_assert (chan);
  *ret = TP_CHANNEL_IFACE (chan);
  return status;
}

void gabble_tubes_factory_handle_request (GabbleTubesFactory *self,
                                          GabbleBytestreamIBB *bytestream,
                                          TpHandle handle,
                                          const gchar *stream_id,
                                          LmMessage *msg)
{
  GabbleTubesFactoryPrivate *priv = GABBLE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubesChannel *chan;

  if (!tp_handle_is_valid (contact_repo, handle, NULL))
    return;

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      chan = new_tubes_channel (self, handle, TP_HANDLE_TYPE_CONTACT);
      tp_channel_factory_iface_emit_new_channel (self,
          (TpChannelIface *)chan, NULL);
    }

  gabble_tubes_channel_tube_offered (chan, bytestream, msg);
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
