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

#include "gabble.h"
#include "handles.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"

#include "gabble-connection.h"
#include "gabble-connection-glue.h"
#include "gabble-connection-signals-marshal.h"

#define BUS_NAME        "org.freedesktop.Telepathy.Connection.gabble"
#define OBJECT_PATH     "/org/freedesktop/Telepathy/Connection/gabble"

G_DEFINE_TYPE(GabbleConnection, gabble_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_CHANNEL,
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

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__STRING_STRING_INT_INT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

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

  /* release any references held by the object here */

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

  bus = gabble_get_bus ();
  bus_proxy = gabble_get_bus_proxy ();
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

  dbus_g_connection_register_g_object (bus, priv->object_path, G_OBJECT (conn));

  *bus_name = g_strdup (priv->bus_name);
  *object_path = g_strdup (priv->object_path);

  return TRUE;
}


static LmSSLResponse connection_ssl_cb (LmSSL*, LmSSLStatus, gpointer);
static void connection_open_cb (LmConnection*, gboolean, gpointer);
static void connection_auth_cb (LmConnection*, gboolean, gpointer);

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

      priv->conn = lm_connection_new (priv->connect_server);
      lm_connection_set_port (priv->conn, priv->port);

      jid = g_strdup_printf ("@%s", priv->stream_server);
      lm_connection_set_jid (priv->conn, jid);
      g_free (jid);

      if (priv->old_ssl)
        {
          LmSSL *ssl = lm_ssl_new (NULL, connection_ssl_cb, conn, NULL);
          lm_connection_set_ssl (priv->conn, ssl);
          lm_ssl_unref (ssl);
        }
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
  return TRUE;
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
gboolean gabble_connection_list_channels (GabbleConnection *obj, gpointer* ret, GError **error)
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
  return TRUE;
}

