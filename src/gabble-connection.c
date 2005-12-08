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

#include "gabble-connection.h"
#include "gabble-connection-signals-marshal.h"

#include "gabble-connection-glue.h"

#include "telepathy-errors.h"

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
    PROP_SERVER = 1,
    PROP_PORT,
    PROP_ACCOUNT,
    PROP_PASSWORD,
    LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;

struct _GabbleConnectionPrivate
{
  LmConnection *conn;
  char *server;
  guint port;
  char *account;
  char *password;
  char *resource;
  gboolean dispose_has_run;
};

#define GABBLE_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate))

static void
gabble_connection_init (GabbleConnection *obj)
{
  /* GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (obj); */

  /* allocate any data required by the object here */
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
    case PROP_SERVER:
      g_value_set_string (value, priv->server);
      break;
    case PROP_PORT:
      g_value_set_uint (value, priv->port);
      break;
    case PROP_ACCOUNT:
      g_value_set_string (value, priv->account);
      break;
    case PROP_PASSWORD:
      g_value_set_string (value, priv->password);
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
    case PROP_SERVER:
      /* an explicitly set server should override one from the account */
      if (priv->server)
        g_free (priv->server);

      priv->server = g_value_dup_string (value);
      break;
    case PROP_PORT:
      g_assert (g_value_get_uint (value) != 0);
      g_assert (priv->port == 0);
      priv->port = g_value_get_uint (value);
      break;
    case PROP_ACCOUNT:
      {
        char *resource;

        g_assert (priv->account == NULL);
        g_assert (priv->resource == NULL);

        priv->account = g_value_dup_string (value);

        /* if the account contains a /, the resource follows it, but
         * we null the / because we want the account without it */
        resource = strchr(priv->account, '/');
        if (resource)
          {
            *resource = '\0';
            resource++;
            priv->resource = g_strdup (resource);
          }
        else
          priv->resource = g_strdup ("Telepathy");

        /* if the account contains an @ the server follows it,
         * unless server has already been set directly */
        if (priv->server == NULL)
          {
            char *server = strchr(priv->account, '@');

            if (server)
              {
                server++;
                priv->server = g_strdup (server);
              }
          }

        break;
      }
    case PROP_PASSWORD:
      g_assert (priv->password == NULL);
      priv->password = g_value_dup_string (value);
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

  param_spec = g_param_spec_string ("server", "Jabber server name",
                                    "The server used when establishing a connection, if one is not specified as part of the account.",
                                    "",
                                    G_PARAM_READWRITE |
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SERVER, param_spec);

  param_spec = g_param_spec_uint ("port", "Jabber server port",
                                  "The port used when establishing a connection.",
                                  0, G_MAXUINT16, 5222,
                                  G_PARAM_READWRITE |
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PORT, param_spec);

  param_spec = g_param_spec_string ("account", "Jabber account",
                                    "The JID used when establishing a connection.",
                                    "",
                                    G_PARAM_READWRITE |
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ACCOUNT, param_spec);

  param_spec = g_param_spec_string ("password", "Jabber password",
                                    "The password used when establishing a connection.",
                                    "",
                                    G_PARAM_READWRITE |
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__BOXED_STRING_INT_INT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_PROXY, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

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
  /* GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self); */

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_connection_parent_class)->finalize (object);
}

static void connection_open_cb(LmConnection*, gboolean, gpointer);
static void connection_auth_cb(LmConnection*, gboolean, gpointer);

/**
 * _gabble_connection_connect
 *
 * Use the stored account, server & authentication details to commence
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

  if (priv->conn == NULL)
    {
      priv->conn = lm_connection_new (priv->server);
    }
  else
    {
      g_assert (lm_connection_is_open (priv->conn) == FALSE);
    }

  if (!lm_connection_open (priv->conn, connection_open_cb,
                           conn, NULL, &lmerror))
    {
      g_debug ("lm_connection_open failed: %s", lmerror->message);

      *error = g_error_new(TELEPATHY_ERRORS, NetworkError,
                           "lm_connection_open_failed: %s", lmerror->message);

      return FALSE;
    }

  return TRUE;
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

  g_assert(lmconn == priv->conn);

  if (!success)
    {
      g_debug ("connection_open_cb failed");

      /* TODO: emit signal, change status */

      return;
    }

  if (!lm_connection_authenticate (lmconn, priv->account, priv->password,
                                   priv->resource, connection_auth_cb,
                                   conn, NULL, &error))
    {
      g_debug ("lm_connection_authenticate failed: %s", error->message);

      /* TODO: disconnect, emit signal, change status */
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

  g_assert(lmconn == priv->conn);

  if (!success)
    {
      g_debug ("connection_auth_cb failed");

      /* TODO: disconnect, emit signal, change status */

      return;
    }

  message = lm_message_new (NULL, LM_MESSAGE_TYPE_PRESENCE);

  if (!lm_connection_send (lmconn, message, &error))
    {
      g_debug ("lm_connection_send of initial presence failed: %s",
               error->message);

      /* TODO: disconnect, emit signal, change status */
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
gboolean gabble_connection_request_channel (GabbleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean supress_handler, gpointer* ret, GError **error)
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

