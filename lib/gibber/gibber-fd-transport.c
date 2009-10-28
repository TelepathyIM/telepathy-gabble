/*
 * gibber-fd-transport.c - Source for GibberFdTransport
 * Copyright (C) 2006-2007 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "gibber-sockets.h"
#include "gibber-fd-transport.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

static gboolean _channel_io_out (GIOChannel *source,
    GIOCondition condition, gpointer data);


static gboolean gibber_fd_transport_send (GibberTransport *transport,
    const guint8 *data, gsize size, GError **error);

static void gibber_fd_transport_disconnect (GibberTransport *transport);

static gboolean gibber_fd_transport_get_peeraddr (GibberTransport *transport,
    struct sockaddr_storage *addr, socklen_t *len);

static gboolean gibber_fd_transport_get_sockaddr (GibberTransport *transport,
    struct sockaddr_storage *addr,
    socklen_t *len);

static void _do_disconnect (GibberFdTransport *self);

static gboolean gibber_fd_transport_buffer_is_empty (
    GibberTransport *transport);

static void gibber_fd_transport_block_receiving (GibberTransport *transport,
    gboolean block);

G_DEFINE_TYPE(GibberFdTransport, gibber_fd_transport, GIBBER_TYPE_TRANSPORT)

GQuark
gibber_fd_transport_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_fd_transport_error");

  return quark;
}

/* private structure */
typedef struct _GibberFdTransportPrivate GibberFdTransportPrivate;

struct _GibberFdTransportPrivate
{
  GIOChannel *channel;
  gboolean dispose_has_run;
  guint watch_in;
  guint watch_out;
  guint watch_err;
  GString *output_buffer;
  gboolean receiving_blocked;
};

#define GIBBER_FD_TRANSPORT_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_FD_TRANSPORT, \
   GibberFdTransportPrivate))


static void
gibber_fd_transport_init (GibberFdTransport *self)
{
  GibberFdTransportPrivate *priv = GIBBER_FD_TRANSPORT_GET_PRIVATE (self);
  self->fd = -1;
  priv->channel = NULL;
  priv->output_buffer = NULL;
  priv->watch_in = 0;
  priv->watch_out = 0;
  priv->watch_err = 0;
}

static void gibber_fd_transport_dispose (GObject *object);
static void gibber_fd_transport_finalize (GObject *object);
static GibberFdIOResult gibber_fd_transport_write (
   GibberFdTransport *fd_transport, GIOChannel *channel, const guint8 *data,
   int len, gsize *written, GError **error);

static void
gibber_fd_transport_class_init (
    GibberFdTransportClass *gibber_fd_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_fd_transport_class);
  GibberTransportClass *transport_class =
    GIBBER_TRANSPORT_CLASS (gibber_fd_transport_class);

  g_type_class_add_private (gibber_fd_transport_class,
                            sizeof (GibberFdTransportPrivate));

  object_class->dispose = gibber_fd_transport_dispose;
  object_class->finalize = gibber_fd_transport_finalize;

  transport_class->send = gibber_fd_transport_send;
  transport_class->disconnect = gibber_fd_transport_disconnect;
  transport_class->get_peeraddr = gibber_fd_transport_get_peeraddr;
  transport_class->get_sockaddr = gibber_fd_transport_get_sockaddr;
  transport_class->buffer_is_empty = gibber_fd_transport_buffer_is_empty;
  transport_class->block_receiving = gibber_fd_transport_block_receiving;

  gibber_fd_transport_class->read = gibber_fd_transport_read;
  gibber_fd_transport_class->write = gibber_fd_transport_write;
}

void
gibber_fd_transport_dispose (GObject *object)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (object);
  GibberFdTransportPrivate *priv = GIBBER_FD_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _do_disconnect (self);

  if (G_OBJECT_CLASS (gibber_fd_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_fd_transport_parent_class)->dispose (object);
}

void
gibber_fd_transport_finalize (GObject *object)
{
  G_OBJECT_CLASS (gibber_fd_transport_parent_class)->finalize (object);
}

static void
_do_disconnect (GibberFdTransport *self)
{
  GibberFdTransportPrivate *priv = GIBBER_FD_TRANSPORT_GET_PRIVATE (self);

  if (GIBBER_TRANSPORT(self)->state == GIBBER_TRANSPORT_DISCONNECTED)
    {
      return;
    }

  DEBUG ("Closing the fd transport");

  if (priv->channel != NULL)
    {
      if (priv->watch_in != 0)
        g_source_remove (priv->watch_in);

      if (priv->watch_out)
        g_source_remove (priv->watch_out);

      g_source_remove (priv->watch_err);
      g_io_channel_shutdown (priv->channel, FALSE, NULL);
      g_io_channel_unref (priv->channel);
      priv->channel = NULL;
    }
  else
    {
      close (self->fd);
    }
  self->fd = -1;

  if (priv->output_buffer)
    {
      g_string_free (priv->output_buffer, TRUE);
      priv->output_buffer = NULL;
    }

  if (!priv->dispose_has_run)
    /* If we are disposing we don't care about the state anymore */
    gibber_transport_set_state (GIBBER_TRANSPORT (self),
        GIBBER_TRANSPORT_DISCONNECTED);
}

static gboolean
_try_write (GibberFdTransport *self, const guint8 *data, int len,
    gsize *written, GError **err)
{
  GibberFdTransportPrivate *priv = GIBBER_FD_TRANSPORT_GET_PRIVATE (self);
  GibberFdTransportClass *cls = GIBBER_FD_TRANSPORT_GET_CLASS (self);
  GibberFdIOResult result;
  GError *error = NULL;

  result = cls->write (self, priv->channel, data, len, written, &error);

  switch (result)
    {
      case GIBBER_FD_IO_RESULT_SUCCESS:
      case GIBBER_FD_IO_RESULT_AGAIN:
        break;
      case GIBBER_FD_IO_RESULT_ERROR:
        gibber_transport_emit_error (GIBBER_TRANSPORT (self), error);
        /* fallthrough */
      case GIBBER_FD_IO_RESULT_EOF:
        DEBUG ("Writing data failed, closing the transport");
        _do_disconnect (self);

        g_propagate_error (err, error);
        return FALSE;
        break;
    }

    return TRUE;
}

static gboolean
_writeout (GibberFdTransport *self, const guint8 *data, gsize len,
    GError **error)
{
  GibberFdTransportPrivate *priv = GIBBER_FD_TRANSPORT_GET_PRIVATE (self);
  gsize written = 0;

  DEBUG ("Writing out %" G_GSIZE_FORMAT " bytes", len);
  if (priv->output_buffer == NULL || priv->output_buffer->len == 0)
    {
      /* We've got nothing buffer yet so try to write out directly */
      if (!_try_write (self, data, len, &written, error))
        {
          return FALSE;
        }
    }

  if (written == len)
    {
      gibber_transport_emit_buffer_empty (GIBBER_TRANSPORT (self));
      return TRUE;
    }

  if (priv->output_buffer)
    {
      g_string_append_len (priv->output_buffer, (gchar *) data + written,
        len - written);
    }
  else
    {
      priv->output_buffer = g_string_new_len ((gchar *) data + written,
          len - written);
    }

  if (!priv->watch_out)
    {
      priv->watch_out =
        g_io_add_watch (priv->channel, G_IO_OUT, _channel_io_out, self);
    }

  return TRUE;
}

static gboolean
_channel_io_in (GIOChannel *source, GIOCondition condition, gpointer data)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (data);
  GibberFdTransportPrivate *priv =
     GIBBER_FD_TRANSPORT_GET_PRIVATE (self);
  GibberFdIOResult result;
  GError *error = NULL;
  GibberFdTransportClass *cls = GIBBER_FD_TRANSPORT_GET_CLASS(self);

  result = cls->read (self, priv->channel, &error);

  switch (result)
    {
      case GIBBER_FD_IO_RESULT_SUCCESS:
      case GIBBER_FD_IO_RESULT_AGAIN:
        break;
      case GIBBER_FD_IO_RESULT_ERROR:
        gibber_transport_emit_error (GIBBER_TRANSPORT(self), error);
        /* Deliberately falling through */
      case GIBBER_FD_IO_RESULT_EOF:
        DEBUG("Failed to read from the transport, closing..");
        _do_disconnect (self);
        return FALSE;
    }

  return TRUE;
}

static gboolean
_channel_io_out (GIOChannel *source, GIOCondition condition, gpointer data)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (data);
  GibberFdTransportPrivate *priv =
     GIBBER_FD_TRANSPORT_GET_PRIVATE (self);
  gsize written;

  g_assert (priv->output_buffer);
  if (!_try_write (self, (guint8 *) priv->output_buffer->str,
                   priv->output_buffer->len, &written, NULL))
    {
      return FALSE;
    }

  if (written > 0 )
    {
      priv->output_buffer = g_string_erase (priv->output_buffer, 0, written);
    }

  if (priv->output_buffer->len == 0)
    {
      priv->watch_out = 0;
      gibber_transport_emit_buffer_empty (GIBBER_TRANSPORT (self));
      return FALSE;
    }

  return TRUE;
}

static gboolean
_channel_io_err (GIOChannel *source, GIOCondition condition, gpointer data)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (data);
  GError *error = NULL;
  gint code;
  const gchar *msg;

  if (condition & G_IO_ERR)
    {
      DEBUG ("Error on GIOChannel.Closing the transport");
      /* We can't use g_io_channel_error_from_errno because it seems errno is
       * not always set when we got a G_IO_ERR. */
      code = GIBBER_FD_TRANSPORT_ERROR_FAILED;
      msg = "Error on GIOChannel";
    }
  else
    {
      g_assert_not_reached ();
    }

  error = g_error_new_literal (GIBBER_FD_TRANSPORT_ERROR, code, msg);
  gibber_transport_emit_error (GIBBER_TRANSPORT (self), error);
  g_error_free (error);

  _do_disconnect (self);
  return FALSE;
}

/* Default read and write implementations */
static GibberFdIOResult
gibber_fd_transport_write (GibberFdTransport *fd_transport,
    GIOChannel *channel, const guint8 *data, int len,
    gsize *written, GError **error)
{
  GIOStatus status;

  status = g_io_channel_write_chars (channel, (gchar *) data, len,
    written, error);

  switch (status)
    {
      case G_IO_STATUS_NORMAL:
        return GIBBER_FD_IO_RESULT_SUCCESS;
      case G_IO_STATUS_AGAIN:
        return GIBBER_FD_IO_RESULT_AGAIN;
      case G_IO_STATUS_ERROR:
        return GIBBER_FD_IO_RESULT_ERROR;
      case G_IO_STATUS_EOF:
        return GIBBER_FD_IO_RESULT_EOF;
    }

    g_assert_not_reached ();
}

#define BUFSIZE 1024

GibberFdIOResult
gibber_fd_transport_read (GibberFdTransport *transport,
    GIOChannel *channel, GError **error)
{
  guint8  buf[BUFSIZE + 1];
  GIOStatus status;
  gsize bytes_read;

  status = g_io_channel_read_chars (channel, (gchar *) buf, BUFSIZE,
    &bytes_read, error);

  switch (status)
    {
      case G_IO_STATUS_NORMAL:
        buf[bytes_read] = '\0';
        DEBUG ("Received %" G_GSIZE_FORMAT " bytes", bytes_read);
        gibber_transport_received_data (GIBBER_TRANSPORT (transport),
            buf, bytes_read);
        return GIBBER_FD_IO_RESULT_SUCCESS;
      case G_IO_STATUS_ERROR:
        return GIBBER_FD_IO_RESULT_ERROR;
      case G_IO_STATUS_EOF:
        return GIBBER_FD_IO_RESULT_EOF;
      case G_IO_STATUS_AGAIN:
        return GIBBER_FD_IO_RESULT_AGAIN;
    }

  g_assert_not_reached ();
}


void
gibber_fd_transport_set_fd (GibberFdTransport *self, int fd)
{
  GibberFdTransportPrivate *priv = GIBBER_FD_TRANSPORT_GET_PRIVATE (self);

  g_assert (self->fd == -1 && fd >= 0);

  self->fd = fd;
  fcntl (fd, F_SETFL, O_NONBLOCK);
  priv->channel = g_io_channel_unix_new (fd);
  g_io_channel_set_close_on_unref (priv->channel, TRUE);
  g_io_channel_set_encoding (priv->channel, NULL, NULL);
  g_io_channel_set_buffered (priv->channel, FALSE);

  if (!priv->receiving_blocked)
    {
      priv->watch_in =
        g_io_add_watch (priv->channel, G_IO_IN, _channel_io_in, self);
    }

  priv->watch_err =
    g_io_add_watch (priv->channel, G_IO_ERR, _channel_io_err, self);

  gibber_transport_set_state (GIBBER_TRANSPORT(self),
      GIBBER_TRANSPORT_CONNECTED);
}

gboolean
gibber_fd_transport_send (GibberTransport *transport,
    const guint8 *data, gsize size, GError **error)
{
  return _writeout (GIBBER_FD_TRANSPORT (transport), data, size, error);
}

void
gibber_fd_transport_disconnect (GibberTransport *transport)
{
  DEBUG("Connection close requested");
  _do_disconnect (GIBBER_FD_TRANSPORT (transport));
}

static gboolean
gibber_fd_transport_get_peeraddr (GibberTransport *transport,
    struct sockaddr_storage *addr, socklen_t *len)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (transport);

  if (self->fd == -1)
    {
      DEBUG ("Someone requested the sockaddr while we're not connected");
      return FALSE;
    }

   *len = sizeof (struct sockaddr_storage);

   return (getpeername (self->fd, (struct sockaddr *) addr, len) == 0);
}

static gboolean
gibber_fd_transport_get_sockaddr (GibberTransport *transport,
    struct sockaddr_storage *addr,
    socklen_t *len)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (transport);

  if (self->fd == -1)
    {
      DEBUG ("Someone requested the sockaddr while we're not connected");
      return FALSE;
    }

   *len = sizeof (struct sockaddr_storage);

   return (getsockname (self->fd, (struct sockaddr *) addr, len) == 0);
}

static gboolean
gibber_fd_transport_buffer_is_empty (GibberTransport *transport)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (transport);
  GibberFdTransportPrivate *priv =
     GIBBER_FD_TRANSPORT_GET_PRIVATE (self);

  return (priv->output_buffer == NULL || priv->output_buffer->len == 0);
}

static void
gibber_fd_transport_block_receiving (GibberTransport *transport,
    gboolean block)
{
  GibberFdTransport *self = GIBBER_FD_TRANSPORT (transport);
  GibberFdTransportPrivate *priv = GIBBER_FD_TRANSPORT_GET_PRIVATE (self);

  if (block && priv->watch_in != 0)
    {
      DEBUG ("block receiving from the transport");
      g_source_remove (priv->watch_in);
      priv->watch_in = 0;
    }
  else if (!block && priv->watch_in == 0)
    {
      DEBUG ("unblock receiving from the transport");
      if (priv->channel != NULL)
        {
          priv->watch_in = g_io_add_watch (priv->channel, G_IO_IN,
              _channel_io_in, self);
        }
      /* else the transport isn't connected yet */
    }

  priv->receiving_blocked = block;
}
