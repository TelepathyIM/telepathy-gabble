/*
 * bytestream-socks5.c - Source for GabbleBytestreamSocks5
 * Copyright (C) 2006 Youness Alaoui <kakaroto@kakaroto.homelinux.net>
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
#include "bytestream-socks5.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_GETIFADDRS
 #include <ifaddrs.h>
#endif

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/interfaces.h>

#include <gibber/gibber-transport.h>
#include <gibber/gibber-tcp-transport.h>
#include <gibber/gibber-listener.h>

#define DEBUG_FLAG GABBLE_DEBUG_BYTESTREAM

#include "base64.h"
#include "bytestream-factory.h"
#include "bytestream-iface.h"
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleBytestreamSocks5, gabble_bytestream_socks5,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_PEER_HANDLE,
  PROP_PEER_HANDLE_TYPE,
  PROP_STREAM_ID,
  PROP_STREAM_INIT_ID,
  PROP_PEER_JID,
  PROP_PEER_RESOURCE,
  PROP_STATE,
  PROP_PROTOCOL,
  LAST_PROPERTY
};

enum _Socks5State
{
  SOCKS5_STATE_INVALID,
  SOCKS5_STATE_TRYING_CONNECT,
  SOCKS5_STATE_AUTH_REQUEST_SENT,
  SOCKS5_STATE_CONNECT_REQUESTED,
  SOCKS5_STATE_CONNECTED,
  SOCKS5_STATE_AWAITING_AUTH_REQUEST,
  SOCKS5_STATE_AWAITING_COMMAND,
  SOCKS5_STATE_ERROR
};

typedef enum _Socks5State Socks5State;

/* SOCKS5 commands */
#define SOCKS5_VERSION     0x05
#define SOCKS5_CMD_CONNECT 0x01
#define SOCKS5_RESERVED    0x00
#define SOCKS5_ATYP_DOMAIN 0x03
#define SOCKS5_STATUS_OK   0x00
#define SOCKS5_AUTH_NONE   0x00

#define SHA1_LENGTH 40
#define SOCKS5_CONNECT_LENGTH (7 + SHA1_LENGTH)

struct _Streamhost
{
  gchar *jid;
  gchar *host;
  gchar *port;
};
typedef struct _Streamhost Streamhost;

static Streamhost *
streamhost_new (const gchar *jid,
                const gchar *host,
                const gchar *port)
{
  Streamhost *streamhost;

  g_return_val_if_fail (jid != NULL, NULL);
  g_return_val_if_fail (host != NULL, NULL);

  streamhost = g_slice_new0 (Streamhost);
  streamhost->jid = g_strdup (jid);
  streamhost->host = g_strdup (host);
  streamhost->port = g_strdup (port);

  return streamhost;
}

static void
streamhost_free (Streamhost *streamhost)
{
  if (streamhost == NULL)
    return;

  g_free (streamhost->jid);
  g_free (streamhost->host);
  g_free (streamhost->port);
  g_slice_free (Streamhost, streamhost);
}

struct _GabbleBytestreamSocks5Private
{
  GabbleConnection *conn;
  TpHandle peer_handle;
  gchar *stream_id;
  gchar *stream_init_id;
  gchar *peer_resource;
  GabbleBytestreamState bytestream_state;
  gchar *peer_jid;
  gchar *self_full_jid;

  /* List of Streamhost */
  GSList *streamhosts;

  /* Connections to streamhosts are async, so we keep the IQ set message
   * around */
  LmMessage *msg_for_acknowledge_connection;

  Socks5State socks5_state;
  GibberTransport *transport;
  gboolean write_blocked;
  gboolean read_blocked;
  GibberListener *listener;

  GString *read_buffer;

  gboolean dispose_has_run;
};

#define GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE(obj) ((obj)->priv)

static void socks5_connect (GabbleBytestreamSocks5 *self);

static void gabble_bytestream_socks5_close (GabbleBytestreamIface *iface,
    GError *error);

static void socks5_error (GabbleBytestreamSocks5 *self);

static void transport_handler (GibberTransport *transport,
    GibberBuffer *data, gpointer user_data);

static void
gabble_bytestream_socks5_init (GabbleBytestreamSocks5 *self)
{
  GabbleBytestreamSocks5Private *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_SOCKS5, GabbleBytestreamSocks5Private);

  self->priv = priv;
}

static void
gabble_bytestream_socks5_dispose (GObject *object)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (object);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_handle_unref (contact_repo, priv->peer_handle);

  if (priv->bytestream_state != GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      gabble_bytestream_iface_close (GABBLE_BYTESTREAM_IFACE (self), NULL);
    }

  if (priv->transport != NULL)
    {
      g_object_unref (priv->transport);
      priv->transport = NULL;
    }

  if (priv->listener != NULL)
    {
      g_object_unref (priv->listener);
      priv->listener = NULL;
    }

  G_OBJECT_CLASS (gabble_bytestream_socks5_parent_class)->dispose (object);
}

static void
gabble_bytestream_socks5_finalize (GObject *object)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (object);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  g_free (priv->stream_id);
  g_free (priv->stream_init_id);
  g_free (priv->peer_resource);
  g_free (priv->peer_jid);
  g_free (priv->self_full_jid);

  g_slist_foreach (priv->streamhosts, (GFunc) streamhost_free, NULL);
  g_slist_free (priv->streamhosts);

  G_OBJECT_CLASS (gabble_bytestream_socks5_parent_class)->finalize (object);
}

static void
gabble_bytestream_socks5_get_property (GObject *object,
                                       guint property_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (object);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_PEER_HANDLE:
        g_value_set_uint (value, priv->peer_handle);
        break;
      case PROP_PEER_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_STREAM_ID:
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_STREAM_INIT_ID:
        g_value_set_string (value, priv->stream_init_id);
        break;
      case PROP_PEER_RESOURCE:
        g_value_set_string (value, priv->peer_resource);
        break;
      case PROP_PEER_JID:
        g_value_set_string (value, priv->peer_jid);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->bytestream_state);
        break;
      case PROP_PROTOCOL:
        g_value_set_string (value, NS_BYTESTREAMS);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_bytestream_socks5_set_property (GObject *object,
                                       guint property_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (object);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_PEER_HANDLE:
        priv->peer_handle = g_value_get_uint (value);
        break;
      case PROP_STREAM_ID:
        g_free (priv->stream_id);
        priv->stream_id = g_value_dup_string (value);
        break;
      case PROP_STREAM_INIT_ID:
        g_free (priv->stream_init_id);
        priv->stream_init_id = g_value_dup_string (value);
        break;
      case PROP_PEER_RESOURCE:
        g_free (priv->peer_resource);
        priv->peer_resource = g_value_dup_string (value);
        break;
      case PROP_STATE:
        if (priv->bytestream_state != g_value_get_uint (value))
            {
              priv->bytestream_state = g_value_get_uint (value);
              g_signal_emit_by_name (object, "state-changed",
                  priv->bytestream_state);
            }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_bytestream_socks5_constructor (GType type,
                                      guint n_props,
                                      GObjectConstructParam *props)
{
  GObject *obj;
  GabbleBytestreamSocks5Private *priv;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_repo;
  const gchar *jid;
  gchar *resource;

  obj = G_OBJECT_CLASS (gabble_bytestream_socks5_parent_class)->
           constructor (type, n_props, props);

  priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (GABBLE_BYTESTREAM_SOCKS5 (obj));

  g_assert (priv->conn != NULL);
  g_assert (priv->peer_handle != 0);
  g_assert (priv->stream_id != NULL);

  base_conn = TP_BASE_CONNECTION (priv->conn);
  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, priv->peer_handle);

  jid = tp_handle_inspect (contact_repo, priv->peer_handle);

  if (priv->peer_resource != NULL)
    priv->peer_jid = g_strdup_printf ("%s/%s", jid, priv->peer_resource);
  else
    priv->peer_jid = g_strdup (jid);

  g_object_get (priv->conn, "resource", &resource, NULL);
  priv->self_full_jid = g_strdup_printf ("%s/%s", tp_handle_inspect (
        contact_repo, base_conn->self_handle), resource);
  g_free (resource);

  return obj;
}

static void
gabble_bytestream_socks5_class_init (
    GabbleBytestreamSocks5Class *gabble_bytestream_socks5_class)
{
  GObjectClass *object_class =
      G_OBJECT_CLASS (gabble_bytestream_socks5_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_bytestream_socks5_class,
      sizeof (GabbleBytestreamSocks5Private));

  object_class->dispose = gabble_bytestream_socks5_dispose;
  object_class->finalize = gabble_bytestream_socks5_finalize;

  object_class->get_property = gabble_bytestream_socks5_get_property;
  object_class->set_property = gabble_bytestream_socks5_set_property;
  object_class->constructor = gabble_bytestream_socks5_constructor;

   g_object_class_override_property (object_class, PROP_CONNECTION,
      "connection");
   g_object_class_override_property (object_class, PROP_PEER_HANDLE,
       "peer-handle");
   g_object_class_override_property (object_class, PROP_PEER_HANDLE_TYPE,
       "peer-handle-type");
   g_object_class_override_property (object_class, PROP_STREAM_ID,
       "stream-id");
   g_object_class_override_property (object_class, PROP_PEER_JID,
       "peer-jid");
   g_object_class_override_property (object_class, PROP_STATE,
       "state");
   g_object_class_override_property (object_class, PROP_PROTOCOL,
       "protocol");

  param_spec = g_param_spec_string (
      "peer-resource",
      "Peer resource",
      "the resource used by the remote peer during the SI, if any",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
      param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);
}

static gboolean
write_to_transport (GabbleBytestreamSocks5 *self,
                    const gchar *data,
                    guint len,
                    GError **error)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  if (!gibber_transport_send (priv->transport, (const guint8 *) data, len,
        error))
    {
      return FALSE;
    }

  return TRUE;
}

static void
transport_connected_cb (GibberTransport *transport,
                        GabbleBytestreamSocks5 *self)
{
  GabbleBytestreamSocks5Private *priv =
    GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  if (priv->socks5_state == SOCKS5_STATE_TRYING_CONNECT)
    {
      gchar msg[3];

      DEBUG ("transport is connected. Sending auth request");

      msg[0] = SOCKS5_VERSION;
      /* Number of auth methods we are offering, we support just
       * SOCKS5_AUTH_NONE */
      msg[1] = 1;
      msg[2] = SOCKS5_AUTH_NONE;

      write_to_transport (self, msg, 3, NULL);

      priv->socks5_state = SOCKS5_STATE_AUTH_REQUEST_SENT;
    }
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           GabbleBytestreamSocks5 *self)
{
  DEBUG ("Sock5 transport disconnected");
  socks5_error (self);
}

static void
change_write_blocked_state (GabbleBytestreamSocks5 *self,
                            gboolean blocked)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  if (priv->write_blocked == blocked)
    return;

  priv->write_blocked = blocked;
  g_signal_emit_by_name (self, "write-blocked", blocked);
}

static void
socks5_close_transport (GabbleBytestreamSocks5 *self)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  if (priv->transport == NULL)
    return;

 if (priv->read_buffer)
   {
     g_string_free (priv->read_buffer, TRUE);
     priv->read_buffer = NULL;
   }

 g_signal_handlers_disconnect_matched (priv->transport,
        G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

 g_object_unref (priv->transport);
 priv->transport = NULL;
}

static void
bytestream_closed (GabbleBytestreamSocks5 *self)
{
  socks5_close_transport (self);
  g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSED, NULL);
}

static void
transport_buffer_empty_cb (GibberTransport *transport,
                           GabbleBytestreamSocks5 *self)
{
  GabbleBytestreamSocks5Private *priv = GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE
      (self);

  if (priv->bytestream_state == GABBLE_BYTESTREAM_STATE_CLOSING)
    {
      DEBUG ("buffer is now empty. Bytestream can be closed");
      bytestream_closed (self);
    }
  else if (priv->write_blocked)
    {
      DEBUG ("buffer is empty, unblock write to the bytestream");
      change_write_blocked_state (self, FALSE);
    }
}

static void
set_transport (GabbleBytestreamSocks5 *self,
               GibberTransport *transport)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  priv->transport = g_object_ref (transport);

  g_assert (priv->read_buffer == NULL);
  priv->read_buffer = g_string_sized_new (4096);

  gibber_transport_set_handler (transport, transport_handler, self);

  g_signal_connect (transport, "connected",
      G_CALLBACK (transport_connected_cb), self);
  g_signal_connect (transport, "disconnected",
      G_CALLBACK (transport_disconnected_cb), self);
  g_signal_connect (priv->transport, "buffer-empty",
      G_CALLBACK (transport_buffer_empty_cb), self);
}

static void
socks5_error (GabbleBytestreamSocks5 *self)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  Socks5State previous_state;

  previous_state = priv->socks5_state;
  priv->socks5_state = SOCKS5_STATE_ERROR;

  if (previous_state == SOCKS5_STATE_TRYING_CONNECT ||
      previous_state == SOCKS5_STATE_AUTH_REQUEST_SENT ||
      previous_state == SOCKS5_STATE_CONNECT_REQUESTED)
    {
      /* The attempt for connect to the streamhost failed */
      socks5_close_transport (self);

      /* Remove the failed streamhost */
      g_assert (priv->streamhosts);
      streamhost_free (priv->streamhosts->data);
      priv->streamhosts = g_slist_delete_link (priv->streamhosts,
          priv->streamhosts);

      if (priv->streamhosts != NULL)
        {
          DEBUG ("connection to streamhost failed, trying the next one");

          socks5_connect (self);
          return;
        }

      DEBUG ("no more streamhosts to try");

      g_signal_emit_by_name (self, "connection-error");

      g_assert (priv->msg_for_acknowledge_connection != NULL);
      _gabble_connection_send_iq_error (priv->conn,
          priv->msg_for_acknowledge_connection, XMPP_ERROR_ITEM_NOT_FOUND,
          "impossible to connect to any streamhost");

      lm_message_unref (priv->msg_for_acknowledge_connection);
      priv->msg_for_acknowledge_connection = NULL;
    }

  DEBUG ("error, closing the connection\n");

  gabble_bytestream_socks5_close (GABBLE_BYTESTREAM_IFACE (self), NULL);
}

static gchar *
compute_domain (const gchar *sid,
                const gchar *initiator,
                const gchar *target)
{
  gchar *unhashed_domain;
  gchar *domain;

  unhashed_domain = g_strconcat (sid, initiator, target, NULL);
  domain = sha1_hex (unhashed_domain, strlen (unhashed_domain));

  g_free (unhashed_domain);
  return domain;
}

/* Process the received data and returns the number of bytes that have been
 * used */
static gsize
socks5_handle_received_data (GabbleBytestreamSocks5 *self,
                             GString *string)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  gchar msg[SOCKS5_CONNECT_LENGTH];
  guint auth_len;
  guint i;
  const gchar *from;
  const gchar *to;
  gchar *domain;
  LmMessage *iq_result;

  switch (priv->socks5_state)
    {
      case SOCKS5_STATE_AUTH_REQUEST_SENT:
        /* We sent an authorization request and we are awaiting for a
         * response, the response is 2 bytes-long */
        if (string->len < 2)
          return 0;

        if (string->str[0] != SOCKS5_VERSION ||
            string->str[1] != SOCKS5_STATUS_OK)
          {
            DEBUG ("Authentication failed");

            socks5_error (self);
            return string->len;
          }

        /* We have been authorized, let's send a CONNECT command */

        DEBUG ("Received auth reply. Sending CONNECT command");

        from = lm_message_node_get_attribute (
            priv->msg_for_acknowledge_connection->node, "from");
        to = lm_message_node_get_attribute (
            priv->msg_for_acknowledge_connection->node, "to"),

        domain = compute_domain (priv->stream_id, from, to);

        msg[0] = SOCKS5_VERSION;
        msg[1] = SOCKS5_CMD_CONNECT;
        msg[2] = SOCKS5_RESERVED;
        msg[3] = SOCKS5_ATYP_DOMAIN;
        /* Length of a hex SHA1 */
        msg[4] = 40;
        /* Domain name: SHA-1(sid + initiator + target) */
        memcpy (&msg[5], domain, 40);
        /* Port: 0 */
        msg[45] = 0x00;
        msg[46] = 0x00;

        g_free (domain);

        write_to_transport (self, msg, SOCKS5_CONNECT_LENGTH, NULL);

        priv->socks5_state = SOCKS5_STATE_CONNECT_REQUESTED;

        return 2;

      case SOCKS5_STATE_CONNECT_REQUESTED:
        /* We sent an CONNECT request and we are awaiting for a response, the
         * response is 2 bytes-long */
        if (string->len < 2)
          return 0;

        if (string->str[0] != SOCKS5_VERSION ||
            string->str[1] != SOCKS5_STATUS_OK)
          {
            DEBUG ("Connection refused");

            socks5_error (self);
            return string->len;
          }

        DEBUG ("Received CONNECT reply. Socks5 stream connected. "
            "Bytestream is now open");
        priv->socks5_state = SOCKS5_STATE_CONNECTED;
        g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_OPEN, NULL);

        /* Acknowledge the connection */
        iq_result = lm_iq_message_make_result (
            priv->msg_for_acknowledge_connection);
        if (NULL != iq_result)
          {
            LmMessageNode *node;
            Streamhost *current_streamhost;

            node = lm_message_node_add_child (iq_result->node, "query", "");
            lm_message_node_set_attribute (node, "xmlns", NS_BYTESTREAMS);

            /* streamhost-used informs the other end of the streamhost we
             * decided to use. In case of a direct connetion this is useless
             * but if we are using an external proxy we need to know which
             * one was selected */
            node = lm_message_node_add_child (node, "streamhost-used", "");
            current_streamhost = priv->streamhosts->data;
            lm_message_node_set_attribute (node, "jid",
                current_streamhost->jid);

            _gabble_connection_send (priv->conn, iq_result, NULL);
            lm_message_unref (iq_result);
          }

        if (priv->read_blocked)
          {
            DEBUG ("reading has been blocked. Blocking now as the socks5 "
                "negotiation is done");
            gibber_transport_block_receiving (priv->transport, TRUE);
          }

        return 2;

      case SOCKS5_STATE_AWAITING_AUTH_REQUEST:
        /* A client connected to us and we are awaiting for the authorization
         * request (at least 2 bytes) */
        if (string->len < 2)
          return 0;

        if (string->str[0] != SOCKS5_VERSION)
          {
            DEBUG ("Authentication failed");

            socks5_error (self);
            return string->len;
          }

        /* The auth request string is SOCKS5_VERSION + # of methods + methods */
        auth_len = string->str[1] + 2;
        if (string->len < auth_len)
          /* We are still receiving some auth method */
          return 0;

        for (i = 2; i < auth_len; i++)
          {
            if (string->str[i] == SOCKS5_AUTH_NONE)
              {
                /* Authorize the connection */
                msg[0] = SOCKS5_VERSION;
                msg[1] = SOCKS5_AUTH_NONE;

                DEBUG ("Received auth request. Sending auth reply");
                write_to_transport (self, msg, 2, NULL);

                priv->socks5_state = SOCKS5_STATE_AWAITING_COMMAND;

                return auth_len;
              }
          }

        DEBUG ("Unauthenticated access is not supported by the streamhost");

        socks5_error (self);

        return auth_len;

      case SOCKS5_STATE_AWAITING_COMMAND:
        /* The client has been authorized and we are waiting for a command,
         * the only one supported by the SOCKS5 bytestreams XEP is
         * CONNECT with:
         *  - ATYP = DOMAIN
         *  - PORT = 0
         *  - DOMAIN = SHA1(sid + initiator + target)
         */
        if (string->len < SOCKS5_CONNECT_LENGTH)
          return 0;

        if (string->str[0] != SOCKS5_VERSION ||
            string->str[1] != SOCKS5_CMD_CONNECT ||
            string->str[2] != SOCKS5_RESERVED ||
            string->str[3] != SOCKS5_ATYP_DOMAIN ||
            string->str[4] != SHA1_LENGTH ||
            string->str[45] != 0 || /* first half of the port number */
            string->str[46] != 0) /* second half of the port number */
          {
            DEBUG ("Invalid SOCKS5 connect message");

            socks5_error (self);
            return string->len;
          }

        /* FIXME: check the domain type */

        domain = compute_domain(priv->stream_id, priv->self_full_jid,
            priv->peer_jid);

        msg[0] = SOCKS5_VERSION;
        msg[1] = SOCKS5_STATUS_OK;
        msg[2] = SOCKS5_RESERVED;
        msg[3] = SOCKS5_ATYP_DOMAIN;
        msg[4] = SHA1_LENGTH;
        /* Domain name: SHA-1(sid + initiator + target) */
        memcpy (&msg[5], domain, 40);
        /* Port: 0 */
        msg[45] = 0x00;
        msg[46] = 0x00;

        DEBUG ("Received CONNECT cmd. Sending CONNECT reply");
        write_to_transport (self, msg, 47, NULL);

        priv->socks5_state = SOCKS5_STATE_CONNECTED;

        /* Sock5 is connected but the bytestream is not open yet as we need
         * to wait for the IQ reply. Stop reading until the bytestream
         * is open to avoid data loss. */
        gibber_transport_block_receiving (priv->transport, TRUE);

        DEBUG ("sock5 stream connected. Stop to listen for connections");
        g_assert (priv->listener != NULL);
        g_object_unref (priv->listener);
        priv->listener = NULL;

        return SOCKS5_CONNECT_LENGTH;

      case SOCKS5_STATE_CONNECTED:
        /* We are connected, everything we receive now is data */
        g_signal_emit_by_name (G_OBJECT (self), "data-received",
            priv->peer_handle, string);

        return string->len;

      case SOCKS5_STATE_ERROR:
        /* An error occurred and the channel will be closed in an idle
         * callback, so let's just throw away the data we receive */
        DEBUG ("An error occurred, throwing away received data");
        return string->len;

      case SOCKS5_STATE_TRYING_CONNECT:
        DEBUG ("Impossible to receive data when not yet connected to the "
            "socket");
        break;

      case SOCKS5_STATE_INVALID:
        DEBUG ("Invalid SOCKS5 state");
        break;
    }

  g_assert_not_reached ();
  return string->len;
}

static void
transport_handler (GibberTransport *transport,
                   GibberBuffer *data,
                   gpointer user_data)

{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (user_data);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  gsize used_bytes;

  DEBUG ("got %" G_GSIZE_FORMAT " bytes from sock5 transport", data->length);

  g_string_append_len (priv->read_buffer, (const gchar *) data->data,
      data->length);

  do
    {
      /* socks5_handle_received_data() processes the data and returns the
       * number of bytes that have been used. 0 means that there is not enough
       * data to do anything, so we just wait for more data from the socket */
      used_bytes = socks5_handle_received_data (self, priv->read_buffer);

      if (priv->read_buffer == NULL)
        /* If something did wrong in socks5_handle_received_data, the
         * bytestream can be closed and so destroyed. */
        return;

      g_string_erase (priv->read_buffer, 0, used_bytes);
    }
  while (used_bytes > 0 && priv->read_buffer->len > 0);
}

static void
socks5_connect (GabbleBytestreamSocks5 *self)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  Streamhost* streamhost;
  GibberTCPTransport *transport;

  priv->socks5_state = SOCKS5_STATE_TRYING_CONNECT;

  if (priv->streamhosts != NULL)
    {
      streamhost = priv->streamhosts->data;
    }
  else
    {
      DEBUG ("No more streamhosts to try, closing");

      socks5_error (self);
      return;
    }

  DEBUG ("Trying streamhost %s on port %s", streamhost->host,
      streamhost->port);

  transport = gibber_tcp_transport_new ();
  set_transport (self, GIBBER_TRANSPORT (transport));
  g_object_unref (transport);

  gibber_tcp_transport_connect (transport, streamhost->host,
      streamhost->port);

  /* We'll send the auth request once the transport is connected */
}

/**
 * gabble_bytestream_socks5_add_streamhost
 *
 * Adds the streamhost as a candidate for connection.
 */
void
gabble_bytestream_socks5_add_streamhost (GabbleBytestreamSocks5 *self,
                                         LmMessageNode *streamhost_node)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  const gchar *zeroconf;
  const gchar *jid;
  const gchar *host;
  const gchar *port;
  Streamhost *streamhost;

  g_return_if_fail (!tp_strdiff (streamhost_node->name, "streamhost"));

  zeroconf = lm_message_node_get_attribute (streamhost_node, "zeroconf");
  if (zeroconf != NULL)
    {
      /* TODO: add suppport for zeroconf */
      DEBUG ("zeroconf streamhosts are not supported");
      return;
    }

  jid = lm_message_node_get_attribute (streamhost_node, "jid");
  if (jid == NULL)
    {
      DEBUG ("streamhost doesn't contain a JID");
      return;
    }

  host = lm_message_node_get_attribute (streamhost_node, "host");
  if (host == NULL)
    {
      DEBUG ("streamhost doesn't contain a host");
      return;
    }

  port = lm_message_node_get_attribute (streamhost_node, "port");
  if (port == NULL)
    {
      DEBUG ("streamhost doesn't contain a port");
      return;
    }

  DEBUG ("streamhost with jid %s, host %s and port %s added", jid, host,
      port);

  streamhost = streamhost_new (jid, host, port);
  priv->streamhosts = g_slist_append (priv->streamhosts, streamhost);
}

/**
 * gabble_bytestream_socks5_connect_to_streamhost
 *
 * Try to connect to a streamhost.
 */
void
gabble_bytestream_socks5_connect_to_streamhost (GabbleBytestreamSocks5 *self,
                                                LmMessage *msg)

{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  priv->msg_for_acknowledge_connection = lm_message_ref (msg);

  socks5_connect (self);
}

/*
 * gabble_bytestream_socks5_send
 *
 * Implements gabble_bytestream_iface_send on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_socks5_send (GabbleBytestreamIface *iface,
                               guint len,
                               const gchar *str)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (iface);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  GError *error = NULL;

  if (priv->bytestream_state != GABBLE_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->bytestream_state);
      return FALSE;
    }

  if (priv->write_blocked)
    {
      DEBUG ("sending data while the bytestream was blocked");
    }

  DEBUG ("send %u bytes through bytestream", len);
  if (!write_to_transport (self, str, len, &error))
    {
      DEBUG ("sending failed: %s", error->message);

      g_error_free (error);
      gabble_bytestream_iface_close (GABBLE_BYTESTREAM_IFACE (self), NULL);
      return FALSE;
    }

  /* If something did wrong during the writting, the transport has been closed
   * and so set to NULL. */
  if (priv->transport == NULL)
    return FALSE;

  if (!gibber_transport_buffer_is_empty (priv->transport))
    {
      /* We >don't want to send more data while the buffer isn't empty */
      DEBUG ("buffer isn't empty. Block write to the bytestream");
      change_write_blocked_state (self, TRUE);
    }

  return TRUE;
}

/*
 * gabble_bytestream_socks5_accept
 *
 * Implements gabble_bytestream_iface_accept on GabbleBytestreamIface
 */
static void
gabble_bytestream_socks5_accept (GabbleBytestreamIface *iface,
                                 GabbleBytestreamAugmentSiAcceptReply func,
                                 gpointer user_data)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (iface);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  LmMessage *msg;
  LmMessageNode *si;

  if (priv->bytestream_state != GABBLE_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      return;
    }

  msg = gabble_bytestream_factory_make_accept_iq (priv->peer_jid,
      priv->stream_init_id, NS_BYTESTREAMS);
  si = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  g_assert (si != NULL);

  if (func != NULL)
    {
      /* let the caller add his profile specific data */
      func (si, user_data);
    }

  if (_gabble_connection_send (priv->conn, msg, NULL))
    {
      DEBUG ("stream %s with %s is now accepted", priv->stream_id,
          priv->peer_jid);
      g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_ACCEPTED, NULL);
    }

  lm_message_unref (msg);
}

static void
gabble_bytestream_socks5_decline (GabbleBytestreamSocks5 *self,
                                  GError *error)
{
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  LmMessage *msg;

  g_return_if_fail (priv->bytestream_state ==
      GABBLE_BYTESTREAM_STATE_LOCAL_PENDING);

  msg = lm_message_build (priv->peer_jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "error",
      '@', "id", priv->stream_init_id,
      NULL);

  if (error != NULL && error->domain == GABBLE_XMPP_ERROR)
    {
      gabble_xmpp_error_to_node (error->code, msg->node, error->message);
    }
  else
    {
      gabble_xmpp_error_to_node (XMPP_ERROR_FORBIDDEN, msg->node,
          "Offer Declined");
    }

  _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);

  g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSED, NULL);
}

/*
 * gabble_bytestream_socks5_close
 *
 * Implements gabble_bytestream_iface_close on GabbleBytestreamIface
 */
static void
gabble_bytestream_socks5_close (GabbleBytestreamIface *iface,
                                GError *error)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (iface);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  if (priv->bytestream_state == GABBLE_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  if (priv->bytestream_state == GABBLE_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* Stream was created using SI so we decline the request */
      gabble_bytestream_socks5_decline (self, error);
    }
  else
    {
      g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSING, NULL);
      if (priv->transport != NULL &&
          !gibber_transport_buffer_is_empty (priv->transport))
        {
          DEBUG ("Wait transport buffer is empty before close the bytestream");
        }
      else
        {
          DEBUG ("Transport buffer is empty, we can close the bytestream");
          bytestream_closed (self);
        }
    }
}

static LmHandlerResult
socks5_init_reply_cb (GabbleConnection *conn,
                      LmMessage *sent_msg,
                      LmMessage *reply_msg,
                      GObject *obj,
                      gpointer user_data)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (obj);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_RESULT)
    {
      LmMessageNode *query, *streamhost = NULL;

      query = lm_message_node_get_child_with_namespace (reply_msg->node,
          "query", NS_BYTESTREAMS);

      if (query != NULL)
        streamhost = lm_message_node_get_child (query, "streamhost-used");

      if (streamhost == NULL)
        {
          DEBUG ("no streamhost-used has been defined. Closing the bytestream");
        }
      else
        {
          /* yeah, stream initiated */
          DEBUG ("Socks5 stream initiated using stream: %s",
              lm_message_node_get_attribute (streamhost, "jid"));
          g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_OPEN, NULL);
          /* We can read data from the sock5 socket now */
          gibber_transport_block_receiving (priv->transport, FALSE);
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  DEBUG ("error during Socks5 initiation");

  g_signal_emit_by_name (self, "connection-error");
  g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSED, NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/* get_local_interfaces_ips copied from Farsight 2 (function
 * fs_interfaces_get_local_ips in /gst-libs/gst/farsight/fs-interfaces.c).
 *   Copyright (C) 2006 Youness Alaoui <kakaroto@kakaroto.homelinux.net>
 *   Copyright (C) 2007 Collabora
 */
#ifdef HAVE_GETIFADDRS

static GList *
get_local_interfaces_ips (gboolean include_loopback)
{
  GList *ips = NULL;
  struct sockaddr_in *sa;
  struct ifaddrs *ifa, *results;
  gchar *loopback = NULL;

  if (getifaddrs (&results) < 0)
    return NULL;

  /* Loop through the interface list and get the IP address of each IF */
  for (ifa = results; ifa; ifa = ifa->ifa_next)
    {
      /* no ip address from interface that is down */
      if ((ifa->ifa_flags & IFF_UP) == 0)
        continue;

      if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
        continue;

      sa = (struct sockaddr_in *) ifa->ifa_addr;

      DEBUG ("Interface:  %s", ifa->ifa_name);
      DEBUG ("IP Address: %s", inet_ntoa (sa->sin_addr));
      if ((ifa->ifa_flags & IFF_LOOPBACK) == IFF_LOOPBACK)
        {
          if (include_loopback)
            loopback = g_strdup (inet_ntoa (sa->sin_addr));
          else
            DEBUG ("Ignoring loopback interface");
        }
      else
        {
          ips = g_list_append (ips, g_strdup (inet_ntoa (sa->sin_addr)));
        }
    }

  freeifaddrs (results);

  if (loopback)
    ips = g_list_append (ips, loopback);

  return ips;
}

#else /* ! HAVE_GETIFADDRS */

static GList *
get_local_interfaces_ips (gboolean include_loopback)
{
  GList *ips = NULL;
  gint sockfd;
  gint size = 0;
  struct ifreq *ifr;
  struct ifconf ifc;
  struct sockaddr_in *sa;
  gchar *loopback = NULL;

  if ((sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_IP)) < 0)
    {
      DEBUG ("Cannot open socket to retreive interface list");
      return NULL;
    }

  ifc.ifc_len = 0;
  ifc.ifc_req = NULL;

  /* Loop and get each interface the system has, one by one... */
  do
    {
      size += sizeof (struct ifreq);
      /* realloc buffer size until no overflow occurs  */
      if (NULL == (ifc.ifc_req = realloc (ifc.ifc_req, size)))
        {
          DEBUG ("Out of memory while allocation interface configuration"
              " structure");
          close (sockfd);
          return NULL;
        }
      ifc.ifc_len = size;

      if (ioctl (sockfd, SIOCGIFCONF, &ifc))
        {
          DEBUG ("ioctl SIOCFIFCONF");
          close (sockfd);
          free (ifc.ifc_req);
          return NULL;
        }
    } while  (size <= ifc.ifc_len);

  /* Loop throught the interface list and get the IP address of each IF */
  for (ifr = ifc.ifc_req;
      (gchar *) ifr < (gchar *) ifc.ifc_req + ifc.ifc_len;
      ++ifr)
    {

      if (ioctl (sockfd, SIOCGIFFLAGS, ifr))
        {
          DEBUG ("Unable to get IP information for interface %s. Skipping...",
              ifr->ifr_name);
          continue;  /* failed to get flags, skip it */
        }
      sa = (struct sockaddr_in *) &ifr->ifr_addr;
      DEBUG ("Interface:  %s", ifr->ifr_name);
      DEBUG ("IP Address: %s", inet_ntoa (sa->sin_addr));
      if ((ifr->ifr_flags & IFF_LOOPBACK) == IFF_LOOPBACK)
        {
          if (include_loopback)
            loopback = g_strdup (inet_ntoa (sa->sin_addr));
          else
            DEBUG ("Ignoring loopback interface");
        }
      else
        {
          ips = g_list_append (ips, g_strdup (inet_ntoa (sa->sin_addr)));
        }
    }

  close (sockfd);
  free (ifc.ifc_req);

  if (loopback)
    ips = g_list_append (ips, loopback);

  return ips;
}

#endif /* ! HAVE_GETIFADDRS */

static void
new_connection_cb (GibberListener *listener,
                   GibberTransport *transport,
                   struct sockaddr *addr,
                   guint size,
                   gpointer user_data)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (user_data);
  GabbleBytestreamSocks5Private *priv =
    GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  DEBUG ("New connection...");

  priv->socks5_state = SOCKS5_STATE_AWAITING_AUTH_REQUEST;
  set_transport (self, transport);
}

/*
 * gabble_bytestream_socks5_initiate
 *
 * Implements gabble_bytestream_iface_initiate on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_socks5_initiate (GabbleBytestreamIface *iface)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (iface);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);
  gchar *port;
  gint port_num;
  LmMessage *msg;
  GList *ips;
  GList *ip;

  if (priv->bytestream_state != GABBLE_BYTESTREAM_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d)",
          priv->bytestream_state);
      return FALSE;
    }

  g_assert (priv->listener == NULL);
  priv->listener = gibber_listener_new ();

  g_signal_connect (priv->listener, "new-connection",
      G_CALLBACK (new_connection_cb), self);

  if (!gibber_listener_listen_tcp (priv->listener, 0, NULL))
    {
      DEBUG ("can't listen for incoming connection");
      return FALSE;
    }

  port_num = gibber_listener_get_port (priv->listener);
  port = g_strdup_printf ("%d", port_num);

  msg = lm_message_build (priv->peer_jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "set",
      '(', "query", "",
        '@', "xmlns", NS_BYTESTREAMS,
        '@', "sid", priv->stream_id,
        '@', "mode", "tcp",
      ')', NULL);

  ips = get_local_interfaces_ips (FALSE);
  ip = ips;
  while (ip)
    {
      LmMessageNode *node = lm_message_node_add_child (msg->node->children,
          "streamhost", "");
      lm_message_node_set_attributes (node,
          "jid", priv->self_full_jid,
          "host", ip->data,
          "port", port,
          NULL);

      g_free (ip->data);
      ip = ip->next;
    }
  g_list_free (ips);
  g_free (port);

  /* FIXME: for now we support only direct connections, we should also
   * add support for external proxies to have more chances to make the
   * bytestream work */

  if (!_gabble_connection_send_with_reply (priv->conn, msg,
      socks5_init_reply_cb, G_OBJECT (self), NULL, NULL))
    {
      DEBUG ("Error when sending Socks5 init stanza");

      lm_message_unref (msg);
      return FALSE;
    }

  lm_message_unref (msg);

  return TRUE;
}

static void
gabble_bytestream_socks5_block_reading (GabbleBytestreamIface *iface,
                                        gboolean block)
{
  GabbleBytestreamSocks5 *self = GABBLE_BYTESTREAM_SOCKS5 (iface);
  GabbleBytestreamSocks5Private *priv =
      GABBLE_BYTESTREAM_SOCKS5_GET_PRIVATE (self);

  if (priv->read_blocked == block)
    return;

  priv->read_blocked = block;

  DEBUG ("%s the transport bytestream", block ? "block": "unblock");

  if (priv->transport != NULL)
    gibber_transport_block_receiving (priv->transport, block);
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleBytestreamIfaceClass *klass = (GabbleBytestreamIfaceClass *) g_iface;

  klass->initiate = gabble_bytestream_socks5_initiate;
  klass->send = gabble_bytestream_socks5_send;
  klass->close = gabble_bytestream_socks5_close;
  klass->accept = gabble_bytestream_socks5_accept;
  klass->block_reading = gabble_bytestream_socks5_block_reading;
}
