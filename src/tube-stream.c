/*
 * tube-stream.c - Source for GabbleTubeStream
 * Copyright (C) 2007-2008 Collabora Ltd.
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
#include "tube-stream.h"

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#include <glib/gstdio.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#include "extensions/extensions.h"

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include <gibber/gibber-fd-transport.h>
#include <gibber/gibber-listener.h>
#include <gibber/gibber-tcp-transport.h>
#include <gibber/gibber-transport.h>
#include <gibber/gibber-unix-transport.h>

#include "bytestream-factory.h"
#include "bytestream-iface.h"
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "gabble-signals-marshal.h"
#include "muc-channel.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "presence.h"
#include "tube-iface.h"
#include "util.h"

static void channel_iface_init (gpointer, gpointer);
static void tube_iface_init (gpointer g_iface, gpointer iface_data);
static void streamtube_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleTubeStream, gabble_tube_stream, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_TUBE_IFACE, tube_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_STREAM_TUBE,
      streamtube_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_INTERFACE_TUBE,
      NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_external_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

static const gchar *gabble_tube_stream_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    /* If more interfaces are added, either keep Group as the first, or change
     * the implementations of gabble_tube_stream_get_interfaces () and
     * gabble_tube_stream_get_property () too */
    GABBLE_IFACE_CHANNEL_INTERFACE_TUBE,
    NULL
};

/* Linux glibc bits/socket.h suggests that struct sockaddr_storage is
 * not guaranteed to be big enough for AF_UNIX addresses */
typedef union
{
  /* we'd call this unix, but gcc predefines that. Thanks, gcc */
  struct sockaddr_un un;
  struct sockaddr_in ipv4;
  struct sockaddr_in6 ipv6;
} SockAddr;

/* signals */
enum
{
  OPENED,
  NEW_CONNECTION,
  CLOSED,
  OFFERED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_CONNECTION,
  PROP_INTERFACES,
  PROP_HANDLE,
  PROP_HANDLE_TYPE,
  PROP_SELF_HANDLE,
  PROP_ID,
  PROP_TYPE,
  PROP_SERVICE,
  PROP_PARAMETERS,
  PROP_STATE,
  PROP_ADDRESS_TYPE,
  PROP_ADDRESS,
  PROP_ACCESS_CONTROL,
  PROP_ACCESS_CONTROL_PARAM,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  PROP_REQUESTED,
  PROP_TARGET_ID,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_SUPPORTED_SOCKET_TYPES,
  LAST_PROPERTY
};

struct _GabbleTubeStreamPrivate
{
  GabbleConnection *conn;
  char *object_path;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandle self_handle;
  guint id;

  /* Bytestreams for tubes. One tube can have several bytestreams. The
   * mapping between the tube bytestream and the transport to the local
   * application is stored in the transport_to_bytestream and
   * bytestream_to_transport fields. This is used both on initiator-side and
   * on recipient-side. */

  /* (GabbleBytestreamIface *) -> (GibberTransport *)
   *
   * The (b->t) is inserted as soon as they are created. On initiator side,
   * we receive an incoming bytestream, create a transport and insert (b->t).
   * On recipient side, we receive an incoming transport, create a bytestream
   * and insert (b->t).
   */
  GHashTable *bytestream_to_transport;

  /* (GibberTransport *) -> (GabbleBytestreamIface *)
   *
   * The (t->b) is inserted when the bytestream is open.
   */
  GHashTable *transport_to_bytestream;

  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  GabbleTubeChannelState state;

  TpSocketAddressType address_type;
  GValue *address;
  TpSocketAccessControl access_control;
  GValue *access_control_param;

  /* listen for connections from local applications */
  GibberListener *local_listener;

  gboolean closed;

  gboolean dispose_has_run;
};

#define GABBLE_TUBE_STREAM_GET_PRIVATE(obj) ((obj)->priv)

static void data_received_cb (GabbleBytestreamIface *ibb, TpHandle sender,
    GString *data, gpointer user_data);

static void
generate_ascii_string (guint len,
                       gchar *buf)
{
  const gchar *chars =
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "_-";
  guint i;

  for (i = 0; i < len; i++)
    buf[i] = chars[g_random_int_range (0, 64)];
}

static void
transport_handler (GibberTransport *transport,
                   GibberBuffer *data,
                   gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GabbleBytestreamIface *bytestream;

  bytestream = g_hash_table_lookup (priv->transport_to_bytestream, transport);
  if (bytestream == NULL)
    {
      DEBUG ("no open bytestream associated with this transport");
      return;
    }

  DEBUG ("read %" G_GSIZE_FORMAT " bytes from socket", data->length);

  gabble_bytestream_iface_send (bytestream, data->length,
      (const gchar *) data->data);
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GabbleBytestreamIface *bytestream;

  bytestream = g_hash_table_lookup (priv->transport_to_bytestream, transport);
  if (bytestream == NULL)
    return;

  DEBUG ("transport disconnected. close the extra bytestream");

  gabble_bytestream_iface_close (bytestream, NULL);
}

static void
remove_transport (GabbleTubeStream *self,
                  GabbleBytestreamIface *bytestream,
                  GibberTransport *transport)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  DEBUG ("disconnect and remove transport");
  g_signal_handlers_disconnect_matched (transport, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  gibber_transport_disconnect (transport);

  /* the transport may not be in transport_to_bytestream if the bytestream was
   * not fully open */
  g_hash_table_remove (priv->transport_to_bytestream, transport);

  g_hash_table_remove (priv->bytestream_to_transport, bytestream);
}

static void
transport_buffer_empty_cb (GibberTransport *transport,
                           GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GabbleBytestreamIface *bytestream;
  GabbleBytestreamState state;

  bytestream = g_hash_table_lookup (priv->transport_to_bytestream, transport);
  g_assert (bytestream != NULL);
  g_object_get (bytestream, "state", &state, NULL);

  if (state == GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      DEBUG ("buffer is now empty. Transport can be removed");
      remove_transport (self, bytestream, transport);
      return;
    }

  /* Buffer is empty so we can unblock the buffer if it was blocked */
  DEBUG ("tube buffer is empty. Unblock the bytestream");
  gabble_bytestream_iface_block_reading (bytestream, FALSE);
}

static void
add_transport (GabbleTubeStream *self,
               GibberTransport *transport,
               GabbleBytestreamIface *bytestream)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  gibber_transport_set_handler (transport, transport_handler, self);

  g_hash_table_insert (priv->transport_to_bytestream,
      g_object_ref (transport), g_object_ref (bytestream));

  g_signal_connect (transport, "disconnected",
      G_CALLBACK (transport_disconnected_cb), self);
  g_signal_connect (transport, "buffer-empty",
      G_CALLBACK (transport_buffer_empty_cb), self);

  /* We can transfer transport's data; unblock it. */
  gibber_transport_block_receiving (transport, FALSE);
}

static void
bytestream_write_blocked_cb (GabbleBytestreamIface *bytestream,
                             gboolean blocked,
                             GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GibberTransport *transport;

  transport = g_hash_table_lookup (priv->bytestream_to_transport,
      bytestream);
  g_assert (transport != NULL);

  if (blocked)
    {
      DEBUG ("bytestream blocked, stop to read data from the tube socket");
    }
  else
    {
      DEBUG ("bytestream unblocked, restart to read data from the tube socket");
    }

  gibber_transport_block_receiving (transport, blocked);
}

static void
extra_bytestream_state_changed_cb (GabbleBytestreamIface *bytestream,
                                   GabbleBytestreamState state,
                                   gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  DEBUG ("Called.");

  if (state == GABBLE_BYTESTREAM_STATE_OPEN)
    {
      GibberTransport *transport;

      DEBUG ("extra bytestream open");

      g_signal_connect (bytestream, "data-received",
          G_CALLBACK (data_received_cb), self);
      g_signal_connect (bytestream, "write-blocked",
          G_CALLBACK (bytestream_write_blocked_cb), self);

      transport = g_hash_table_lookup (priv->bytestream_to_transport,
            bytestream);
      g_assert (transport != NULL);

      add_transport (self, transport, bytestream);
    }
  else if (state == GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      GibberTransport *transport;

      DEBUG ("extra bytestream closed");
      transport = g_hash_table_lookup (priv->bytestream_to_transport,
          bytestream);
      if (transport != NULL)
        {
          if (gibber_transport_buffer_is_empty (transport))
            {
              DEBUG ("Buffer is empty, we can remove the transport");
              remove_transport (self, bytestream, transport);
            }
          else
            {
              DEBUG ("Wait buffer is empty before disconnect the transport");
            }
        }
    }
}

struct _extra_bytestream_negotiate_cb_data
{
  GabbleTubeStream *self;
  /* transport from the local application */
  GibberTransport *transport;
};

static void
extra_bytestream_negotiate_cb (GabbleBytestreamIface *bytestream,
                               const gchar *stream_id,
                               LmMessage *msg,
                               gpointer user_data)
{
  struct _extra_bytestream_negotiate_cb_data *data =
    (struct _extra_bytestream_negotiate_cb_data *) user_data;
  GabbleTubeStream *self = data->self;
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (bytestream == NULL)
    {
      DEBUG ("initiator refused new bytestream");

      g_object_unref (data->transport);
      g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
      return;
    }

  DEBUG ("extra bytestream accepted");

  g_hash_table_insert (priv->bytestream_to_transport, g_object_ref (bytestream),
      data->transport);

  g_signal_connect (bytestream, "state-changed",
                G_CALLBACK (extra_bytestream_state_changed_cb), self);

  g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
}

static gboolean
start_stream_initiation (GabbleTubeStream *self,
                         GibberTransport *transport,
                         GError **error)
{
  GabbleTubeStreamPrivate *priv;
  LmMessageNode *node, *si_node;
  LmMessage *msg;
  TpHandleRepoIface *contact_repo;
  const gchar *jid;
  gchar *full_jid, *stream_id, *id_str;
  gboolean result;
  struct _extra_bytestream_negotiate_cb_data *data;

  priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  contact_repo = tp_base_connection_get_handles (
     (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  jid = tp_handle_inspect (contact_repo, priv->initiator);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Private tube */
      GabblePresence *presence;
      const gchar *resource;

      presence = gabble_presence_cache_get (priv->conn->presence_cache,
          priv->initiator);
      if (presence == NULL)
        {
          DEBUG ("can't find initiator's presence");
          if (error != NULL)
            g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                "can't find initiator's presence");

          return FALSE;
        }

      resource = gabble_presence_pick_resource_by_caps (presence,
          PRESENCE_CAP_SI_TUBES);
      if (resource == NULL)
        {
          DEBUG ("initiator doesn't have tubes capabilities");
          if (error != NULL)
            g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                "initiator doesn't have tubes capabilities");

          return FALSE;
        }

        full_jid = g_strdup_printf ("%s/%s", jid, resource);
    }
  else
    {
      /* Muc tube */
      full_jid = g_strdup (jid);
    }

  stream_id = gabble_bytestream_factory_generate_stream_id ();

  msg = gabble_bytestream_factory_make_stream_init_iq (full_jid,
      stream_id, NS_TUBES);

  si_node = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  g_assert (si_node != NULL);

  id_str = g_strdup_printf ("%u", priv->id);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      node = lm_message_node_add_child (si_node, "stream", NULL);
    }
  else
    {
      node = lm_message_node_add_child (si_node, "muc-stream", NULL);
    }

  lm_message_node_set_attributes (node,
      "xmlns", NS_TUBES,
      "tube", id_str,
      NULL);

  data = g_slice_new (struct _extra_bytestream_negotiate_cb_data);
  data->self = self;
  data->transport = g_object_ref (transport);

  result = gabble_bytestream_factory_negotiate_stream (
    priv->conn->bytestream_factory,
    msg,
    stream_id,
    extra_bytestream_negotiate_cb,
    data,
    error);

  /* FIXME: data and one ref on data->transport are leaked if the tube is
   * closed before we got the SI reply. */

  if (!result)
    {
      g_object_unref (data->transport);
      g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
    }

  lm_message_unref (msg);
  g_free (stream_id);
  g_free (full_jid);
  g_free (id_str);

  return result;
}

/* callback for listening connections from the local application */
static void
local_new_connection_cb (GibberListener *listener,
                         GibberTransport *transport,
                         struct sockaddr_storage *addr,
                         guint size,
                         gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);

  /* Block the transport while there is no open bytestream to transfer
   * its data. */
  gibber_transport_block_receiving (transport, TRUE);

  /* Streams in stream tubes are established with stream initiation (XEP-0095).
   * We use SalutSiBytestreamManager.
   */
  if (!start_stream_initiation (self, transport, NULL))
    {
      DEBUG ("closing new client connection");
    }
}

static gboolean
new_connection_to_socket (GabbleTubeStream *self,
                          GabbleBytestreamIface *bytestream)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GibberTransport *transport;

  DEBUG ("Called.");

  g_assert (priv->initiator == priv->self_handle);

  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      array = g_value_get_boxed (priv->address);
      DEBUG ("Will try to connect to socket: %s", (const gchar *) array->data);

      transport = GIBBER_TRANSPORT (gibber_unix_transport_new ());
      gibber_unix_transport_connect (GIBBER_UNIX_TRANSPORT (transport),
          array->data, NULL);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4 ||
      priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      gchar *ip;
      gchar *port_str;
      guint port;

      dbus_g_type_struct_get (priv->address,
          0, &ip,
          1, &port,
          G_MAXUINT);
      port_str = g_strdup_printf ("%d", port);

      transport = GIBBER_TRANSPORT (gibber_tcp_transport_new ());
      gibber_tcp_transport_connect (GIBBER_TCP_TRANSPORT (transport), ip,
          port_str);

      g_free (ip);
      g_free (port_str);
    }
  else
    {
      g_assert_not_reached ();
    }

  /* Block the transport while there is no open bytestream to transfer
   * its data. */
  gibber_transport_block_receiving (transport, TRUE);

  g_hash_table_insert (priv->bytestream_to_transport, g_object_ref (bytestream),
      g_object_ref (transport));

  g_signal_connect (bytestream, "state-changed",
      G_CALLBACK (extra_bytestream_state_changed_cb), self);

  g_object_unref (transport);
  return TRUE;
}

static gboolean
tube_stream_open (GabbleTubeStream *self,
                  GError **error)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  DEBUG ("called");

  if (priv->initiator == priv->self_handle)
    /* Nothing to do if we are the initiator of this tube.
     * We'll connect to the socket each time request a new bytestream. */
    return TRUE;

  /* We didn't create this tube so it doesn't have
   * a socket associated with it. Let's create one */
  g_assert (priv->address == NULL);
  g_assert (priv->local_listener == NULL);
  priv->local_listener = gibber_listener_new ();

  g_signal_connect (priv->local_listener, "new-connection",
      G_CALLBACK (local_new_connection_cb), self);
  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      gchar suffix[8];
      gchar *path;
      int ret;

      generate_ascii_string (8, suffix);
      path = g_strdup_printf ("/tmp/stream-salut-%.8s", suffix);

      DEBUG ("create socket: %s", path);

      array = g_array_sized_new (TRUE, FALSE, sizeof (gchar), strlen (path));
      g_array_insert_vals (array, 0, path, strlen (path));

      priv->address = tp_g_value_slice_new (DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_set_boxed (priv->address, array);

      g_array_free (array, TRUE);

      ret = gibber_listener_listen_socket (priv->local_listener, path, FALSE,
          error);
      if (ret != TRUE)
        {
          g_assert (error != NULL && *error != NULL);
          DEBUG ("Error listening on socket %s: %s", path, (*error)->message);
          g_free (path);
          return FALSE;
        }
      g_free (path);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      int ret;

      ret = gibber_listener_listen_tcp_loopback_af (priv->local_listener, 0,
          GIBBER_AF_IPV4, error);
      if (!ret)
        {
          g_assert (error != NULL && *error != NULL);
          DEBUG ("Error listening on socket: %s", (*error)->message);
          return FALSE;
        }

      priv->address = tp_g_value_slice_new (
          TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4);
      g_value_take_boxed (priv->address,
          dbus_g_type_specialized_construct (
              TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4));

      dbus_g_type_struct_set (priv->address,
          0, "127.0.0.1",
          1, gibber_listener_get_port (priv->local_listener),
          G_MAXUINT);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      int ret;

      ret = gibber_listener_listen_tcp_loopback_af (priv->local_listener, 0,
          GIBBER_AF_IPV6, error);
      if (!ret)
        {
          g_assert (error != NULL && *error != NULL);
          DEBUG ("Error listening on socket: %s", (*error)->message);
          return FALSE;
        }

      priv->address = tp_g_value_slice_new (
          TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6);
      g_value_take_boxed (priv->address,
          dbus_g_type_specialized_construct (
            TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6));

      dbus_g_type_struct_set (priv->address,
          0, "::1",
          1, gibber_listener_get_port (priv->local_listener),
          G_MAXUINT);
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
}

static void
gabble_tube_stream_init (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBE_STREAM, GabbleTubeStreamPrivate);

  self->priv = priv;

  priv->transport_to_bytestream = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_object_unref,
      (GDestroyNotify) g_object_unref);

  priv->bytestream_to_transport = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_object_unref,
      (GDestroyNotify) g_object_unref);

  priv->address_type = TP_SOCKET_ADDRESS_TYPE_UNIX;
  priv->address = NULL;
  priv->access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  priv->access_control_param = NULL;
  priv->closed = FALSE;

  priv->dispose_has_run = FALSE;
}

static gboolean
close_each_extra_bytestream (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GibberTransport *transport = (GibberTransport *) value;
  GabbleBytestreamIface *bytestream = (GabbleBytestreamIface *) key;

  /* We are iterating over priv->fd_to_bytestreams so we can't modify it.
   * Disconnect signals so extra_bytestream_state_changed_cb won't be
   * called */
  g_signal_handlers_disconnect_matched (bytestream, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);
 g_signal_handlers_disconnect_matched (transport, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  gabble_bytestream_iface_close (bytestream, NULL);
  gibber_transport_disconnect (transport);

  g_hash_table_remove (priv->transport_to_bytestream, transport);

  return TRUE;
}

static void
gabble_tube_stream_dispose (GObject *object)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  gabble_tube_iface_close (GABBLE_TUBE_IFACE (self), TRUE);

  if (priv->initiator != priv->self_handle &&
      priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX &&
      priv->address != NULL)
    {
      /* We created a new UNIX socket. Let's delete it */
      GArray *array;
      GString *path;

      array = g_value_get_boxed (priv->address);
      path = g_string_new_len (array->data, array->len);

      if (g_unlink (path->str) != 0)
        {
          DEBUG ("unlink of %s failed: %s", path->str, g_strerror (errno));
        }

      g_string_free (path, TRUE);
    }

  if (priv->transport_to_bytestream != NULL)
    {
      g_hash_table_destroy (priv->transport_to_bytestream);
      priv->transport_to_bytestream = NULL;
    }

  if (priv->bytestream_to_transport != NULL)
    {
      g_hash_table_destroy (priv->bytestream_to_transport);
      priv->bytestream_to_transport = NULL;
    }

  tp_handle_unref (contact_repo, priv->initiator);

  if (priv->local_listener != NULL)
    {
      g_object_unref (priv->local_listener);
      priv->local_listener = NULL;
    }

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_tube_stream_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_tube_stream_parent_class)->dispose (object);
}

static void
gabble_tube_stream_finalize (GObject *object)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  g_free (priv->object_path);
  g_free (priv->service);
  g_hash_table_destroy (priv->parameters);

  if (priv->address != NULL)
    {
      tp_g_value_slice_free (priv->address);
      priv->address = NULL;
    }

  if (priv->access_control_param != NULL)
    {
      tp_g_value_slice_free (priv->access_control_param);
      priv->access_control_param = NULL;
    }

  G_OBJECT_CLASS (gabble_tube_stream_parent_class)->finalize (object);
}

static void
gabble_tube_stream_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value,
            GABBLE_IFACE_CHANNEL_TYPE_STREAM_TUBE);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_INTERFACES:
        if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
          {
            /* 1-1 tubes - omit the Group interface */
            g_value_set_boxed (value, gabble_tube_stream_interfaces + 1);
          }
        else
          {
            /* MUC tubes */
            g_value_set_boxed (value, gabble_tube_stream_interfaces);
          }
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, priv->handle_type);
        break;
      case PROP_SELF_HANDLE:
        g_value_set_uint (value, priv->self_handle);
        break;
      case PROP_ID:
        g_value_set_uint (value, priv->id);
        break;
      case PROP_TYPE:
        g_value_set_uint (value, TP_TUBE_TYPE_STREAM);
        break;
      case PROP_INITIATOR_HANDLE:
        g_value_set_uint (value, priv->initiator);
        break;
      case PROP_SERVICE:
        g_value_set_string (value, priv->service);
        break;
      case PROP_PARAMETERS:
        g_value_set_boxed (value, priv->parameters);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_ADDRESS_TYPE:
        g_value_set_uint (value, priv->address_type);
        break;
      case PROP_ADDRESS:
        g_value_set_pointer (value, priv->address);
        break;
      case PROP_ACCESS_CONTROL:
        g_value_set_uint (value, priv->access_control);
        break;
      case PROP_ACCESS_CONTROL_PARAM:
        g_value_set_pointer (value, priv->access_control_param);
        break;
      case PROP_CHANNEL_DESTROYED:
        g_value_set_boolean (value, priv->closed);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_take_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                TP_IFACE_CHANNEL, "Requested",
                TP_IFACE_CHANNEL, "Interfaces",
                GABBLE_IFACE_CHANNEL_TYPE_STREAM_TUBE, "Service",
                GABBLE_IFACE_CHANNEL_TYPE_STREAM_TUBE, "SupportedSocketTypes",
                NULL));
        break;
      case PROP_REQUESTED:
        g_value_set_boolean (value,
            (priv->initiator == priv->self_handle));
        break;
      case PROP_INITIATOR_ID:
          {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (
                base_conn, TP_HANDLE_TYPE_CONTACT);

            /* some channel can have o.f.T.Channel.InitiatorHandle == 0 but
             * tubes always have an initiator */
            g_assert (priv->initiator != 0);

            g_value_set_string (value,
                tp_handle_inspect (repo, priv->initiator));
          }
        break;
      case PROP_TARGET_ID:
          {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (
                base_conn, priv->handle_type);

            g_value_set_string (value,
                tp_handle_inspect (repo, priv->handle));
          }
        break;
      case PROP_SUPPORTED_SOCKET_TYPES:
        g_value_take_boxed (value,
            gabble_tube_stream_get_supported_socket_types ());
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_tube_stream_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_CHANNEL_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changeable on this channel, so we do nothing */
      break;
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_HANDLE_TYPE:
        priv->handle_type = g_value_get_uint (value);
        break;
      case PROP_SELF_HANDLE:
        priv->self_handle = g_value_get_uint (value);
        break;
      case PROP_ID:
        priv->id = g_value_get_uint (value);
        break;
      case PROP_INITIATOR_HANDLE:
        priv->initiator = g_value_get_uint (value);
        break;
      case PROP_SERVICE:
        g_free (priv->service);
        priv->service = g_value_dup_string (value);
        break;
      case PROP_PARAMETERS:
        if (priv->parameters != NULL)
          g_hash_table_destroy (priv->parameters);
        priv->parameters = g_value_dup_boxed (value);
        break;
      case PROP_ADDRESS_TYPE:
        g_assert (g_value_get_uint (value) == TP_SOCKET_ADDRESS_TYPE_UNIX ||
            g_value_get_uint (value) == TP_SOCKET_ADDRESS_TYPE_IPV4 ||
            g_value_get_uint (value) == TP_SOCKET_ADDRESS_TYPE_IPV6);
        priv->address_type = g_value_get_uint (value);
        break;
      case PROP_ADDRESS:
        if (priv->address == NULL)
          {
            priv->address = tp_g_value_slice_dup (g_value_get_pointer (value));
          }
        break;
      case PROP_ACCESS_CONTROL:
        priv->access_control = g_value_get_uint (value);
        break;
      case PROP_ACCESS_CONTROL_PARAM:
        if (priv->access_control_param == NULL)
          {
            priv->access_control_param = tp_g_value_slice_dup (
                g_value_get_pointer (value));
          }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_tube_stream_constructor (GType type,
                                guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  GabbleTubeStreamPrivate *priv;
  DBusGConnection *bus;
  TpHandleRepoIface *contact_repo;

  obj = G_OBJECT_CLASS (gabble_tube_stream_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_TUBE_STREAM_GET_PRIVATE (GABBLE_TUBE_STREAM (obj));

  /* Ref the initiator handle */
  g_assert (priv->conn != NULL);
  g_assert (priv->initiator != 0);
  contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  tp_handle_ref (contact_repo, priv->initiator);

  /* Set initial state of the tube */
  if (priv->initiator == priv->self_handle)
    {
      /* We initiated this tube */
      if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
        {
          /* Private tube */
          priv->state = GABBLE_TUBE_CHANNEL_STATE_NOT_OFFERED;
        }
      else
        {
          /* Muc tube */
          priv->state = GABBLE_TUBE_CHANNEL_STATE_OPEN;
        }
    }
  else
    {
      priv->state = GABBLE_TUBE_CHANNEL_STATE_LOCAL_PENDING;
    }

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  DEBUG ("Registering at '%s'", priv->object_path);

  return obj;
}

static gboolean
tube_iface_props_setter (GObject *object,
                         GQuark interface,
                         GQuark name,
                         const GValue *value,
                         gpointer setter_data,
                         GError **error)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  g_return_val_if_fail (interface == GABBLE_IFACE_QUARK_CHANNEL_INTERFACE_TUBE,
      FALSE);

  if (name != g_quark_from_static_string ("Parameters"))
    {
      g_object_set_property (object, setter_data, value);
      return TRUE;
    }

  if (priv->state != GABBLE_TUBE_CHANNEL_STATE_NOT_OFFERED)
  {
    g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "Can change parameters only if the tube is not offered");
    return FALSE;
  }

  g_object_set (self, "parameters", g_value_get_boxed (value), NULL);

  return TRUE;
}

static void
gabble_tube_stream_class_init (GabbleTubeStreamClass *gabble_tube_stream_class)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "ChannelType", "channel-type", NULL },
      { "TargetID", "target-id", NULL },
      { "Interfaces", "interfaces", NULL },
      { "Requested", "requested", NULL },
      { "InitiatorHandle", "initiator-handle", NULL },
      { "InitiatorID", "initiator-id", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl stream_tube_props[] = {
      { "Service", "service", NULL },
      { "SupportedSocketTypes", "supported-socket-types", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl tube_iface_props[] = {
      { "Parameters", "parameters", "parameters" },
      { "State", "state", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { GABBLE_IFACE_CHANNEL_TYPE_STREAM_TUBE,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_tube_props,
      },
      { GABBLE_IFACE_CHANNEL_INTERFACE_TUBE,
        tp_dbus_properties_mixin_getter_gobject_properties,
        tube_iface_props_setter,
        tube_iface_props,
      },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_tube_stream_class);
  GParamSpec *param_spec;

  object_class->get_property = gabble_tube_stream_get_property;
  object_class->set_property = gabble_tube_stream_set_property;
  object_class->constructor = gabble_tube_stream_constructor;

  g_type_class_add_private (gabble_tube_stream_class,
      sizeof (GabbleTubeStreamPrivate));

  object_class->dispose = gabble_tube_stream_dispose;
  object_class->finalize = gabble_tube_stream_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_CONNECTION,
      "connection");
  g_object_class_override_property (object_class, PROP_HANDLE,
      "handle");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_SELF_HANDLE,
      "self-handle");
  g_object_class_override_property (object_class, PROP_ID,
      "id");
  g_object_class_override_property (object_class, PROP_TYPE,
      "type");
  g_object_class_override_property (object_class, PROP_SERVICE,
      "service");
  g_object_class_override_property (object_class, PROP_PARAMETERS,
      "parameters");
  g_object_class_override_property (object_class, PROP_STATE,
      "state");

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_boxed (
      "supported-socket-types",
      "Supported socket types",
      "GHashTable containing supported socket types.",
      dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, DBUS_TYPE_G_UINT_ARRAY),
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SUPPORTED_SOCKET_TYPES,
      param_spec);

  param_spec = g_param_spec_uint (
      "address-type",
      "address type",
      "a TpSocketAddressType representing the type of the listening"
      "address of the local service",
      0, NUM_TP_SOCKET_ADDRESS_TYPES - 1,
      TP_SOCKET_ADDRESS_TYPE_UNIX,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ADDRESS_TYPE,
      param_spec);

  param_spec = g_param_spec_pointer (
      "address",
      "address",
      "The listening address of the local service, as indicated by the "
      "address-type",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ADDRESS, param_spec);

  param_spec = g_param_spec_uint (
      "access-control",
      "access control",
      "a TpSocketAccessControl representing the access control "
      "the local service applies to the local socket",
      0, NUM_TP_SOCKET_ACCESS_CONTROLS - 1,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL,
      param_spec);

  param_spec = g_param_spec_pointer (
      "access-control-param",
      "access control param",
      "A parameter for the access control type, to be interpreted as specified"
      "in the documentation for the Socket_Access_Control enum.",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL_PARAM,
      param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "The string obtained by inspecting the target handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "The contact who initiated the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator's bare JID",
      "The string obtained by inspecting the initiator-handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  signals[OPENED] =
    g_signal_new ("tube-opened",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_CONNECTION] =
    g_signal_new ("tube-new-connection",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("tube-closed",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[OFFERED] =
    g_signal_new ("tube-offered",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  gabble_tube_stream_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleTubeStreamClass, dbus_props_class));

  tp_external_group_mixin_init_dbus_properties (object_class);
}

static void
data_received_cb (GabbleBytestreamIface *bytestream,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  GabbleTubeStream *tube = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (tube);
  GibberTransport *transport;
  GError *error = NULL;

  DEBUG ("received %" G_GSIZE_FORMAT " bytes from bytestream", data->len);

  transport = g_hash_table_lookup (priv->bytestream_to_transport, bytestream);
  g_assert (transport != NULL);

  /* If something goes wrong when trying to write the data on the transport,
   * it could be disconnected, causing its removal from the hash tables.
   * When removed, the transport would be destroyed as the hash tables keep a
   * ref on it and so we'll call _buffer_is_empty on a destroyed transport.
   * We avoid that by reffing the transport between the 2 calls so we keep it
   * artificially alive if needed. */
  g_object_ref (transport);
  if (!gibber_transport_send (transport, (const guint8 *) data->str, data->len,
      &error))
  {
    DEBUG ("sending failed: %s", error->message);
    g_error_free (error);
    g_object_unref (transport);
    return;
  }

  if (!gibber_transport_buffer_is_empty (transport))
    {
      /* We don't want to send more data while the buffer isn't empty */
      DEBUG ("tube buffer isn't empty. Block the bytestream");
      gabble_bytestream_iface_block_reading (bytestream, TRUE);
    }
  g_object_unref (transport);
}

GabbleTubeStream *
gabble_tube_stream_new (GabbleConnection *conn,
                        TpHandle handle,
                        TpHandleType handle_type,
                        TpHandle self_handle,
                        TpHandle initiator,
                        const gchar *service,
                        GHashTable *parameters,
                        guint id)
{
  GabbleTubeStream *obj;
  char *object_path;

  object_path = g_strdup_printf ("%s/StreamTubeChannel_%u_%u",
      conn->parent.object_path, handle, id);

  obj = g_object_new (GABBLE_TYPE_TUBE_STREAM,
      "connection", conn,
      "object-path", object_path,
      "handle", handle,
      "handle-type", handle_type,
      "self-handle", self_handle,
      "initiator-handle", initiator,
      "service", service,
      "parameters", parameters,
      "id", id,
      NULL);

  g_free (object_path);
  return obj;
}

/**
 * gabble_tube_stream_accept
 *
 * Implements gabble_tube_iface_accept on GabbleTubeIface
 */
static gboolean
gabble_tube_stream_accept (GabbleTubeIface *tube,
                           GError **error)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (!gabble_tube_stream_check_params (priv->address_type, NULL,
        priv->access_control, priv->access_control_param, error))
    {
      goto fail;
    }

  if (priv->state != GABBLE_TUBE_CHANNEL_STATE_LOCAL_PENDING)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the local pending state");
      goto fail;
    }

  if (!tube_stream_open (self, error))
    {
      gabble_tube_iface_close (GABBLE_TUBE_IFACE (self), TRUE);
      goto fail;
    }

  priv->state = GABBLE_TUBE_CHANNEL_STATE_OPEN;

  gabble_svc_channel_interface_tube_emit_tube_channel_state_changed (self,
      GABBLE_TUBE_CHANNEL_STATE_OPEN);

  g_signal_emit (G_OBJECT (self), signals[OPENED], 0);

  return TRUE;

fail:
  priv->address_type = 0;
  priv->access_control = 0;
  tp_g_value_slice_free (priv->access_control_param);
  priv->access_control_param = NULL;
  return FALSE;
}

/**
 * gabble_tube_stream_close
 *
 * Implements gabble_tube_iface_close on GabbleTubeIface
 */
static void
gabble_tube_stream_close (GabbleTubeIface *tube, gboolean closed_remotely)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (priv->closed)
    return;
  priv->closed = TRUE;

  g_hash_table_foreach_remove (priv->bytestream_to_transport,
      close_each_extra_bytestream, self);

  if (!closed_remotely && priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      LmMessage *msg;
      const gchar *jid;
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
      gchar *id_str;

      jid = tp_handle_inspect (contact_repo, priv->handle);
      id_str = g_strdup_printf ("%u", priv->id);

      /* Send the close message */
      msg = lm_message_build (jid, LM_MESSAGE_TYPE_MESSAGE,
          '(', "close", "",
            '@', "xmlns", NS_TUBES,
            '@', "tube", id_str,
          ')',
          '(', "amp", "",
            '@', "xmlns", NS_AMP,
            '(', "rule", "",
              '@', "condition", "deliver-at",
              '@', "value", "stored",
              '@', "action", "error",
            ')',
            '(', "rule", "",
              '@', "condition", "match-resource",
              '@', "value", "exact",
              '@', "action", "error",
            ')',
          ')',
          NULL);
      g_free (id_str);

      _gabble_connection_send (priv->conn, msg, NULL);

      lm_message_unref (msg);
    }

  g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
}

static void
augment_si_accept_iq (LmMessageNode *si,
                      gpointer user_data)
{
  LmMessageNode *tube_node;

  tube_node = lm_message_node_add_child (si, "tube", "");
  lm_message_node_set_attribute (tube_node, "xmlns", NS_TUBES);
}

/**
 * gabble_tube_stream_add_bytestream
 *
 * Implements gabble_tube_iface_add_bytestream on GabbleTubeIface
 */

static void
gabble_tube_stream_add_bytestream (GabbleTubeIface *tube,
                                   GabbleBytestreamIface *bytestream)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (priv->initiator != priv->self_handle)
    {
      DEBUG ("I'm not the initiator of this tube, can't accept "
          "an extra bytestream");

      gabble_bytestream_iface_close (bytestream, NULL);
      return;
    }

  /* New bytestream, let's connect to the socket */
  if (new_connection_to_socket (self, bytestream))
    {
      TpHandle contact;

      if (priv->state == GABBLE_TUBE_CHANNEL_STATE_REMOTE_PENDING)
        {
          DEBUG ("Received first connection. Tube is now open");
          priv->state = GABBLE_TUBE_CHANNEL_STATE_OPEN;

          gabble_svc_channel_interface_tube_emit_tube_channel_state_changed (
              self, GABBLE_TUBE_CHANNEL_STATE_OPEN);

          g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
        }

      DEBUG ("accept the extra bytestream");

      gabble_bytestream_iface_accept (bytestream, augment_si_accept_iq, self);

      g_object_get (bytestream, "peer-handle", &contact, NULL);

      g_signal_emit (G_OBJECT (self), signals[NEW_CONNECTION], 0, contact);
    }
  else
    {
      gabble_bytestream_iface_close (bytestream, NULL);
    }
}

static gboolean
check_unix_params (TpSocketAddressType address_type,
                   const GValue *address,
                   TpSocketAccessControl access_control,
                   const GValue *access_control_param,
                   GError **error)
{
  GArray *array;
  GString *socket_address;
  struct stat stat_buff;
  guint i;
  struct sockaddr_un dummy;

  g_assert (address_type == TP_SOCKET_ADDRESS_TYPE_UNIX);

  /* Check address type */
  if (address != NULL)
    {
      if (G_VALUE_TYPE (address) != DBUS_TYPE_G_UCHAR_ARRAY)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Unix socket address is supposed to be ay");
          return FALSE;
        }

      array = g_value_get_boxed (address);

      if (array->len > sizeof (dummy.sun_path) - 1)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Unix socket path is too long (max length allowed: %"
              G_GSIZE_FORMAT ")",
              sizeof (dummy.sun_path) - 1);
          return FALSE;
        }

      for (i = 0; i < array->len; i++)
        {
          if (g_array_index (array, gchar , i) == '\0')
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Unix socket path can't contain zero bytes");
              return FALSE;
            }
        }

      socket_address = g_string_new_len (array->data, array->len);

      if (g_stat (socket_address->str, &stat_buff) == -1)
      {
        DEBUG ("Error calling stat on socket: %s", g_strerror (errno));

        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "%s: %s",
            socket_address->str, g_strerror (errno));
        g_string_free (socket_address, TRUE);
        return FALSE;
      }

      if (!S_ISSOCK (stat_buff.st_mode))
      {
        DEBUG ("%s is not a socket", socket_address->str);

        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "%s is not a socket", socket_address->str);
        g_string_free (socket_address, TRUE);
        return FALSE;
      }

      g_string_free (socket_address, TRUE);
    }

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
  {
    g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "Only the Localhost access control method is supported for Unix"
        " sockets");
    return FALSE;
  }

  return TRUE;
}

static gboolean
check_ip_params (TpSocketAddressType address_type,
                 const GValue *address,
                 TpSocketAccessControl access_control,
                 const GValue *access_control_param,
                 GError **error)
{
  /* Check address type */
  if (address != NULL)
    {
      gchar *ip;
      guint port;
      struct addrinfo req, *result = NULL;
      int ret;

      if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
        {
          if (G_VALUE_TYPE (address) != TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4)
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "IPv4 socket address is supposed to be sq");
              return FALSE;
            }
        }
      else if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
        {
          if (G_VALUE_TYPE (address) != TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6)
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "IPv6 socket address is supposed to be sq");
              return FALSE;
            }
        }
      else
        {
          g_return_val_if_reached (FALSE);
        }

      dbus_g_type_struct_get (address,
          0, &ip,
          1, &port,
          G_MAXUINT);

      memset (&req, 0, sizeof (req));
      req.ai_flags = AI_NUMERICHOST;
      req.ai_socktype = SOCK_STREAM;
      req.ai_protocol = IPPROTO_TCP;

      if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
        req.ai_family = AF_INET;
      else
        req.ai_family = AF_INET6;

      ret = getaddrinfo (ip, NULL, &req, &result);
      if (ret != 0)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Invalid address: %s", gai_strerror (ret));
          g_free (ip);
          return FALSE;
        }

      g_free (ip);
      freeaddrinfo (result);
    }

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "%s sockets only support localhost control access",
          (address_type == TP_SOCKET_ADDRESS_TYPE_IPV4 ? "IPv4" : "IPv6"));
      return FALSE;
    }

  return TRUE;
}

/* used to check access control parameters both for OfferStreamTube and
 * AcceptStreamTube. In case of AcceptStreamTube, address is NULL because we
 * listen on the socket after the parameters have been accepted
 */
gboolean
gabble_tube_stream_check_params (TpSocketAddressType address_type,
                                 const GValue *address,
                                 TpSocketAccessControl access_control,
                                 const GValue *access_control_param,
                                 GError **error)
{
  switch (address_type)
    {
      case TP_SOCKET_ADDRESS_TYPE_UNIX:
        return check_unix_params (address_type, address, access_control,
            access_control_param, error);

      case TP_SOCKET_ADDRESS_TYPE_IPV4:
      case TP_SOCKET_ADDRESS_TYPE_IPV6:
        return check_ip_params (address_type, address, access_control,
            access_control_param, error);

      default:
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
            "Address type %d not implemented", address_type);
        return FALSE;
    }
}

/* can be called both from the old tube API and the new tube API */
gboolean
gabble_tube_stream_offer (GabbleTubeStream *self,
                          guint address_type,
                          const GValue *address, guint access_control,
                          const GValue *access_control_param,
                          GError **error)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  LmMessageNode *tube_node = NULL;
  LmMessage *msg;
  TpHandleRepoIface *contact_repo;
  const gchar *jid;
  gboolean result;
  GabblePresence *presence;
  const gchar *resource;
  gchar *full_jid;

  g_assert (priv->state == GABBLE_TUBE_CHANNEL_STATE_NOT_OFFERED);

  contact_repo = tp_base_connection_get_handles (
     (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  jid = tp_handle_inspect (contact_repo, priv->handle);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->handle);
  if (presence == NULL)
    {
      DEBUG ("can't find tube recipient's presence");
      if (error != NULL)
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "can't find tube recipient's presence");

      return FALSE;
    }

  resource = gabble_presence_pick_resource_by_caps (presence,
      PRESENCE_CAP_SI_TUBES);
  if (resource == NULL)
    {
      DEBUG ("tube recipient doesn't have tubes capabilities");
      if (error != NULL)
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "tube recipient doesn't have tubes capabilities");

      return FALSE;
    }

  full_jid = g_strdup_printf ("%s/%s", jid, resource);

  msg = lm_message_build (full_jid, LM_MESSAGE_TYPE_MESSAGE,
      '(', "tube", "",
        '*', &tube_node,
        '@', "xmlns", NS_TUBES,
      ')',
      '(', "amp", "",
        '@', "xmlns", NS_AMP,
        '(', "rule", "",
          '@', "condition", "deliver-at",
          '@', "value", "stored",
          '@', "action", "error",
        ')',
        '(', "rule", "",
          '@', "condition", "match-resource",
          '@', "value", "exact",
          '@', "action", "error",
        ')',
      ')',
      NULL);
  g_free (full_jid);

  g_assert (tube_node != NULL);

  gabble_tube_iface_publish_in_node (GABBLE_TUBE_IFACE (self),
      (TpBaseConnection *) priv->conn, tube_node);

  result = _gabble_connection_send (priv->conn, msg, error);
  if (result)
    {
      priv->state = GABBLE_TUBE_CHANNEL_STATE_REMOTE_PENDING;
    }

  lm_message_unref (msg);
  return result;
}

static void
destroy_socket_control_list (gpointer data)
{
  GArray *tab = data;
  g_array_free (tab, TRUE);
}

/**
 * gabble_tube_stream_get_supported_socket_types
 *
 * Used to implement D-Bus property
 * org.freedesktop.Telepathy.Channel.Type.StreamTube.SupportedSocketTypes
 * and D-Bus method GetAvailableStreamTubeTypes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
GHashTable *
gabble_tube_stream_get_supported_socket_types (void)
{
  GHashTable *ret;
  GArray *unix_tab, *ipv4_tab, *ipv6_tab;
  TpSocketAccessControl access_control;

  ret = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      destroy_socket_control_list);

  /* Socket_Address_Type_Unix */
  unix_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (unix_tab, access_control);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_UNIX),
      unix_tab);

  /* Socket_Address_Type_IPv4 */
  ipv4_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (ipv4_tab, access_control);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV4),
      ipv4_tab);

  /* Socket_Address_Type_IPv6 */
  ipv6_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (ipv6_tab, access_control);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV6),
      ipv6_tab);

  return ret;
}

/* Callback plugged only if the tube has been offered with the new
 * Channel.Type.StreamTube API. */
static void
stream_unix_tube_new_connection_cb (GabbleTubeStream *self,
                                    guint contact,
                                    gpointer user_data)
{
  gabble_svc_channel_type_stream_tube_emit_stream_tube_new_connection (self,
      contact);
}


/**
 * gabble_tube_stream_offer_stream_tube
 *
 * Implements D-Bus method OfferStreamTube
 * on org.freedesktop.Telepathy.Channel.Type.StreamTube
 */
static void
gabble_tube_stream_offer_stream_tube (GabbleSvcChannelTypeStreamTube *iface,
                                      guint address_type,
                                      const GValue *address,
                                      guint access_control,
                                      const GValue *access_control_param,
                                      DBusGMethodInvocation *context)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (iface);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GError *error = NULL;

  if (priv->state != GABBLE_TUBE_CHANNEL_STATE_NOT_OFFERED)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the not offered state");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!gabble_tube_stream_check_params (address_type, address,
        access_control, access_control_param, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  g_assert (address_type == TP_SOCKET_ADDRESS_TYPE_UNIX ||
      address_type == TP_SOCKET_ADDRESS_TYPE_IPV4 ||
      address_type == TP_SOCKET_ADDRESS_TYPE_IPV6);
  g_assert (priv->address == NULL);
  priv->address_type = address_type;
  priv->address = tp_g_value_slice_dup (address);
  g_assert (priv->access_control == TP_SOCKET_ACCESS_CONTROL_LOCALHOST);
  priv->access_control = access_control;
  g_assert (priv->access_control_param == NULL);
  priv->access_control_param = tp_g_value_slice_dup (access_control_param);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Stream initiation */
      if (!gabble_tube_stream_offer (self, address_type,
          address, access_control, access_control_param, &error))
        {
          gabble_tube_stream_close (GABBLE_TUBE_IFACE (self), TRUE);

          dbus_g_method_return_error (context, error);

          g_error_free (error);
          return;
        }

      gabble_svc_channel_interface_tube_emit_tube_channel_state_changed (
          self, GABBLE_TUBE_CHANNEL_STATE_REMOTE_PENDING);
    }

  g_signal_connect (self, "tube-new-connection",
      G_CALLBACK (stream_unix_tube_new_connection_cb), self);

  g_signal_emit (G_OBJECT (self), signals[OFFERED], 0);

  gabble_svc_channel_type_stream_tube_return_from_offer_stream_tube (context);
}

/**
 * gabble_tube_stream_accept_stream_tube
 *
 * Implements D-Bus method AcceptStreamTube
 * on org.freedesktop.Telepathy.Channel.Type.StreamTube
 */
static void
gabble_tube_stream_accept_stream_tube (GabbleSvcChannelTypeStreamTube *iface,
                                       guint address_type,
                                       guint access_control,
                                       const GValue *access_control_param,
                                       DBusGMethodInvocation *context)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (iface);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GError *error = NULL;

  /* parameters sanity checks are done in gabble_tube_stream_accept */
  priv->address_type = address_type;
  priv->access_control = access_control;
  if (priv->access_control_param != NULL)
    tp_g_value_slice_free (priv->access_control_param);
  priv->access_control_param = tp_g_value_slice_dup (access_control_param);

  if (!gabble_tube_stream_accept (GABBLE_TUBE_IFACE (self), &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

#if 0
  /* TODO: add a property "muc" and set it at initialization */
  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    gabble_muc_channel_send_presence (self->muc, NULL);
#endif

  gabble_svc_channel_type_stream_tube_return_from_accept_stream_tube (context,
      priv->address);
}

/**
 * gabble_tube_stream_close_async:
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tube_stream_close_async (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  gabble_tube_stream_close (GABBLE_TUBE_IFACE (iface), FALSE);
  tp_svc_channel_return_from_close (context);
}

/**
 * gabble_tube_stream_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tube_stream_get_channel_type (TpSvcChannel *iface,
                                       DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      GABBLE_IFACE_CHANNEL_TYPE_STREAM_TUBE);
}

/**
 * gabble_tube_stream_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tube_stream_get_handle (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (iface);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, priv->handle_type,
      priv->handle);
}

/**
 * gabble_tube_stream_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tube_stream_get_interfaces (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (iface);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* omit the Group interface */
      tp_svc_channel_return_from_get_interfaces (context,
          gabble_tube_stream_interfaces + 1);
    }
  else
    {
      tp_svc_channel_return_from_get_interfaces (context,
          gabble_tube_stream_interfaces);
    }
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_tube_stream_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
tube_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  GabbleTubeIfaceClass *klass = (GabbleTubeIfaceClass *) g_iface;

  klass->accept = gabble_tube_stream_accept;
  klass->close = gabble_tube_stream_close;
  klass->add_bytestream = gabble_tube_stream_add_bytestream;
}

static void
streamtube_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleSvcChannelTypeStreamTubeClass *klass =
      (GabbleSvcChannelTypeStreamTubeClass *) g_iface;

#define IMPLEMENT(x) gabble_svc_channel_type_stream_tube_implement_##x (\
    klass, gabble_tube_stream_##x)
  IMPLEMENT(offer_stream_tube);
  IMPLEMENT(accept_stream_tube);
#undef IMPLEMENT
}
