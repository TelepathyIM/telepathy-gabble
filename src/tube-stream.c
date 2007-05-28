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
#include "namespaces.h"
#include <telepathy-glib/svc-unstable.h>
#include "util.h"
#include "tube-iface.h"
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
  PROP_UNIQUE_ID,
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
  gchar *unique_id;
  GabbleBytestreamIBB *bytestream;
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;

  /* Path of the unix socket associated with this stream tube */
  gchar *socket_path;
  GIOChannel *io_channel;
  GIOChannel *listen_io_channel;

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
socket_recv_data_cb (GIOChannel *source,
                     GIOCondition condition,
                     gpointer data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  gchar buffer[4096];
  gsize readed;
  GIOStatus status;
  GError *error = NULL;

  if (! (condition & G_IO_IN))
    return TRUE;

  memset (&buffer, 0, sizeof (buffer));

  status = g_io_channel_read_chars (source, buffer, 4096, &readed, &error);
  if (status == G_IO_STATUS_NORMAL)
    {
      DEBUG ("read from socket: %s\n", buffer);

      gabble_bytestream_ibb_send (priv->bytestream, readed, buffer);
    }
  else
    {
      DEBUG ("error reading from socket: %s",
          error ? error->message : "");
    }

  if (error != NULL)
    g_error_free (error);

  return TRUE;
}

gboolean
listen_cb (GIOChannel *source,
           GIOCondition condition,
           gpointer data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  int fd, listen_fd;
  struct sockaddr_un addr;
  socklen_t addrlen;
  int flags;

  listen_fd = g_io_channel_unix_get_fd (source);

  fd = accept (listen_fd, (struct sockaddr *) &addr, &addrlen);
  if (fd == -1)
    {
      DEBUG ("Error accepting socket: %s", g_strerror (errno));
      return FALSE;
    }

  DEBUG ("connection from client");

  /* Set socket non blocking */
  flags = fcntl (fd, F_GETFL, 0);
  if (fcntl (fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      DEBUG ("Can't set socket non blocking: %s", g_strerror (errno));
      return FALSE;
    }

  priv->io_channel = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (priv->io_channel, NULL, NULL);
  g_io_channel_set_buffered (priv->io_channel, FALSE);
  g_io_add_watch (priv->io_channel, G_IO_IN,
      socket_recv_data_cb, self);

  /* Shall we accept more than one connection ? */
  return FALSE;
}

static void
tube_stream_open (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  int fd;
  struct sockaddr_un addr;
  int flags;

  DEBUG ("called");

  g_signal_connect (priv->bytestream, "data-received",
      G_CALLBACK (data_received_cb), self);

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

  if (priv->socket_path == NULL)
    {
      /* No socket associated, let's create one */
      gchar suffix[8];

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
      g_io_add_watch (priv->listen_io_channel, G_IO_IN,
          listen_cb, self);
    }
  else
    {
      DEBUG ("Connect to socket: %s", priv->socket_path);

      strncpy (addr.sun_path, priv->socket_path,
          strlen (priv->socket_path) + 1);

      if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
        {
          DEBUG ("Error connecting socket: %s", g_strerror (errno));
          return;
        }

      priv->io_channel = g_io_channel_unix_new (fd);
      g_io_channel_set_encoding (priv->io_channel, NULL, NULL);
      g_io_channel_set_buffered (priv->io_channel, FALSE);
      g_io_add_watch (priv->io_channel, G_IO_IN,
          socket_recv_data_cb, self);
    }
}

static void
gabble_tube_stream_init (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBE_STREAM, GabbleTubeStreamPrivate);

  self->priv = priv;

  priv->bytestream = NULL;
  priv->io_channel = NULL;
  priv->listen_io_channel = NULL;

  priv->dispose_has_run = FALSE;
}

static TpTubeState
get_tube_state (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  BytestreamIBBState bytestream_state;

  if (priv->bytestream == NULL)
    /* bytestream not yet created as we're waiting for the SI reply */
    return TP_TUBE_STATE_REMOTE_PENDING;

  g_object_get (priv->bytestream, "state", &bytestream_state, NULL);

  if (bytestream_state == BYTESTREAM_IBB_STATE_OPEN)
    return TP_TUBE_STATE_OPEN;

  else if (bytestream_state == BYTESTREAM_IBB_STATE_LOCAL_PENDING ||
      bytestream_state == BYTESTREAM_IBB_STATE_ACCEPTED)
    return TP_TUBE_STATE_LOCAL_PENDING;

  else if (bytestream_state == BYTESTREAM_IBB_STATE_INITIATING)
    return TP_TUBE_STATE_REMOTE_PENDING;

  else
    g_assert_not_reached ();
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
      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
      priv->bytestream = NULL;
    }
  else if (state == BYTESTREAM_IBB_STATE_OPEN)
    {
      tube_stream_open (self);
      g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
    }
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

  if (priv->bytestream)
    {
      gabble_bytestream_ibb_close (priv->bytestream);
      priv->bytestream  = NULL;
    }

  tp_handle_unref (contact_repo, priv->initiator);

  if (priv->io_channel)
    {
      g_io_channel_shutdown (priv->io_channel, TRUE, NULL);
      g_io_channel_unref (priv->io_channel);
      priv->io_channel = NULL;
    }

  if (priv->listen_io_channel)
    {
      g_io_channel_shutdown (priv->listen_io_channel, TRUE, NULL);
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
      case PROP_UNIQUE_ID:
        g_value_set_string (value, priv->unique_id);
        break;
      case PROP_BYTESTREAM:
        g_value_set_object (value, priv->bytestream);
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
        g_value_set_uint (value, get_tube_state (self));
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
      case PROP_UNIQUE_ID:
        g_free (priv->unique_id);
        priv->unique_id = g_value_dup_string (value);
        break;
      case PROP_BYTESTREAM:
        if (priv->bytestream == NULL)
          {
            BytestreamIBBState state;
            priv->bytestream = g_value_get_object (value);

            g_object_get (priv->bytestream, "state", &state, NULL);
            if (state == BYTESTREAM_IBB_STATE_OPEN)
              {
                tube_stream_open (self);
              }

            g_signal_connect (priv->bytestream, "state-changed",
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

  param_spec = g_param_spec_string (
      "unique-id",
      "unique id",
      "The unique identifier of this tube",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_UNIQUE_ID, param_spec);

  param_spec = g_param_spec_object (
      "bytestream",
      "GabbleBytestreamIBB object",
      "Gabble bytestream IBB object used for streaming data for this D-Bus"
      "tube object.",
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
      0, G_MAXUINT32, TP_TUBE_TYPE_STREAM,
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
data_received_cb (GabbleBytestreamIBB *ibb,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  GabbleTubeStream *tube = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (tube);
  gsize written;
  GIOStatus status;
  GError *error = NULL;

  DEBUG ("received: %s\n", data->str);

  if (priv->io_channel == NULL)
    return;

  status = g_io_channel_write_chars (priv->io_channel, data->str, data->len,
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
                        const gchar *unique_id)
{
  return g_object_new (GABBLE_TYPE_TUBE_STREAM,
      "connection", conn,
      "handle", handle,
      "handle-type", handle_type,
      "self-handle", self_handle,
      "initiator", initiator,
      "service", service,
      "parameters", parameters,
      "unique-id", unique_id,
      NULL);
}

/**
 * gabble_tube_stream_get_stream_id
 *
 * Implements gabble_tube_iface_get_stream_id on GabbleTubeIface
 */
static gchar *
gabble_tube_stream_get_stream_id (GabbleTubeIface *tube)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  gchar *stream_id;

  if (priv->bytestream == NULL)
    return NULL;

  g_object_get (priv->bytestream, "stream-id", &stream_id, NULL);
  return stream_id;
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

  g_assert (priv->bytestream != NULL);

  g_object_get (priv->bytestream,
      "state", &state,
      NULL);

  if (state != BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    return;

  g_object_get (priv->bytestream,
      "stream-init-id", &stream_init_id,
      NULL);

  if (stream_init_id != NULL)
    {
      /* Bytestream was created using a SI request so
       * we have to accept it */
      LmMessage *msg;
      LmMessageNode *si, *tube_node;

      DEBUG ("accept the SI request");

      msg = gabble_bytestream_ibb_make_accept_iq (priv->bytestream);

      si = lm_message_node_get_child_with_namespace (msg->node, "si",
          NS_SI);
      g_assert (si != NULL);

      tube_node = lm_message_node_add_child (si, "tube", "");
      lm_message_node_set_attribute (tube_node, "xmlns", NS_SI_TUBES);

      gabble_bytestream_ibb_accept (priv->bytestream, msg);

      lm_message_unref (msg);
    }
  else
    {
      /* No SI so the bytestream is open */
      DEBUG ("no SI, bytestream open");
      g_object_set (priv->bytestream,
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

  if (priv->bytestream != NULL)
    {
      gabble_bytestream_ibb_close (priv->bytestream);
      priv->bytestream = NULL;
    }
  else
    {
      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
    }
}

static void
tube_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  GabbleTubeIfaceClass *klass = (GabbleTubeIfaceClass *) g_iface;

  klass->get_stream_id = gabble_tube_stream_get_stream_id;
  klass->accept = gabble_tube_stream_accept;
  klass->close = gabble_tube_stream_close;
}
