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

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "presence.h"
#include "presence-cache.h"
#include "namespaces.h"
#include <telepathy-glib/svc-unstable.h>
#include "util.h"
#include "tube-iface.h"
#include "bytestream-factory.h"
#include "bytestream-ibb.h"
#include "gabble-signals-marshal.h"

static void
tube_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleTubeStream, gabble_tube_stream, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_TUBE_IFACE, tube_iface_init));

/* signals */
enum
{
  OPENED,
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
  PROP_SOCKET,
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
   * Another problem is currently this bytestream is not only way we have
   * to know when the remote contact close the tube for private tubes */
  GabbleBytestreamIBB *default_bytestream;
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  TpTubeState state;

  /* Path of the unix socket associated with this stream tube */
  gchar *socket_path;
  GIOChannel *listen_io_channel;
  guint listen_io_channel_source_id;

  gboolean dispose_has_run;
};

#define GABBLE_TUBE_STREAM_GET_PRIVATE(obj) \
    ((GabbleTubeStreamPrivate *) obj->priv)

static void data_received_cb (GabbleBytestreamIBB *ibb, TpHandle sender,
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
  GabbleBytestreamIBB *bytestream;
  int fd;
  gchar buffer[4096];
  gsize readed;
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
      return TRUE;
    }

  memset (&buffer, 0, sizeof (buffer));

  status = g_io_channel_read_chars (source, buffer, 4096, &readed, &error);
  if (status == G_IO_STATUS_NORMAL)
    {
      DEBUG ("read %d bytes from socket", readed);

      gabble_bytestream_ibb_send (bytestream, readed, buffer);
      result = TRUE;
    }
  else if (status == G_IO_STATUS_EOF)
    {
      DEBUG ("error reading from socket: EOF");

      gabble_bytestream_ibb_close (bytestream);
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

      gabble_bytestream_ibb_close (bytestream);
      result = FALSE;
    }

  if (error != NULL)
    g_error_free (error);

  return TRUE;
}

static void
extra_bytestream_state_changed_cb (GabbleBytestreamIBB *bytestream,
                                   BytestreamIBBState state,
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

  if (state == BYTESTREAM_IBB_STATE_OPEN)
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
  else if (state == BYTESTREAM_IBB_STATE_CLOSED)
    {
      int fd;

      DEBUG ("extra bytestream closed");

      fd = g_io_channel_unix_get_fd (channel);

      g_hash_table_remove (priv->fd_to_bytestreams, GINT_TO_POINTER (fd));
      g_hash_table_remove (priv->bytestream_to_io_channel, bytestream);
      g_hash_table_remove (priv->io_channel_to_watcher_source_id, channel);
    }
}

struct _bytestream_negotiate_cb_data
{
  GabbleTubeStream *self;
  gint fd;
};

static void
bytestream_negotiate_cb (GabbleBytestreamIBB *bytestream,
                         const gchar *stream_id,
                         LmMessage *msg,
                         gpointer user_data)
{
  struct _bytestream_negotiate_cb_data *data =
    (struct _bytestream_negotiate_cb_data *) user_data;
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

  g_slice_free (struct _bytestream_negotiate_cb_data, data);
}

static gboolean
start_stream_initiation (GabbleTubeStream *self,
                         gint fd,
                         GError **error)
{
  GabbleTubeStreamPrivate *priv;
  LmMessageNode *node;
  LmMessage *msg;
  TpHandleRepoIface *contact_repo;
  const gchar *jid;
  gchar *full_jid, *stream_id, *id_str;
  gboolean result;
  struct _bytestream_negotiate_cb_data *data;

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
      stream_id, NS_SI_TUBES_OLD);

  id_str = g_strdup_printf ("%u", priv->id);

  node = lm_message_node_add_child (msg->node, "tube", NULL);
  lm_message_node_set_attributes (node,
      "xmlns", NS_SI_TUBES_OLD,
      "type", "stream",
      "service", priv->service,
      "initiator", jid,
      "stream_id", stream_id,
      "id", id_str,
      "offering", "false",
      NULL);

  data = g_slice_new (struct _bytestream_negotiate_cb_data);
  data->self = self;
  data->fd = fd;

  result = gabble_bytestream_factory_negotiate_stream (
    priv->conn->bytestream_factory,
    msg,
    stream_id,
    bytestream_negotiate_cb,
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
                          GabbleBytestreamIBB *bytestream)
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

  strncpy (addr.sun_path, priv->socket_path,
      strlen (priv->socket_path) + 1);

  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
    {
      DEBUG ("Error connecting socket: %s", g_strerror (errno));
      return FALSE;
    }

  DEBUG ("Connected to socket: %s", priv->socket_path);
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
  g_assert (priv->socket_path == NULL);

  // XXX close the tube if error ?

  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (fd == -1)
    {
      DEBUG ("Error creating socket: %s", g_strerror (errno));
      return;
    }

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = PF_UNIX;

  /* Set socket non blocking */
  flags = fcntl (fd, F_GETFL, 0);
  if (fcntl (fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      DEBUG ("Can't set socket non blocking: %s", g_strerror (errno));
      return;
    }

  generate_ascii_string (8, suffix);
  priv->socket_path = g_strdup_printf ("/tmp/stream-gabble-%.8s", suffix);

  DEBUG ("create socket: %s", priv->socket_path);

  strncpy (addr.sun_path, priv->socket_path,
      strlen (priv->socket_path) + 1);

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

  priv->dispose_has_run = FALSE;
}

static void
bytestream_state_changed_cb (GabbleBytestreamIBB *bytestream,
                             BytestreamIBBState state,
                             gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (state == BYTESTREAM_IBB_STATE_CLOSED)
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
  GabbleBytestreamIBB *bytestream = (GabbleBytestreamIBB *) value;

  gabble_bytestream_ibb_close (bytestream);
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
      gabble_bytestream_ibb_close (priv->default_bytestream);
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
  g_free (priv->socket_path);

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
        g_value_set_uint (value, TP_TUBE_TYPE_STREAM_UNIX);
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
      case PROP_SOCKET:
        g_value_set_string (value, priv->socket_path);
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
      case PROP_SOCKET:
        g_free (priv->socket_path);
        priv->socket_path = g_value_dup_string (value);
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

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this D-Bus tube object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_uint (
      "handle",
      "Handle",
      "The TpHandle associated with the tubes channel that"
      "owns this D-Bus tube object.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);

  param_spec = g_param_spec_uint (
      "handle-type",
      "Handle type",
      "The TpHandleType of the handle associated with the tubes channel that"
      "owns this D-Bus tube object.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE_TYPE,
      param_spec);

  param_spec = g_param_spec_uint (
      "self-handle",
      "Self handle",
      "The handle to use for ourself. This can be different from the "
      "connection's self handle if our handle is a room handle.",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SELF_HANDLE, param_spec);

  param_spec = g_param_spec_uint (
      "id",
      "id",
      "The unique identifier of this tube",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  param_spec = g_param_spec_object (
      "bytestream",
      "GabbleBytestreamIBB object",
      "bytestream object created during SI if any",
      GABBLE_TYPE_BYTESTREAM_IBB,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_BYTESTREAM, param_spec);

  param_spec = g_param_spec_uint (
      "type",
      "Tube type",
      "The TpTubeType this D-Bus tube object.",
      0, G_MAXUINT32, TP_TUBE_TYPE_STREAM_UNIX,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TYPE, param_spec);

  param_spec = g_param_spec_uint (
      "initiator",
      "Initiator handle",
      "The TpHandle of the initiator of this D-Bus tube object.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_string (
      "service",
      "service name",
      "the service associated with this D-BUS tube object.",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SERVICE, param_spec);

  param_spec = g_param_spec_boxed (
      "parameters",
      "parameters GHashTable",
      "GHashTable containing parameters of this STREAM tube object.",
      G_TYPE_HASH_TABLE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PARAMETERS, param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "Tube state",
      "The TpTubeState of this STREAM tube object",
      0, G_MAXUINT32, TP_TUBE_STATE_REMOTE_PENDING,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_string (
      "socket",
      "socket path",
      "the path of the unix socket associated with this stream tube.",
      "",
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SOCKET, param_spec);

  signals[OPENED] =
    g_signal_new ("opened",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

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
data_received_cb (GabbleBytestreamIBB *bytestream,
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
  BytestreamIBBState state;
  const gchar *stream_init_id;

  tube_stream_open (self);
  priv->state = TP_TUBE_STATE_OPEN;
  g_signal_emit (G_OBJECT (self), signals[OPENED], 0);

  if (priv->default_bytestream == NULL)
    return;

  g_object_get (priv->default_bytestream,
      "state", &state,
      NULL);

  if (state != BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    return;

  g_object_get (priv->default_bytestream,
      "stream-init-id", &stream_init_id,
      NULL);

  if (stream_init_id != NULL)
    {
      /* Bytestream was created using a SI request so
       * we have to accept it */
      LmMessage *msg;
      LmMessageNode *si, *tube_node;

      DEBUG ("accept the SI request");

      msg = gabble_bytestream_ibb_make_accept_iq (priv->default_bytestream);
      if (msg == NULL)
        {
          DEBUG ("can't create SI accept IQ. Close the bytestream");
          gabble_bytestream_ibb_close (priv->default_bytestream);
          return;
        }

      si = lm_message_node_get_child_with_namespace (msg->node, "si",
          NS_SI);
      g_assert (si != NULL);

      tube_node = lm_message_node_add_child (si, "tube", "");
      lm_message_node_set_attribute (tube_node, "xmlns", NS_SI_TUBES_OLD);

      gabble_bytestream_ibb_accept (priv->default_bytestream, msg);

      lm_message_unref (msg);
    }
  else
    {
      /* No SI so the bytestream is open */
      DEBUG ("no SI, bytestream open");
      g_object_set (priv->default_bytestream,
          "state", BYTESTREAM_IBB_STATE_OPEN,
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
      gabble_bytestream_ibb_close (priv->default_bytestream);
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
                                   GabbleBytestreamIBB *bytestream)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (priv->initiator != priv->self_handle)
    {
      DEBUG ("I'm not the initiator of this tube, can't accept "
          "an extra bytestream");

      gabble_bytestream_ibb_close (bytestream);
      return;
    }

  /* New bytestream, let's connect to the socket */
  if (new_connection_to_socket (self, bytestream))
    {
      LmMessage *msg;
      LmMessageNode *si, *tube_node;

      DEBUG ("accept the extra bytestream");

      msg = gabble_bytestream_ibb_make_accept_iq (bytestream);
      if (msg == NULL)
        {
          DEBUG ("can't create SI accept IQ. Close the bytestream");
          gabble_bytestream_ibb_close (bytestream);
          return;
        }

      si = lm_message_node_get_child_with_namespace (msg->node, "si",
          NS_SI);
      g_assert (si != NULL);

      tube_node = lm_message_node_add_child (si, "tube", "");
      lm_message_node_set_attribute (tube_node, "xmlns", NS_SI_TUBES_OLD);

      gabble_bytestream_ibb_accept (bytestream, msg);

      lm_message_unref (msg);
    }
  else
    {
      gabble_bytestream_ibb_close (bytestream);
    }
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
