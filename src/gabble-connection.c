/*
 * gabble-connection.c - Source for GabbleConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include <dbus/dbus-glib.h>
#include <loudmouth/loudmouth.h>
#include <string.h>
#include <time.h>

#include "gabble-im-channel.h"
#include "handles.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-connection.h"
#include "gabble-connection-glue.h"
#include "gabble-connection-signals-marshal.h"

#define BUS_NAME        "org.freedesktop.Telepathy.Connection.gabble"
#define OBJECT_PATH     "/org/freedesktop/Telepathy/Connection/gabble"

G_DEFINE_TYPE(GabbleConnection, gabble_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CAPABILITIES_CHANGED,
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    STATUS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
    PROP_PROTOCOL = 1,
    PROP_CONNECT_SERVER,
    PROP_PORT,
    PROP_OLD_SSL,
    PROP_STREAM_SERVER,
    PROP_USERNAME,
    PROP_PASSWORD,
    PROP_RESOURCE,
    LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;

struct _GabbleConnectionPrivate
{
  LmConnection *conn;
  LmMessageHandler *message_cb;

  /* telepathy properties */
  char *protocol;

  /* connection properties */
  char *connect_server;
  guint port;
  gboolean old_ssl;

  /* authentication properties */
  char *stream_server;
  char *username;
  char *password;
  char *resource;

  /* dbus object location */
  char *bus_name;
  char *object_path;

  /* connection status */
  TpConnectionStatus status;

  /* handles */
  GabbleHandleRepo *handles;
  GabbleHandle self_handle;

  /* channels */
  GHashTable *im_channels;

  /* gobject housekeeping */
  gboolean dispose_has_run;
};

#define GABBLE_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate))

static void
gabble_connection_init (GabbleConnection *obj)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  priv->port = 5222;
  priv->resource = g_strdup ("Telepathy");

  priv->handles = gabble_handle_repo_new ();

  priv->im_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                             NULL, g_object_unref);
}

/* static GObject*
gabble_connection_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
  GObject *object;
  GabbleConnection *self;
  GabbleConnectionPrivate *priv;
  char *server;

  object = G_OBJECT_CLASS (gabble_connection_parent_class)->constructor (type, n_construct_properties, construct_properties);
  self = GABBLE_CONNECTION (object);
  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  server = priv->account;
  while (*server && *server != '@')
    server++;
  server++;
  g_assert (*server != '\0');

  priv->conn = lm_connection_new (server);
} */

static void
gabble_connection_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleConnection *self = (GabbleConnection *) object;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  switch (property_id) {
    case PROP_PROTOCOL:
      g_value_set_string (value, priv->protocol);
      break;
    case PROP_CONNECT_SERVER:
      g_value_set_string (value, priv->connect_server);
      break;
    case PROP_STREAM_SERVER:
      g_value_set_string (value, priv->stream_server);
      break;
    case PROP_PORT:
      g_value_set_uint (value, priv->port);
      break;
    case PROP_OLD_SSL:
      g_value_set_boolean (value, priv->old_ssl);
      break;
    case PROP_USERNAME:
      g_value_set_string (value, priv->username);
      break;
    case PROP_PASSWORD:
      g_value_set_string (value, priv->password);
      break;
    case PROP_RESOURCE:
      g_value_set_string (value, priv->resource);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_connection_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GabbleConnection *self = (GabbleConnection *) object;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  switch (property_id) {
    case PROP_PROTOCOL:
      if (priv->protocol)
        g_free (priv->protocol);

      priv->protocol = g_value_dup_string (value);
      break;
    case PROP_CONNECT_SERVER:
      if (priv->connect_server)
        g_free (priv->connect_server);

      priv->connect_server = g_value_dup_string (value);
      break;
    case PROP_PORT:
      priv->port = g_value_get_uint (value);
      break;
    case PROP_OLD_SSL:
      priv->old_ssl = g_value_get_boolean (value);
      break;
    case PROP_STREAM_SERVER:
      if (priv->stream_server);
        g_free (priv->stream_server);

      priv->stream_server = g_value_dup_string (value);
      break;
    case PROP_USERNAME:
      if (priv->username);
        g_free (priv->username);

      priv->username = g_value_dup_string (value);
      break;
   case PROP_PASSWORD:
      if (priv->password)
        g_free (priv->password);

      priv->password = g_value_dup_string (value);
      break;
    case PROP_RESOURCE:
      if (priv->resource)
        g_free (priv->resource);

      priv->resource = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_connection_dispose (GObject *object);
static void gabble_connection_finalize (GObject *object);

static void
gabble_connection_class_init (GabbleConnectionClass *gabble_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_class);
  GParamSpec *param_spec;

  /* not required currently:
  object_class->constructor = gabble_connection_constructor; */

  object_class->get_property = gabble_connection_get_property;
  object_class->set_property = gabble_connection_set_property;

  g_type_class_add_private (gabble_connection_class, sizeof (GabbleConnectionPrivate));

  object_class->dispose = gabble_connection_dispose;
  object_class->finalize = gabble_connection_finalize;

  param_spec = g_param_spec_string ("protocol", "Telepathy identifier for protocol",
                                    "Identifier string used when the protocol "
                                    "name is required. Unused internally.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PROTOCOL, param_spec);

  param_spec = g_param_spec_string ("connect-server", "Hostname or IP of Jabber server",
                                    "The server used when establishing a connection.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECT_SERVER, param_spec);

  param_spec = g_param_spec_uint ("port", "Jabber server port",
                                  "The port used when establishing a connection.",
                                  0, G_MAXUINT16, 5222,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PORT, param_spec);

  param_spec = g_param_spec_boolean ("old-ssl", "Old-style SSL tunneled connection",
                                     "Establish the entire connection to the server "
                                     "within an SSL-encrypted tunnel. Note that this "
                                     "is not the same as connecting with TLS, which "
                                     "is not yet supported.", FALSE,
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OLD_SSL, param_spec);

  param_spec = g_param_spec_string ("stream-server", "The server name used to initialise the stream.",
                                    "The server name used when initialising the stream, "
                                    "which is usually the part after the @ in the user's JID.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_SERVER, param_spec);

  param_spec = g_param_spec_string ("username", "Jabber username",
                                    "The username used when authenticating.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

  param_spec = g_param_spec_string ("password", "Jabber password",
                                    "The password used when authenticating.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

  param_spec = g_param_spec_string ("resource", "Jabber resource",
                                    "The Jabber resource used when authenticating.",
                                    "Telepathy",
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_RESOURCE, param_spec);

  signals[CAPABILITIES_CHANGED] =
    g_signal_new ("capabilities-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_BOXED_BOXED,
                  G_TYPE_NONE, 3, G_TYPE_UINT, (dbus_g_type_get_collection ("GPtrArray", G_TYPE_VALUE_ARRAY)), (dbus_g_type_get_collection ("GPtrArray", G_TYPE_VALUE_ARRAY)));

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__STRING_STRING_INT_INT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[PRESENCE_UPDATE] =
    g_signal_new ("presence-update",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, G_VALUE_ARRAY)));

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_connection_class), &dbus_glib_gabble_connection_object_info);
}

void
gabble_connection_dispose (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->conn)
    {
      if (lm_connection_is_open (priv->conn))
        lm_connection_close (priv->conn, NULL);

      lm_connection_unregister_message_handler (priv->conn, priv->message_cb,
                                                LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (priv->message_cb);
    }

  if (G_OBJECT_CLASS (gabble_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_parent_class)->dispose (object);
}

void
gabble_connection_finalize (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  if (priv->conn)
    lm_connection_unref (priv->conn);

  if (priv->protocol)
    g_free (priv->protocol);

  if (priv->connect_server)
    g_free (priv->connect_server);

  if (priv->stream_server)
    g_free (priv->stream_server);

  if (priv->username)
    g_free (priv->username);

  if (priv->password)
    g_free (priv->password);

  if (priv->resource)
    g_free (priv->resource);

  if (priv->bus_name)
    g_free (priv->bus_name);

  if (priv->object_path)
    g_free (priv->object_path);

  if (priv->handles);
    gabble_handle_repo_destroy (priv->handles);

  G_OBJECT_CLASS (gabble_connection_parent_class)->finalize (object);
}

/**
 * _gabble_connection_set_properties_from_account
 *
 * Parses an account string which may be one of the following forms:
 *  username
 *  username/resource
 *  username@server
 *  username@server/resource
 * and sets the properties for username, stream server and resource
 * appropriately. Also sets the connect server to the stream server if one has
 * not yet been specified.
 */
void
_gabble_connection_set_properties_from_account (GabbleConnection *conn,
                                                const char       *account)
{
  GabbleConnectionPrivate *priv;
  char *username, *server, *resource;

  g_assert (GABBLE_IS_CONNECTION (conn));
  g_assert (account != NULL);
  g_assert (*account != '\0');

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  gabble_handle_decode_jid (account, &username, &server, &resource);

  g_object_set (G_OBJECT (conn),
                "username", username,
                "stream-server", server,
                NULL);

  /* only override the default resource if we actually got one */
  if (resource)
    g_object_set (G_OBJECT (conn), "resource", resource, NULL);

  /* only set the connect server if one hasn't already been specified */
  if (!priv->connect_server)
    g_object_set (G_OBJECT (conn), "connect-server", server, NULL);

  g_free (username);
  g_free (server);
  g_free (resource);
}

/**
 * _gabble_connection_register
 *
 * Make the connection object appear on the bus, returning the bus
 * name and object path used.
 */
gboolean
_gabble_connection_register (GabbleConnection *conn,
                             gchar           **bus_name,
                             gchar           **object_path,
                             GError          **error)
{
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GabbleConnectionPrivate *priv;
  const char *allowed_chars = "_1234567890"
                              "abcdefghijklmnopqrstuvwxyz"
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  char *safe_proto;
  char *unique_name;
  guint request_name_result;

  g_assert (GABBLE_IS_CONNECTION (conn));

  bus = tp_get_bus ();
  bus_proxy = tp_get_bus_proxy ();
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  safe_proto = g_strdup (priv->protocol);
  g_strcanon (safe_proto, allowed_chars, '_');

  unique_name = g_strdup_printf ("%s_%s_%s",
                                 priv->username,
                                 priv->stream_server,
                                 priv->resource);
  g_strcanon (unique_name, allowed_chars, '_');

  priv->bus_name = g_strdup_printf (BUS_NAME ".%s.%s",
                                    safe_proto,
                                    unique_name);
  priv->object_path = g_strdup_printf (OBJECT_PATH "/%s/%s",
                                       safe_proto,
                                       unique_name);

  g_free (safe_proto);
  g_free (unique_name);

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", error,
                          G_TYPE_STRING, priv->bus_name,
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    return FALSE;

  if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS)
    g_error ("Failed to acquire bus name, connection manager already running?");

  g_debug ("_gabble_connection_register: bus name %s", priv->bus_name);

  dbus_g_connection_register_g_object (bus, priv->object_path, G_OBJECT (conn));

  g_debug ("_gabble_connection_register: object path %s", priv->object_path);

  *bus_name = g_strdup (priv->bus_name);
  *object_path = g_strdup (priv->object_path);

  return TRUE;
}

/**
 * _gabble_connection_get_handles
 *
 * Return the handle repo for a connection.
 */
GabbleHandleRepo *
_gabble_connection_get_handles (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  return priv->handles;
}

/**
 * _gabble_connection_send
 *
 * Send an LmMessage and trap network errors appropriately.
 */
gboolean
_gabble_connection_send (GabbleConnection *conn, LmMessage *msg, GError **error)
{
  GabbleConnectionPrivate *priv;
  GError *lmerror = NULL;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (!lm_connection_send (priv->conn, msg, &lmerror))
    {
      g_error ("_gabble_connection_send failed: %s", lmerror->message);

      *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                            "message send failed: %s", lmerror->message);

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
}

static LmHandlerResult connection_message_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmSSLResponse connection_ssl_cb (LmSSL*, LmSSLStatus, gpointer);
static void connection_open_cb (LmConnection*, gboolean, gpointer);
static void connection_auth_cb (LmConnection*, gboolean, gpointer);
static GabbleIMChannel *new_im_channel (GabbleConnection *conn, GabbleHandle handle, gboolean supress_handler);

/**
 * _gabble_connection_connect
 *
 * Use the stored server & authentication details to commence
 * the stages for connecting to the server and authenticating. Will
 * re-use an existing LmConnection if it is present, or create it
 * if necessary.
 *
 * Stage 1 is _gabble_connection_connect calling lm_connection_open
 * Stage 2 is connection_open_cb calling lm_connection_auth
 * Stage 3 is connection_auth_cb advertising initial presence and
 *  setting the CONNECTED state
 */
gboolean
_gabble_connection_connect (GabbleConnection *conn,
                            GError          **error)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GError *lmerror = NULL;

  g_assert (priv->connect_server != NULL);
  g_assert (priv->port > 0 && priv->port <= G_MAXUINT16);
  g_assert (priv->stream_server != NULL);
  g_assert (priv->username != NULL);
  g_assert (priv->password != NULL);
  g_assert (priv->resource != NULL);

  if (priv->conn == NULL)
    {
      char *jid;
      gboolean valid;

      priv->conn = lm_connection_new (priv->connect_server);
      lm_connection_set_port (priv->conn, priv->port);

      jid = g_strdup_printf ("%s@%s", priv->username, priv->stream_server);
      lm_connection_set_jid (priv->conn, jid);

      priv->self_handle = gabble_handle_for_contact (priv->handles,
                                                     jid, FALSE);
      valid = gabble_handle_ref (priv->handles,
                                 TP_HANDLE_TYPE_CONTACT,
                                 priv->self_handle);
      g_assert (valid);

      g_free (jid);

      if (priv->old_ssl)
        {
          LmSSL *ssl = lm_ssl_new (NULL, connection_ssl_cb, conn, NULL);
          lm_connection_set_ssl (priv->conn, ssl);
          lm_ssl_unref (ssl);
        }

      priv->message_cb = lm_message_handler_new (connection_message_cb,
                                                 conn, NULL);
      lm_connection_register_message_handler (priv->conn, priv->message_cb,
                                              LM_MESSAGE_TYPE_MESSAGE,
                                              LM_HANDLER_PRIORITY_NORMAL);
    }
  else
    {
      g_assert (lm_connection_is_open (priv->conn) == FALSE);
    }

  if (!lm_connection_open (priv->conn, connection_open_cb,
                           conn, NULL, &lmerror))
    {
      g_debug ("lm_connection_open failed: %s", lmerror->message);

      *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                            "lm_connection_open_failed: %s", lmerror->message);

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
}

/**
 * connection_status_change
 *
 * Emit a signal for the status being changed, and update it in the
 * object.
 */
static void
connection_status_change (GabbleConnection        *conn,
                          TpConnectionStatus       status,
                          TpConnectionStatusReason reason)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("connection_status_change: status %u reason %u", status, reason);

  priv->status = status;

  g_signal_emit (conn, signals[STATUS_CHANGED], 0, status, reason);
}


/**
 * connection_message_cb
 *
 * Called by loudmouth when we get an incoming <message>.
 */
static LmHandlerResult
connection_message_cb (LmMessageHandler *handler,
                       LmConnection *connection,
                       LmMessage *message,
                       gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *msg_node, *body_node;
  const char *from, *body;
  GabbleHandle handle;
  GabbleIMChannel *chan;
  time_t stamp;

  g_assert (connection == priv->conn);

  msg_node = lm_message_get_node (message);
  from = lm_message_node_get_attribute (msg_node, "from");
  body_node = lm_message_node_get_child (msg_node, "body");

  if (from == NULL || body_node == NULL)
    {
      char *tmp = lm_message_node_to_string (msg_node);
      g_debug ("connection_message_cb: got a message without a from and a body, ignoring:\n%s", tmp);
      g_free (tmp);

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  body = lm_message_node_get_value (body_node);
  handle = gabble_handle_for_contact (priv->handles, from, FALSE);

  g_debug ("connection_message_cb: message from %s (handle %u), body:\n%s",
           from, handle, body);

  chan = g_hash_table_lookup (priv->im_channels, GINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      g_debug ("connection_message_cb: found no channel, creating one");

      chan = new_im_channel (conn, handle, FALSE);
    }

  stamp = time (NULL);

  /* TODO: correctly parse timestamp of delayed messages */

  if (_gabble_im_channel_receive (chan, TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
                                  handle, stamp, body))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/**
 * connection_ssl_cb
 *
 * If we're doing old SSL, this function gets called if the certificate
 * is dodgy.
 */
static LmSSLResponse
connection_ssl_cb (LmSSL      *lmssl,
                   LmSSLStatus status,
                   gpointer    data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  const char *reason;
  LmSSLResponse response = LM_SSL_RESPONSE_STOP;

  switch (status) {
    case LM_SSL_STATUS_NO_CERT_FOUND:
      reason = "The server doesn't provide a certificate.";
      response = LM_SSL_RESPONSE_CONTINUE;
      break;
    case LM_SSL_STATUS_UNTRUSTED_CERT:
      reason = "The certificate can not be trusted.";
      response = LM_SSL_RESPONSE_CONTINUE;
      break;
    case LM_SSL_STATUS_CERT_EXPIRED:
      reason = "The certificate has expired.";
      break;
    case LM_SSL_STATUS_CERT_NOT_ACTIVATED:
      reason = "The certificate has not been activated.";
      break;
    case LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH:
      reason = "The server hostname doesn't match the one in the certificate.";
      break;
    case LM_SSL_STATUS_CERT_FINGERPRINT_MISMATCH:
      reason = "The fingerprint doesn't match the expected value.";
      break;
    case LM_SSL_STATUS_GENERIC_ERROR:
      reason = "An unknown SSL error occured.";
      break;
    default:
      g_assert_not_reached();
  }

  g_debug ("connection_ssl_cb called: %s", reason);

  if (response == LM_SSL_RESPONSE_CONTINUE)
    g_debug ("proceeding anyway!");
  else
    connection_status_change (conn, TP_CONNECTION_STATUS_DISCONNECTED,
                              TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR);

  return response;
}

/**
 * connection_open_cb
 *
 * Stage 2 of connecting, this function is called by loudmouth after the
 * result of the non-blocking lm_connection_open call is known. It makes
 * a request to authenticate the user with the server.
 */
static void
connection_open_cb (LmConnection *lmconn,
                    gboolean      success,
                    gpointer      data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GError *error = NULL;

  g_assert (priv);
  g_assert (lmconn == priv->conn);

  if (!success)
    {
      g_debug ("connection_open_cb failed");

      connection_status_change (conn, TP_CONNECTION_STATUS_DISCONNECTED,
                                TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

      return;
    }

  g_debug ("authenticating with username: %s, password: %s, resource: %s",
           priv->username, priv->password, priv->resource);

  if (!lm_connection_authenticate (lmconn, priv->username, priv->password,
                                   priv->resource, connection_auth_cb,
                                   conn, NULL, &error))
    {
      g_debug ("lm_connection_authenticate failed: %s", error->message);
      g_error_free (error);

      /* the reason this function can fail is through network errors,
       * authentication failures are reported to our auth_cb */
      connection_status_change (conn, TP_CONNECTION_STATUS_DISCONNECTED,
                                TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
    }
}

/**
 * connection_auth_cb
 *
 * Stage 3 of connecting, this function is called by loudmouth after the
 * result of the non-blocking lm_connection_auth call is known. It sends
 * the user's initial presence to the server, marking them as available.
 */
static void
connection_auth_cb (LmConnection *lmconn,
                    gboolean      success,
                    gpointer      data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessage *message;
  GError *error = NULL;

  g_assert (priv);
  g_assert (lmconn == priv->conn);

  if (!success)
    {
      g_debug ("connection_auth_cb failed");

      connection_status_change (conn, TP_CONNECTION_STATUS_DISCONNECTED,
                                TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);

      return;
    }

  message = lm_message_new (NULL, LM_MESSAGE_TYPE_PRESENCE);

  if (!lm_connection_send (lmconn, message, &error))
    {
      g_debug ("lm_connection_send of initial presence failed: %s",
               error->message);
      g_error_free (error);

      connection_status_change (conn, TP_CONNECTION_STATUS_DISCONNECTED,
                                TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
    }
  else
    {
      connection_status_change (conn, TP_CONNECTION_STATUS_CONNECTED,
                                TP_CONNECTION_STATUS_REASON_REQUESTED);
    }

  lm_message_unref (message);
}

/**
 * new_im_channel
 */
static GabbleIMChannel *
new_im_channel (GabbleConnection *conn, GabbleHandle handle, gboolean supress_handler)
{
  GabbleConnectionPrivate *priv;
  GabbleIMChannel *chan;
  char *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  object_path = g_strdup_printf ("%s/ImChannel%u", priv->object_path, handle);

  chan = g_object_new (GABBLE_TYPE_IM_CHANNEL,
                       "connection", conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  g_debug ("new_im_channel: object path %s", object_path);

  g_hash_table_insert (priv->im_channels, GINT_TO_POINTER (handle), chan);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_TEXT,
                 TP_HANDLE_TYPE_CONTACT, handle,
                 supress_handler);

  g_free (object_path);

  return chan;
}

/**
 * gabble_connection_add_status
 *
 * Implements DBus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_add_status (GabbleConnection *obj, const gchar * status, GHashTable * parms, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_advertise_capabilities
 *
 * Implements DBus method AdvertiseCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_advertise_capabilities (GabbleConnection *obj, const gchar ** add, const gchar ** remove, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_clear_status
 *
 * Implements DBus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_clear_status (GabbleConnection *obj, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_disconnect
 *
 * Implements DBus method Disconnect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_disconnect (GabbleConnection *obj, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  connection_status_change (obj, TP_CONNECTION_STATUS_DISCONNECTED,
                            TP_CONNECTION_STATUS_REASON_REQUESTED);

  lm_connection_close (priv->conn, NULL);

  return TRUE;
}


/**
 * gabble_connection_get_capabilities
 *
 * Implements DBus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_capabilities (GabbleConnection *obj, guint handle, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_interfaces (GabbleConnection *obj, gchar *** ret, GError **error)
{
  const char *interfaces[] = { TP_IFACE_CONN_INTERFACE, NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_connection_get_protocol
 *
 * Implements DBus method GetProtocol
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_protocol (GabbleConnection *obj, gchar ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  *ret = g_strdup (priv->protocol);

  return TRUE;
}


/**
 * gabble_connection_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_self_handle (GabbleConnection *obj, guint* ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  *ret = priv->self_handle;

  return TRUE;
}


/**
 * gabble_connection_get_status
 *
 * Implements DBus method GetStatus
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_status (GabbleConnection *obj, guint* ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  *ret = priv->status;

  return TRUE;
}


/**
 * gabble_connection_get_statuses
 *
 * Implements DBus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_statuses (GabbleConnection *obj, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_hold_handle
 *
 * Implements DBus method HoldHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_hold_handle (GabbleConnection *obj, guint handle_type, guint handle, GError **error)
{
  GabbleConnectionPrivate *priv;
  gboolean valid;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!gabble_handle_type_is_valid (handle_type))
    {
      g_debug ("hold_handle: invalid handle type %u", handle_type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid handle type %u", handle_type);

      return FALSE;
    }

  valid = gabble_handle_ref (priv->handles, handle_type, handle);

  if (!valid)
    {
      g_debug ("hold_handle: unknown handle %u", handle);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                            "unknown handle %u", handle);

      return FALSE;
    }

  return TRUE;
}


/**
 * gabble_connection_inspect_handle
 *
 * Implements DBus method InspectHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_inspect_handle (GabbleConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;
  const char *tmp;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!gabble_handle_type_is_valid (handle_type))
    {
      g_debug ("inspect_handle: invalid handle type %u", handle_type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid handle type %u", handle_type);

      return FALSE;
    }

  tmp = gabble_handle_inspect (priv->handles, handle_type, handle);

  if (tmp == NULL)
    {
      g_debug ("inspect_handle: invalid handle %u", handle);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                            "unknown handle %u", handle);

      return FALSE;
    }

  *ret = g_strdup (tmp);

  return TRUE;
}


/**
 * list_channel_hash_foreach
 *
 * Called by the exported ListChannels function, this should iterate over
 * the handle/channel pairs in a hash, and to the GPtrArray in the
 * ListChannelInfo struct, add a GValueArray containing the following:
 *  a D-Bus object path for the channel object on this service
 *  a D-Bus interface name representing the channel type
 *  an integer representing the handle type this channel communicates with, or zero
 *  an integer handle representing the contact, room or list this channel communicates with, or zero
 */
static void
list_channel_hash_foreach (gpointer key,
                           gpointer value,
                           gpointer data)
{
  GObject *channel = G_OBJECT (value);
  GPtrArray *channels = (GPtrArray *) data;
  char *path, *type;
  guint handle_type, handle;
  GValueArray *vals;

  g_object_get (channel, "object-path", &path,
                         "channel-type", &type,
                         "handle-type", &handle_type,
                         "handle", &handle, NULL);

  g_debug ("list_channels_foreach_hash: adding path %s, type %s, "
           "handle type %u, handle %u", path, type, handle_type, handle);

  vals = g_value_array_new (4);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 0), DBUS_TYPE_G_OBJECT_PATH);
  g_value_set_boxed (g_value_array_get_nth (vals, 0), path);
  g_free (path);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_STRING);
  g_value_set_string (g_value_array_get_nth (vals, 1), type);
  g_free (type);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (vals, 2), handle_type);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 3), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (vals, 3), handle);

  g_ptr_array_add (channels, vals);
}


/**
 * gabble_connection_list_channels
 *
 * Implements DBus method ListChannels
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_list_channels (GabbleConnection *obj, GPtrArray ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;
  guint count;
  GPtrArray *channels;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  count = g_hash_table_size (priv->im_channels);
  channels = g_ptr_array_sized_new (count);
  dbus_g_collection_set_signature (channels, "(osuu)");

  g_hash_table_foreach (priv->im_channels, list_channel_hash_foreach, channels);

  *ret = channels;

  return TRUE;
}


/**
 * gabble_connection_release_handle
 *
 * Implements DBus method ReleaseHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_release_handle (GabbleConnection *obj, guint handle_type, guint handle, GError **error)
{
  GabbleConnectionPrivate *priv;
  gboolean valid;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!gabble_handle_type_is_valid (handle_type))
    {
      g_debug ("release_handle: invalid handle type %u", handle_type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid handle type %u", handle_type);

      return FALSE;
    }

  valid = gabble_handle_is_valid (priv->handles, handle_type, handle);

  if (!valid)
    {
      g_debug ("release_handle: invalid handle %u", handle);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                            "unknown handle %u", handle);

      return FALSE;
    }

  /* TODO: this method is currently a no-op!
   * we need a per-client list of handles. */

  return TRUE;
}


/**
 * gabble_connection_remove_status
 *
 * Implements DBus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_remove_status (GabbleConnection *obj, const gchar * status, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_request_channel
 *
 * Implements DBus method RequestChannel
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_channel (GabbleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean supress_handler, gchar ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      GabbleIMChannel *chan;

      if (handle_type != TP_HANDLE_TYPE_CONTACT)
        goto NOT_AVAILABLE;

      if (!gabble_handle_is_valid (priv->handles,
                                   TP_HANDLE_TYPE_CONTACT,
                                   handle))
        goto INVALID_HANDLE;

      chan = new_im_channel (obj, handle, supress_handler);

      g_object_get (chan, "object-path", ret, NULL);
    }
  else
    {
      goto NOT_IMPLEMENTED;
    }

  return TRUE;

NOT_AVAILABLE:
  g_debug ("request_channel: requested channel is unavailable with "
           "handle type %u", handle_type);

  *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                        "requested channel is not available with "
                        "handle type %u", handle_type);

  return FALSE;

INVALID_HANDLE:
  g_debug ("request_channel: handle %u (type %u) not valid", handle, handle_type);

  *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                        "handle %u (type %u) not valid", handle, handle_type);

  return FALSE;

NOT_IMPLEMENTED:
  g_debug ("request_channel: unsupported channel type %s", type);

  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "unsupported channel type %s", type);

  return FALSE;
}


/**
 * gabble_connection_request_handle
 *
 * Implements DBus method RequestHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_handle (GabbleConnection *obj, guint handle_type, const gchar * name, guint* ret, GError **error)
{
  GabbleConnectionPrivate *priv;
  GabbleHandle handle;
  gboolean valid;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!gabble_handle_type_is_valid (handle_type))
    {
      g_debug ("request_handle: invalid handle type %u", handle_type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid handle type %u", handle_type);

      return FALSE;
    }

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      if (!strchr (name, '@'))
        {
          g_debug ("request_handle: requested handle %s has no @ in", name);

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                                "requested handle %s has no @ in", name);

          return FALSE;
        }
      else
        {
          handle = gabble_handle_for_contact (priv->handles, name, FALSE);
        }
      break;
      /* TODO: list handles */
/*    case TP_HANDLE_TYPE_LIST:
      g_assert_not_reached ();
      break; */
    default:
      g_debug ("request_handle: unimplemented handle type %u", handle_type);

      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                          "unimplemented handle type %u", handle_type);

      return FALSE;
    }

  /* TODO: this should use a per-client list of handles */
  valid = gabble_handle_ref (priv->handles, handle_type, handle);
  g_assert (valid);

  *ret = handle;

  return TRUE;
}


/**
 * gabble_connection_request_presence
 *
 * Implements DBus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_presence (GabbleConnection *obj, const GArray * contacts, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_set_last_activity_time
 *
 * Implements DBus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_set_last_activity_time (GabbleConnection *obj, guint time, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_set_status
 *
 * Implements DBus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_set_status (GabbleConnection *obj, GHashTable * statuses, GError **error)
{
  return TRUE;
}

