/*
 * tube-stream.c - Source for GabbleTubeStream
 * Copyright (C) 2007 Ltd.
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

#include "tube-stream.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "debug.h"
#include "disco.h"
#include "extensions/extensions.h"
#include "gabble-connection.h"
#include "presence.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "util.h"
#include "tube-iface.h"
#include "bytestream-factory.h"
#include "bytestream-iface.h"
#include "gabble-signals-marshal.h"

static void
tube_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleTubeStream, gabble_tube_stream, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_TUBE_IFACE, tube_iface_init));

/* signals */
enum
{
  OPENED,
  NEW_CONNECTION,
  CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_HANDLE,
  PROP_HANDLE_TYPE,
  PROP_SELF_HANDLE,
  PROP_ID,
  PROP_BYTESTREAM,
  PROP_TYPE,
  PROP_INITIATOR,
  PROP_SERVICE,
  PROP_PARAMETERS,
  PROP_STATE,
  PROP_ADDRESS_TYPE,
  PROP_ADDRESS,
  PROP_ACCESS_CONTROL,
  PROP_ACCESS_CONTROL_PARAM,
  LAST_PROPERTY
};

typedef struct _GabbleTubeStreamPrivate GabbleTubeStreamPrivate;
struct _GabbleTubeStreamPrivate
{
  GabbleConnection *conn;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandle self_handle;
  guint id;
  GHashTable *fd_to_bytestreams;
  GHashTable *bytestream_to_io_channel;
  GHashTable *io_channel_to_watcher_source_id;
  /* Default bytestream (the one created during SI)
   * XXX: this is crack because we don't use/need this bytestream
   * at all.
   * Maybe we should refactor SI to not automatically create the bytestream
   * and delegates that to the tube.
   * Another problem is currently this bytestream is the only way we have
   * to know when the remote contact close the tube for private tubes */
  GabbleBytestreamIface *default_bytestream;
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  TpTubeState state;

  TpSocketAddressType address_type;
  GValue *address;
  TpSocketAccessControl access_control;
  GValue *access_control_param;
  GIOChannel *listen_io_channel;
  guint listen_io_channel_source_id;

  gboolean dispose_has_run;
};

#define GABBLE_TUBE_STREAM_GET_PRIVATE(obj) \
    ((GabbleTubeStreamPrivate *) obj->priv)

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

gboolean
data_to_read_on_socket_cb (GIOChannel *source,
                           GIOCondition condition,
                           gpointer data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GabbleBytestreamIface *bytestream;
  int fd;
  gchar buffer[4096];
  gsize num_read;
  GIOStatus status;
  GError *error = NULL;
  gboolean result = TRUE;

  if (! (condition & G_IO_IN))
    return TRUE;

  fd = g_io_channel_unix_get_fd (source);

  bytestream = g_hash_table_lookup (priv->fd_to_bytestreams,
      GINT_TO_POINTER (fd));
  if (bytestream == NULL)
    {
      DEBUG ("no bytestream associated with this socket");

      g_hash_table_remove (priv->io_channel_to_watcher_source_id, source);
      return FALSE;
    }

  memset (&buffer, 0, sizeof (buffer));

  status = g_io_channel_read_chars (source, buffer, 4096, &num_read, &error);
  if (status == G_IO_STATUS_NORMAL)
    {
      DEBUG ("read %d bytes from socket", num_read);

      gabble_bytestream_iface_send (bytestream, num_read, buffer);
      result = TRUE;
    }
  else if (status == G_IO_STATUS_EOF)
    {
      DEBUG ("error reading from socket: EOF");

      gabble_bytestream_iface_close (bytestream, NULL);
      result = FALSE;
    }
  else if (status == G_IO_STATUS_AGAIN)
    {
      DEBUG ("error reading from socket: resource temporarily unavailable");

      result = TRUE;
    }
  else
    {
      DEBUG ("error reading from socket: %s", error ? error->message : "");

      gabble_bytestream_iface_close (bytestream, NULL);
      result = FALSE;
    }

  if (error != NULL)
    g_error_free (error);

  return TRUE;
}

static void
extra_bytestream_state_changed_cb (GabbleBytestreamIface *bytestream,
                                   GabbleBytestreamState state,
                                   gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GIOChannel *channel;

  channel = g_hash_table_lookup (priv->bytestream_to_io_channel,
      bytestream);
  if (channel == NULL)
    {
      DEBUG ("no IO channel associated with the bytestream");
      return;
    }

  if (state == GABBLE_BYTESTREAM_STATE_OPEN)
    {
      guint source_id;
      DEBUG ("extra bytestream open");

      g_signal_connect (bytestream, "data-received",
          G_CALLBACK (data_received_cb), self);

      source_id = g_io_add_watch (channel, G_IO_IN, data_to_read_on_socket_cb,
          self);
      g_hash_table_insert (priv->io_channel_to_watcher_source_id,
          g_io_channel_ref (channel), GUINT_TO_POINTER (source_id));
    }
  else if (state == GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      int fd;

      DEBUG ("extra bytestream closed");

      fd = g_io_channel_unix_get_fd (channel);

      g_hash_table_remove (priv->fd_to_bytestreams, GINT_TO_POINTER (fd));
      g_hash_table_remove (priv->bytestream_to_io_channel, bytestream);
      g_hash_table_remove (priv->io_channel_to_watcher_source_id, channel);
    }
}

struct _extra_bytestream_negotiate_cb_data
{
  GabbleTubeStream *self;
  gint fd;
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
  GIOChannel *channel;

  if (bytestream == NULL)
    {
      DEBUG ("initiator refused new bytestream");

      close (data->fd);
      return;
    }

  DEBUG ("extra bytestream accepted");

  channel = g_io_channel_unix_new (data->fd);
  g_io_channel_set_encoding (channel, NULL, NULL);
  g_io_channel_set_buffered (channel, FALSE);
  g_io_channel_set_close_on_unref (channel, TRUE);

  g_hash_table_insert (priv->fd_to_bytestreams, GINT_TO_POINTER (data->fd),
      g_object_ref (bytestream));
  g_hash_table_insert (priv->bytestream_to_io_channel,
      g_object_ref (bytestream), channel);

  g_signal_connect (bytestream, "state-changed",
                G_CALLBACK (extra_bytestream_state_changed_cb), self);

  g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
}

static gboolean
start_stream_initiation (GabbleTubeStream *self,
                         gint fd,
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
     (TpBaseConnection*) priv->conn, TP_HANDLE_TYPE_CONTACT);

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
  data->fd = fd;

  result = gabble_bytestream_factory_negotiate_stream (
    priv->conn->bytestream_factory,
    msg,
    stream_id,
    extra_bytestream_negotiate_cb,
    data,
    error);

  lm_message_unref (msg);
  g_free (stream_id);
  g_free (full_jid);
  g_free (id_str);

  return result;
}

gboolean
listen_cb (GIOChannel *source,
           GIOCondition condition,
           gpointer data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (data);
  int fd, listen_fd;
  struct sockaddr_un addr;
  socklen_t addrlen;
  int flags;

  listen_fd = g_io_channel_unix_get_fd (source);

  addrlen = sizeof (struct sockaddr_un);
  fd = accept (listen_fd, (struct sockaddr *) &addr, &addrlen);
  if (fd == -1)
    {
      DEBUG ("Error accepting socket: %s", g_strerror (errno));
      return TRUE;
    }

  DEBUG ("connection from client");

  /* Set socket non blocking */
  flags = fcntl (fd, F_GETFL, 0);
  if (fcntl (fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      DEBUG ("Can't set socket non blocking: %s", g_strerror (errno));
      close (fd);
      return TRUE;
    }

  DEBUG ("request new bytestream");

  if (!start_stream_initiation (self, fd, NULL))
    {
      DEBUG ("closing new client connection");
      close (fd);
    }

  return TRUE;
}

static gboolean
new_connection_to_socket (GabbleTubeStream *self,
                          GabbleBytestreamIface *bytestream)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  int fd;
  struct sockaddr_un addr;
  int flags;
  GIOChannel *channel;

  g_assert (priv->initiator == priv->self_handle);

  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (fd == -1)
    {
      DEBUG ("Error creating socket: %s", g_strerror (errno));
      return FALSE;
    }

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = PF_UNIX;

  /* Set socket non blocking */
  flags = fcntl (fd, F_GETFL, 0);
  if (fcntl (fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      DEBUG ("Can't set socket non blocking: %s", g_strerror (errno));
      return FALSE;
    }

  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      array = g_value_get_boxed (priv->address);

      strncpy (addr.sun_path, array->data, array->len + 1);

      DEBUG ("Will try to connect to socket: %s", (const gchar *) array->data);
    }
  else
    {
      g_assert_not_reached ();
    }

  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
    {
      DEBUG ("Error connecting socket: %s", g_strerror (errno));
      return FALSE;
    }
  DEBUG ("Connected to socket");

  channel = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (channel, NULL, NULL);
  g_io_channel_set_buffered (channel, FALSE);
  g_io_channel_set_close_on_unref (channel, TRUE);

  g_hash_table_insert (priv->fd_to_bytestreams, GINT_TO_POINTER (fd),
      g_object_ref (bytestream));
  g_hash_table_insert (priv->bytestream_to_io_channel,
      g_object_ref (bytestream), channel);

  g_signal_connect (bytestream, "state-changed",
                G_CALLBACK (extra_bytestream_state_changed_cb), self);

  return TRUE;
}

static void
tube_stream_open (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  int fd;
  struct sockaddr_un addr;
  int flags;
  gchar suffix[8];

  DEBUG ("called");

  if (priv->initiator == priv->self_handle)
    /* Nothing to do if we are the initiator of this tube.
     * We'll connect to the socket each time request a new bytestream. */
    return;

  /* We didn't create this tube so it doesn't have
   * a socket associated with it. Let's create one */
  g_assert (priv->address == NULL);

  // XXX close the tube if error ?

  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (fd == -1)
    {
      DEBUG ("Error creating socket: %s", g_strerror (errno));
      return;
    }

  /* Set socket non blocking */
  flags = fcntl (fd, F_GETFL, 0);
  if (fcntl (fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      DEBUG ("Can't set socket non blocking: %s", g_strerror (errno));
      return;
    }

  memset (&addr, 0, sizeof (addr));

  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      gchar *socket_path;

      generate_ascii_string (8, suffix);
      socket_path = g_strdup_printf ("/tmp/stream-gabble-%.8s", suffix);

      array = g_array_sized_new (TRUE, FALSE, sizeof (gchar), strlen (
            socket_path));
      g_array_insert_vals (array, 0, socket_path, strlen (socket_path));

      priv->address = tp_g_value_slice_new (DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_set_boxed (priv->address, array);

      DEBUG ("create socket: %s", socket_path);

      addr.sun_family = PF_UNIX;

      strncpy (addr.sun_path, socket_path,
          strlen (socket_path) + 1);

      g_free (socket_path);
      g_array_free (array, TRUE);
    }
  else
    {
      g_assert_not_reached ();
    }

  if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
    {
      DEBUG ("Error binding socket: %s", g_strerror (errno));
      return;
    }

  if (listen (fd, 5) == -1)
    {
      DEBUG ("Error listening socket: %s", g_strerror (errno));
      return;
    }

  priv->listen_io_channel = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (priv->listen_io_channel, NULL, NULL);
  g_io_channel_set_buffered (priv->listen_io_channel, FALSE);
  g_io_channel_set_close_on_unref (priv->listen_io_channel, TRUE);

  priv->listen_io_channel_source_id = g_io_add_watch (priv->listen_io_channel,
      G_IO_IN, listen_cb, self);
}

static void
remove_watcher_source_id (gpointer data)
{
  guint source_id = GPOINTER_TO_UINT (data);
  GSource *source;

  source = g_main_context_find_source_by_id (NULL, source_id);
  if (source != NULL)
    {
      g_source_destroy (source);
    }
}

static void
gabble_tube_stream_init (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBE_STREAM, GabbleTubeStreamPrivate);

  self->priv = priv;

  priv->fd_to_bytestreams = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_object_unref);

  priv->bytestream_to_io_channel = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_object_unref,
      (GDestroyNotify) g_io_channel_unref);

  priv->io_channel_to_watcher_source_id = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_io_channel_unref,
      (GDestroyNotify) remove_watcher_source_id);

  priv->default_bytestream = NULL;
  priv->listen_io_channel = NULL;
  priv->listen_io_channel_source_id = 0;
  priv->address_type = TP_SOCKET_ADDRESS_TYPE_UNIX;
  priv->address = NULL;
  priv->access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  priv->access_control_param = NULL;

  priv->dispose_has_run = FALSE;
}

static void
bytestream_state_changed_cb (GabbleBytestreamIface *bytestream,
                             GabbleBytestreamState state,
                             gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (state == GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      if (priv->default_bytestream != NULL)
        {
          g_object_unref (priv->default_bytestream);
          priv->default_bytestream = NULL;
        }

      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
    }
}

static void
close_each_extra_bytestream (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  GabbleBytestreamIface *bytestream = (GabbleBytestreamIface *) value;

  gabble_bytestream_iface_close (bytestream, NULL);
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

  if (priv->default_bytestream)
    {
      gabble_bytestream_iface_close (priv->default_bytestream, NULL);
    }

  if (priv->fd_to_bytestreams != NULL)
    {
      g_hash_table_foreach (priv->fd_to_bytestreams,
          close_each_extra_bytestream, self);

      g_hash_table_destroy (priv->fd_to_bytestreams);
      priv->fd_to_bytestreams = NULL;
    }

  if (priv->bytestream_to_io_channel != NULL)
    {
      g_hash_table_destroy (priv->bytestream_to_io_channel);
      priv->bytestream_to_io_channel = NULL;
    }

  if (priv->io_channel_to_watcher_source_id != NULL)
    {
      g_hash_table_destroy (priv->io_channel_to_watcher_source_id);
      priv->io_channel_to_watcher_source_id = NULL;
    }

  tp_handle_unref (contact_repo, priv->initiator);

  if (priv->listen_io_channel_source_id != 0)
    {
      g_source_destroy (g_main_context_find_source_by_id (NULL,
            priv->listen_io_channel_source_id));
      priv->listen_io_channel_source_id = 0;
    }

  if (priv->listen_io_channel)
    {
      g_io_channel_unref (priv->listen_io_channel);
      priv->listen_io_channel = NULL;
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

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
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
      case PROP_BYTESTREAM:
        g_value_set_object (value, priv->default_bytestream);
        break;
      case PROP_TYPE:
        g_value_set_uint (value, TP_TUBE_TYPE_STREAM);
        break;
      case PROP_INITIATOR:
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
      case PROP_BYTESTREAM:
        if (priv->default_bytestream == NULL)
          {
            priv->default_bytestream = g_value_get_object (value);
            g_object_ref (priv->default_bytestream);

            g_signal_connect (priv->default_bytestream, "state-changed",
                G_CALLBACK (bytestream_state_changed_cb), self);
          }
        break;
      case PROP_INITIATOR:
        priv->initiator = g_value_get_uint (value);
        break;
      case PROP_SERVICE:
        g_free (priv->service);
        priv->service = g_value_dup_string (value);
        break;
      case PROP_PARAMETERS:
        priv->parameters = g_value_get_boxed (value);
        break;
      case PROP_ADDRESS_TYPE:
        /* For now, only UNIX sockets are implemented */
        g_assert (g_value_get_uint (value) == TP_SOCKET_ADDRESS_TYPE_UNIX);
        priv->address_type = g_value_get_uint (value);
        break;
      case PROP_ADDRESS:
        if (priv->address == NULL)
          {
            priv->address = tp_g_value_slice_dup (g_value_get_pointer (value));
          }
        break;
      case PROP_ACCESS_CONTROL:
        /* For now, only "localhost" control is implemented */
        g_assert (g_value_get_uint (value) ==
            TP_SOCKET_ACCESS_CONTROL_LOCALHOST);
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
          priv->state = TP_TUBE_STATE_REMOTE_PENDING;
        }
      else
        {
          /* Muc tube */
          priv->state = TP_TUBE_STATE_OPEN;
        }
    }
  else
    {
      priv->state = TP_TUBE_STATE_LOCAL_PENDING;
    }

  return obj;
}

static void
gabble_tube_stream_class_init (GabbleTubeStreamClass *gabble_tube_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_tube_stream_class);
  GParamSpec *param_spec;

  object_class->get_property = gabble_tube_stream_get_property;
  object_class->set_property = gabble_tube_stream_set_property;
  object_class->constructor = gabble_tube_stream_constructor;

  g_type_class_add_private (gabble_tube_stream_class,
      sizeof (GabbleTubeStreamPrivate));

  object_class->dispose = gabble_tube_stream_dispose;
  object_class->finalize = gabble_tube_stream_finalize;

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
  g_object_class_override_property (object_class, PROP_INITIATOR,
    "initiator");
  g_object_class_override_property (object_class, PROP_SERVICE,
    "service");
  g_object_class_override_property (object_class, PROP_PARAMETERS,
    "parameters");
  g_object_class_override_property (object_class, PROP_STATE,
    "state");

  param_spec = g_param_spec_uint (
      "address-type",
      "address type",
      "a TpSocketAddressType representing the type of the listening"
      "address of the local service",
      0, NUM_TP_SOCKET_ADDRESS_TYPES - 1,
      TP_SOCKET_ADDRESS_TYPE_UNIX,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ADDRESS_TYPE,
      param_spec);

  param_spec = g_param_spec_pointer (
      "address",
      "address",
      "The listening address of the local service, as indicated by the "
      "address-type",
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ADDRESS, param_spec);

  param_spec = g_param_spec_uint (
      "access-control",
      "access control",
      "a TpSocketAccessControl representing the access control "
      "the local service applies to the local socket",
      0, NUM_TP_SOCKET_ACCESS_CONTROLS - 1,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL,
      param_spec);

  param_spec = g_param_spec_pointer (
      "access-control-param",
      "access control param",
      "A parameter for the access control type, to be interpreted as specified"
      "in the documentation for the Socket_Access_Control enum.",
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL_PARAM,
      param_spec);

  signals[OPENED] =
    g_signal_new ("opened",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
data_received_cb (GabbleBytestreamIface *bytestream,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  GabbleTubeStream *tube = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (tube);
  gsize written;
  GIOChannel *channel;
  GIOStatus status;
  GError *error = NULL;

  DEBUG ("received %d bytes from bytestream", data->len);

  channel = g_hash_table_lookup (priv->bytestream_to_io_channel, bytestream);
  if (channel == NULL)
    {
      DEBUG ("no IO channel associated with the bytestream");
      return;
    }

  status = g_io_channel_write_chars (channel, data->str, data->len,
      &written, &error);
  if (status == G_IO_STATUS_NORMAL)
    {
      DEBUG ("%d bytes written to the socket", written);
    }
  else
    {
      DEBUG ("error writing to socket: %s",
          error ? error->message : "");
    }

  if (error != NULL)
    g_error_free (error);
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
  return g_object_new (GABBLE_TYPE_TUBE_STREAM,
      "connection", conn,
      "handle", handle,
      "handle-type", handle_type,
      "self-handle", self_handle,
      "initiator", initiator,
      "service", service,
      "parameters", parameters,
      "id", id,
      NULL);
}

static LmMessage *
create_si_accept_iq (GabbleBytestreamIface *bytestream)
{
  LmMessage *msg;
  gchar *stream_init_id, *peer_jid;
  const gchar *protocol;

  g_object_get (bytestream,
      "stream-init-id", &stream_init_id,
      "peer-jid", &peer_jid,
      NULL);

  protocol = gabble_bytestream_iface_get_protocol (bytestream);
  msg = gabble_bytestream_factory_make_accept_iq (peer_jid, stream_init_id,
      protocol);

  g_free (stream_init_id);
  g_free (peer_jid);
  return msg;
}

/**
 * gabble_tube_stream_accept
 *
 * Implements gabble_tube_iface_accept on GabbleTubeIface
 */
static void
gabble_tube_stream_accept (GabbleTubeIface *tube)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  GabbleBytestreamState state;

  tube_stream_open (self);
  priv->state = TP_TUBE_STATE_OPEN;
  g_signal_emit (G_OBJECT (self), signals[OPENED], 0);

  if (priv->default_bytestream == NULL)
    return;

  g_object_get (priv->default_bytestream,
      "state", &state,
      NULL);

  if (state != GABBLE_BYTESTREAM_STATE_LOCAL_PENDING)
    return;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Bytestream was created using a SI request so
       * we have to accept it */
      LmMessage *msg;
      LmMessageNode *si, *tube_node;

      DEBUG ("accept the SI request");

      msg = create_si_accept_iq (priv->default_bytestream);
      si = lm_message_node_get_child_with_namespace (msg->node, "si",
          NS_SI);
      g_assert (si != NULL);

      tube_node = lm_message_node_add_child (si, "tube", "");
      lm_message_node_set_attribute (tube_node, "xmlns", NS_TUBES);

      gabble_bytestream_iface_accept (priv->default_bytestream, msg);

      lm_message_unref (msg);
    }
  else
    {
      /* No SI so the bytestream is open */
      DEBUG ("no SI, bytestream open");
      g_object_set (priv->default_bytestream,
          "state", GABBLE_BYTESTREAM_STATE_OPEN,
          NULL);
    }
}

/**
 * gabble_tube_stream_close
 *
 * Implements gabble_tube_iface_close on GabbleTubeIface
 */
static void
gabble_tube_stream_close (GabbleTubeIface *tube)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (priv->default_bytestream != NULL)
    {
      gabble_bytestream_iface_close (priv->default_bytestream, NULL);
    }
  else
    {
      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
    }
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
      LmMessage *msg;
      LmMessageNode *si, *tube_node;
      TpHandle contact;

      DEBUG ("accept the extra bytestream");

      msg = create_si_accept_iq (bytestream);
      si = lm_message_node_get_child_with_namespace (msg->node, "si",
          NS_SI);
      g_assert (si != NULL);

      tube_node = lm_message_node_add_child (si, "tube", "");
      lm_message_node_set_attribute (tube_node, "xmlns", NS_TUBES);

      gabble_bytestream_iface_accept (bytestream, msg);

      g_object_get (bytestream, "peer-handle", &contact, NULL);

      g_signal_emit (G_OBJECT (self), signals[NEW_CONNECTION], 0, contact);

      lm_message_unref (msg);
    }
  else
    {
      gabble_bytestream_iface_close (bytestream, NULL);
    }
}

gboolean
gabble_tube_stream_check_params (TpSocketAddressType address_type,
                                 const GValue *address,
                                 TpSocketAccessControl access_control,
                                 const GValue *access_control_param,
                                 GError **error)
{
  if (address_type != TP_SOCKET_ADDRESS_TYPE_UNIX)
  {
    g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "Address type %d not implemented", address_type);
    return FALSE;
  }

  if (address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      GString *socket;
      struct stat stat_buff;

      /* Check address type */
      if (G_VALUE_TYPE (address) != DBUS_TYPE_G_UCHAR_ARRAY)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Unix socket address is supposed to be ay");
          return FALSE;
        }

      array = g_value_get_boxed (address);
      socket = g_string_new_len (array->data, array->len);

      if (g_stat (socket->str, &stat_buff) == -1)
      {
        DEBUG ("Error calling stat on socket: %s", g_strerror (errno));

        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "%s: %s",
            socket->str, g_strerror (errno));
        g_string_free (socket, TRUE);
        return FALSE;
      }

      if (!S_ISSOCK (stat_buff.st_mode))
      {
        DEBUG ("%s is not a socket", socket->str);

        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "%s is not a socket", socket->str);
        g_string_free (socket, TRUE);
        return FALSE;
      }

      g_string_free (socket, TRUE);

      if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
      {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "Unix sockets only support localhost control access");
        return FALSE;
      }
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
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
