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

#define DBUS_API_SUBJECT_TO_CHANGE
#define _GNU_SOURCE /* Needed for strptime (_XOPEN_SOURCE can also be used). */

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "handles.h"
#include "handle-set.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-connection.h"
#include "gabble-connection-glue.h"
#include "gabble-connection-signals-marshal.h"

#include "gabble-im-channel.h"
#include "gabble-muc-channel.h"
#include "gabble-media-channel.h"
#include "gabble-roster-channel.h"
#include "gabble-disco.h"
#include "gabble-roomlist-channel.h"

#define BUS_NAME        "org.freedesktop.Telepathy.Connection.gabble"
#define OBJECT_PATH     "/org/freedesktop/Telepathy/Connection/gabble"

#define XMLNS_ROSTER    "jabber:iq:roster"

#define TP_CAPABILITY_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID))

#define ERROR_IF_NOT_CONNECTED(PRIV, ERROR) \
  if ((PRIV)->status != TP_CONN_STATUS_CONNECTED) \
    { \
      g_debug ("%s: rejected request as disconnected", G_STRFUNC); \
      (ERROR) = g_error_new(TELEPATHY_ERRORS, NotAvailable, \
                            "Connection is disconnected"); \
      return FALSE; \
    }

#define ERROR_IF_NOT_CONNECTED_ASYNC(PRIV, ERROR, CONTEXT) \
  if ((PRIV)->status != TP_CONN_STATUS_CONNECTED) \
    { \
      g_debug ("%s: rejected request as disconnected", G_STRFUNC); \
      (ERROR) = g_error_new(TELEPATHY_ERRORS, NotAvailable, \
                            "Connection is disconnected"); \
      dbus_g_method_return_error ((CONTEXT), (ERROR)); \
      g_error_free ((ERROR)); \
      return FALSE; \
    }


G_DEFINE_TYPE(GabbleConnection, gabble_connection, G_TYPE_OBJECT)

#define JABBER_PRESENCE_SHOW_AWAY "away"
#define JABBER_PRESENCE_SHOW_CHAT "chat"
#define JABBER_PRESENCE_SHOW_DND "dnd"
#define JABBER_PRESENCE_SHOW_XA "xa"

typedef struct _StatusInfo StatusInfo;

struct _StatusInfo
{
  const gchar *name;
  TpConnectionPresenceType presence_type;
  const gboolean self;
  const gboolean exclusive;
};

static const StatusInfo gabble_statuses[LAST_GABBLE_PRESENCE] = {
 { "available", TP_CONN_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE },
 { "away",      TP_CONN_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "chat",      TP_CONN_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE },
 { "dnd",       TP_CONN_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "xa",        TP_CONN_PRESENCE_TYPE_EXTENDED_AWAY, TRUE, TRUE },
 { "offline",   TP_CONN_PRESENCE_TYPE_OFFLINE,       TRUE, TRUE }
};

static void
contact_presence_destroy (ContactPresence *cp)
{
  g_free (cp->status_message);
  g_free (cp);
}

/* signal enum */
enum
{
    CAPABILITIES_CHANGED,
    GOT_CONTACT_INFO,
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    STATUS_CHANGED,
    DISCONNECTED,
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
  LmMessageHandler *presence_cb;
  LmMessageHandler *iq_roster_cb;
  LmMessageHandler *iq_jingle_cb;
  LmMessageHandler *iq_unknown_cb;

  /* disconnect reason */
  TpConnectionStatusReason disconnect_reason;

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

  /* jingle sessions */
  GHashTable *jingle_sessions;

  /* channels */
  GHashTable *im_channels;

  GHashTable *muc_channels;

  GPtrArray *media_channels;
  guint media_channel_index;

  GabbleRosterChannel *publish_channel;
  GabbleRosterChannel *subscribe_channel;

  GabbleRoomlistChannel *roomlist_channel;

  /* clients */
  GData *client_contact_handle_sets;
  GData *client_room_handle_sets;
  GData *client_list_handle_sets;

  /* DISCO! */
  GabbleDisco *disco;

  /* server services */
  GList *conference_servers;

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

  priv->jingle_sessions = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                 NULL, NULL);

  priv->im_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                             NULL, g_object_unref);

  priv->muc_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                              NULL, g_object_unref);

  priv->media_channels = g_ptr_array_sized_new (1);
  priv->media_channel_index = 0;

  priv->status = TP_CONN_STATUS_CONNECTING;

  g_datalist_init (&priv->client_contact_handle_sets);
  g_datalist_init (&priv->client_room_handle_sets);
  g_datalist_init (&priv->client_list_handle_sets);

  priv->disco = gabble_disco_new (obj);
}

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
      g_free (priv->protocol);
      priv->protocol = g_value_dup_string (value);
      break;
    case PROP_CONNECT_SERVER:
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
      g_free (priv->stream_server);
      priv->stream_server = g_value_dup_string (value);
      break;
    case PROP_USERNAME:
      g_free (priv->username);
      priv->username = g_value_dup_string (value);
      break;
   case PROP_PASSWORD:
      g_free (priv->password);
      priv->password = g_value_dup_string (value);
      break;
    case PROP_RESOURCE:
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

  /** signal definitions */

  signals[CAPABILITIES_CHANGED] =
    g_signal_new ("capabilities-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_BOXED_BOXED,
                  G_TYPE_NONE, 3, G_TYPE_UINT, (dbus_g_type_get_collection ("GPtrArray", G_TYPE_VALUE_ARRAY)), (dbus_g_type_get_collection ("GPtrArray", G_TYPE_VALUE_ARRAY)));

  signals[GOT_CONTACT_INFO] =
    g_signal_new ("got-contact-info",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_STRING,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

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
                  G_TYPE_NONE, 1, (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)))), G_TYPE_INVALID)))));

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[DISCONNECTED] =
    g_signal_new ("disconnected",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_connection_class), &dbus_glib_gabble_connection_object_info);
}

static void close_all_channels (GabbleConnection *conn);

void
gabble_connection_dispose (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  DBusGProxy *bus_proxy;
  bus_proxy = tp_get_bus_proxy ();

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_debug ("%s: dispose called", G_STRFUNC);

  if (priv->jingle_sessions)
    {
      g_assert (g_hash_table_size (priv->jingle_sessions) == 0);
      g_hash_table_destroy (priv->jingle_sessions);
    }
  if (priv->im_channels)
    {
      g_assert (g_hash_table_size (priv->im_channels) == 0);
      g_hash_table_destroy (priv->im_channels);
    }
  if (priv->muc_channels)
    {
      g_assert (g_hash_table_size (priv->muc_channels) == 0);
      g_hash_table_destroy (priv->muc_channels);
    }
  if (priv->media_channels)
    {
      g_assert (priv->media_channels->len == 0);
      g_ptr_array_free (priv->media_channels, TRUE);
    }

  g_object_unref (priv->disco);

  if (priv->conn)
    {
      if (lm_connection_is_open (priv->conn))
        {
          g_warning ("%s: connection was open when the object was deleted, it'll probably crash now...", G_STRFUNC);
          lm_connection_close (priv->conn, NULL);
        }

      lm_connection_unregister_message_handler (priv->conn, priv->message_cb,
                                                LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (priv->message_cb);

      lm_connection_unregister_message_handler (priv->conn, priv->presence_cb,
                                                LM_MESSAGE_TYPE_PRESENCE);
      lm_message_handler_unref (priv->presence_cb);

      lm_connection_unregister_message_handler (priv->conn, priv->iq_roster_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_roster_cb);

      lm_connection_unregister_message_handler (priv->conn, priv->iq_jingle_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_jingle_cb);

      lm_connection_unregister_message_handler (priv->conn, priv->iq_unknown_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_unknown_cb);
    }

  dbus_g_proxy_call_no_reply (bus_proxy, "ReleaseName",
                              G_TYPE_STRING, priv->bus_name,
                              G_TYPE_INVALID);

  if (G_OBJECT_CLASS (gabble_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_parent_class)->dispose (object);
}

void
gabble_connection_finalize (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  g_debug ("%s called with %p", G_STRFUNC, object);

  if (priv->conn)
    lm_connection_unref (priv->conn);

  g_free (priv->protocol);
  g_free (priv->connect_server);
  g_free (priv->stream_server);
  g_free (priv->username);
  g_free (priv->password);
  g_free (priv->resource);
  g_free (priv->bus_name);
  g_free (priv->object_path);

  g_datalist_clear (&priv->client_room_handle_sets);
  g_datalist_clear (&priv->client_contact_handle_sets);
  g_datalist_clear (&priv->client_list_handle_sets);

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
  GError *request_error;

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

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", &request_error,
                          G_TYPE_STRING, priv->bus_name,
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            request_error->message);
      return FALSE;
    }

  if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
      gchar *msg;

      switch (request_name_result)
        {
        case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
          msg = "Request has been queued, though we request non-queueing.";
          break;
        case DBUS_REQUEST_NAME_REPLY_EXISTS:
          msg = "A connection manger already has this busname.";
          break;
        case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
          msg = "Connection manager already has a connection to this account.";
          break;
        default:
          msg = "Unknown error return from ReleaseName";
        }

      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "Error acquiring bus name %s, %s",
                             priv->bus_name, msg);
      return FALSE;
    }

  g_debug ("%s: bus name %s", G_STRFUNC, priv->bus_name);

  dbus_g_connection_register_g_object (bus, priv->object_path, G_OBJECT (conn));

  g_debug ("%s: object path %s", G_STRFUNC, priv->object_path);

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
      g_debug ("_gabble_connection_send failed: %s", lmerror->message);

      if (error)
        {
          *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                                "message send failed: %s", lmerror->message);
        }

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
}

typedef struct {
    GabbleConnectionMsgReplyFunc reply_func;

    GabbleConnection *conn;
    LmMessage *sent_msg;
    gpointer user_data;

    GObject *object;
    gboolean object_alive;
} GabbleMsgHandlerData;

static LmHandlerResult
message_send_reply_cb (LmMessageHandler *handler,
                       LmConnection *connection,
                       LmMessage *reply_msg,
                       gpointer user_data)
{
  GabbleMsgHandlerData *handler_data = user_data;
  LmMessageSubType sub_type;

  sub_type = lm_message_get_sub_type (reply_msg);

  /* Is it a reply to this message? If we're talking to another loudmouth,
   * they can send us messages which have the same ID as ones we send. :-O */
  if (sub_type != LM_MESSAGE_SUB_TYPE_RESULT &&
      sub_type != LM_MESSAGE_SUB_TYPE_ERROR)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (handler_data->object_alive)
    {
      return handler_data->reply_func (handler_data->conn,
                                       handler_data->sent_msg,
                                       reply_msg,
                                       handler_data->object,
                                       handler_data->user_data);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
message_send_object_destroy_notify_cb (gpointer data,
                                       GObject *where_the_object_was)
{
  GabbleMsgHandlerData *handler_data = data;

  handler_data->object = NULL;
  handler_data->object_alive = FALSE;
}

static void
message_send_handler_destroy_cb (gpointer data)
{
  GabbleMsgHandlerData *handler_data = data;

  lm_message_unref (handler_data->sent_msg);

  if (handler_data->object != NULL)
    {
      g_object_weak_unref (handler_data->object,
                           message_send_object_destroy_notify_cb,
                           handler_data);
    }

  g_free (handler_data);
}

/**
 * _gabble_connection_send_with_reply
 *
 * Send a tracked LmMessage and trap network errors appropriately.
 *
 * If object is non-NULL the handler will follow the lifetime of that object,
 * which means that if the object is destroyed the callback will not be invoked.
 */
gboolean
_gabble_connection_send_with_reply (GabbleConnection *conn,
                                    LmMessage *msg,
                                    GabbleConnectionMsgReplyFunc reply_func,
                                    GObject *object,
                                    gpointer user_data,
                                    GError **error)
{
  GabbleConnectionPrivate *priv;
  LmMessageHandler *handler;
  GabbleMsgHandlerData *handler_data;
  gboolean ret;
  GError *lmerror = NULL;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  lm_message_ref (msg);

  handler_data = g_new (GabbleMsgHandlerData, 1);
  handler_data->reply_func = reply_func;
  handler_data->conn = conn;
  handler_data->sent_msg = msg;
  handler_data->user_data = user_data;

  handler_data->object = object;
  handler_data->object_alive = TRUE;

  if (object != NULL)
    {
      g_object_weak_ref (object, message_send_object_destroy_notify_cb,
                         handler_data);
    }

  handler = lm_message_handler_new (message_send_reply_cb, handler_data,
                                    message_send_handler_destroy_cb);

  ret = lm_connection_send_with_reply (priv->conn, msg, handler, &lmerror);
  if (!ret)
    {
      g_debug ("_gabble_connection_send_with_reply failed: %s",
               lmerror->message);

      if (error)
        {
          *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                                "message send failed: %s", lmerror->message);
        }

      g_error_free (lmerror);
    }

  lm_message_handler_unref (handler);

  return ret;
}

static LmHandlerResult connection_message_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_presence_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_roster_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_jingle_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_unknown_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmSSLResponse connection_ssl_cb (LmSSL*, LmSSLStatus, gpointer);
static void connection_open_cb (LmConnection*, gboolean, gpointer);
static void connection_auth_cb (LmConnection*, gboolean, gpointer);
static GabbleIMChannel *new_im_channel (GabbleConnection *conn, GabbleHandle handle, gboolean suppress_handler);
static void make_roster_channels (GabbleConnection *conn);
static void discover_services (GabbleConnection *conn);

static void connection_disconnect (GabbleConnection *conn, TpConnectionStatusReason reason);
static void connection_disconnected_cb (LmConnection *connection, LmDisconnectReason lm_reason, gpointer user_data);
static void update_presence (GabbleConnection *self, GabbleHandle contact_handle, GabblePresenceId presence_id, const gchar *status_message, const gchar *voice_resource);

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

      /* send whitespace to the server every 30 seconds */
      lm_connection_set_keep_alive_rate (priv->conn, 30);

      jid = g_strdup_printf ("%s@%s", priv->username, priv->stream_server);
      lm_connection_set_jid (priv->conn, jid);

      lm_connection_set_disconnect_function (priv->conn,
                                             connection_disconnected_cb,
                                             conn,
                                             NULL);

      priv->self_handle = gabble_handle_for_contact (priv->handles,
                                                     jid, FALSE);

      if (priv->self_handle == 0)
        {
          /* FIXME: check this sooner and return an error to the user
           * this will be when we implement Connect() in spec 0.13 */
          g_error ("%s: invalid jid %s", G_STRFUNC, jid);

          return FALSE;
        }

      valid = gabble_handle_ref (priv->handles,
                                 TP_HANDLE_TYPE_CONTACT,
                                 priv->self_handle);
      g_assert (valid);

      /* set initial presence. TODO: some way for the user to set this */
      update_presence (conn, priv->self_handle, GABBLE_PRESENCE_AVAILABLE, NULL, "Telepathy");

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

      priv->presence_cb = lm_message_handler_new (connection_presence_cb,
                                                  conn, NULL);
      lm_connection_register_message_handler (priv->conn, priv->presence_cb,
                                              LM_MESSAGE_TYPE_PRESENCE,
                                              LM_HANDLER_PRIORITY_NORMAL);

      priv->iq_roster_cb = lm_message_handler_new (connection_iq_roster_cb,
                                                   conn, NULL);
      lm_connection_register_message_handler (priv->conn, priv->iq_roster_cb,
                                              LM_MESSAGE_TYPE_IQ,
                                              LM_HANDLER_PRIORITY_NORMAL);

      priv->iq_jingle_cb = lm_message_handler_new (connection_iq_jingle_cb,
                                                   conn, NULL);
      lm_connection_register_message_handler (priv->conn, priv->iq_jingle_cb,
                                              LM_MESSAGE_TYPE_IQ,
                                              LM_HANDLER_PRIORITY_NORMAL);

      priv->iq_unknown_cb = lm_message_handler_new (connection_iq_unknown_cb,
                                                conn, NULL);
      lm_connection_register_message_handler (priv->conn, priv->iq_unknown_cb,
                                              LM_MESSAGE_TYPE_IQ,
                                              LM_HANDLER_PRIORITY_LAST);
    }
  else
    {
      g_assert (lm_connection_is_open (priv->conn) == FALSE);
    }

  if (!lm_connection_open (priv->conn, connection_open_cb,
                           conn, NULL, &lmerror))
    {
      g_debug ("%s: %s", G_STRFUNC, lmerror->message);

      *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                            "lm_connection_open_failed: %s", lmerror->message);

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
}

/**
 * connection_status_change:
 * @conn: a #GabbleConnection
 * @status: new status to advertise
 * @reason: reason for new status
 *
 * Compares status with current status. If different, emits a signal
 * for the new status, and updates it in the #GabbleConnection.
 */
static void
connection_status_change (GabbleConnection        *conn,
                          TpConnectionStatus       status,
                          TpConnectionStatusReason reason)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("%s: status %u reason %u", G_STRFUNC, status, reason);

  if (priv->status != status)
    {
      priv->status = status;

      g_debug ("%s emitting status-changed with status %u reason %u",
               G_STRFUNC, status, reason);
      g_signal_emit (conn, signals[STATUS_CHANGED], 0, status, reason);
    }
}

static void im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data);

gboolean hash_foreach_close_im_channel (gpointer key,
                                    gpointer value,
                                    gpointer user_data)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (value);
  GError *error = NULL;

  g_signal_handlers_disconnect_by_func (chan, (GCallback) im_channel_closed_cb,
                                       user_data);
  g_debug ("%s calling gabble_im_channel_close on %p", G_STRFUNC, chan);
  gabble_im_channel_close (chan, &error);
  g_debug ("%s removing channel %p", G_STRFUNC, chan);
  return TRUE;
}

/**
 * close_all_channels:
 * @conn: A #GabbleConnection object
 *
 * Closes all channels owned by @conn.
 */
static void
close_all_channels (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  guint i;

  if (priv->im_channels)
    {
      g_hash_table_destroy (priv->im_channels);
      priv->im_channels = NULL;
    }

  if (priv->muc_channels)
    {
      g_hash_table_destroy (priv->muc_channels);
      priv->muc_channels = NULL;
    }

  if (priv->media_channels)
    {
      for (i = 0; i < priv->media_channels->len; i++)
        {
          GabbleMediaChannel *chan = g_ptr_array_index (priv->media_channels, i);

          g_debug ("%s: about to unref channel with ref_count %d",
                   G_STRFUNC, G_OBJECT (chan)->ref_count);

          g_object_unref (chan);
        }

      g_ptr_array_free (priv->media_channels, TRUE);
      priv->media_channels = NULL;
    }

  if (priv->jingle_sessions)
    {
      g_hash_table_destroy (priv->jingle_sessions);
      priv->jingle_sessions = NULL;
    }

  if (priv->publish_channel)
    {
      g_object_unref (priv->publish_channel);
      priv->publish_channel = NULL;
    }

  if (priv->subscribe_channel)
    {
      g_object_unref (priv->subscribe_channel);
      priv->subscribe_channel = NULL;
    }

  priv->media_channel_index = 0;
}

/**
 * connection_disconnect:
 * @conn: A #GabbleConnection
 * @reason: reason for disconnect
 *
 * Request @conn to disconnect
 *
 * Starts the disconnection process and sets the status to disconnected.
 */
static void
connection_disconnect (GabbleConnection *conn, TpConnectionStatusReason reason)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  priv->disconnect_reason = reason;

  /* remove the channels so we don't get any race conditions
   * where method calls are delivered to a channel after we've started
   * disconnection */
  close_all_channels (conn);

  connection_status_change (conn, TP_CONN_STATUS_DISCONNECTED, TP_CONN_STATUS_REASON_REQUESTED);
  lm_connection_close (priv->conn, NULL);
}

static void
connection_disconnected_cb (LmConnection *connection,
                            LmDisconnectReason lm_reason,
                            gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  TpConnectionStatusReason tp_reason;

  g_debug ("%s: called with reason %u", G_STRFUNC, lm_reason);

  switch (lm_reason)
    {
    case LM_DISCONNECT_REASON_OK:
      tp_reason = priv->disconnect_reason;
      break;
    case LM_DISCONNECT_REASON_PING_TIME_OUT:
    case LM_DISCONNECT_REASON_HUP:
      tp_reason = TP_CONN_STATUS_REASON_NETWORK_ERROR;
      break;
    case LM_DISCONNECT_REASON_ERROR:
    case LM_DISCONNECT_REASON_UNKNOWN:
      tp_reason = priv->disconnect_reason;
    default:
      g_warning ("%s: Unknown reason code returned from libloudmouth",
          G_STRFUNC);
      tp_reason = TP_CONN_STATUS_REASON_NONE_SPECIFIED;
    }

   close_all_channels (conn);
   connection_status_change (conn, TP_CONN_STATUS_DISCONNECTED, tp_reason);
   g_signal_emit(conn, signals[DISCONNECTED], 0);

}

/**
 * muc_channel_closed_cb:
 *
 * Signal callback for when a MUC channel is closed. Removes the references
 * that #GabbleConnection holds to them.
 */
static void
muc_channel_closed_cb (GabbleMucChannel *chan, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GabbleHandle room_handle;

  g_object_get (chan, "handle", &room_handle, NULL);

  g_debug ("%s: removing MUC channel with handle %d", G_STRFUNC, room_handle);
  g_hash_table_remove (priv->muc_channels, GINT_TO_POINTER (room_handle));
}

/**
 * new_muc_channel
 */
static GabbleMucChannel *
new_muc_channel (GabbleConnection *conn, GabbleHandle handle, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv;
  GabbleMucChannel *chan;
  char *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  object_path = g_strdup_printf ("%s/MucChannel%u", priv->object_path, handle);

  chan = g_object_new (GABBLE_TYPE_MUC_CHANNEL,
                       "connection", conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  g_debug ("new_muc_channel: object path %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) muc_channel_closed_cb, conn);

  g_hash_table_insert (priv->muc_channels, GINT_TO_POINTER (handle), chan);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_TEXT,
                 TP_HANDLE_TYPE_ROOM, handle,
                 suppress_handler);

  g_free (object_path);

  return chan;
}

static gboolean
node_is_for_muc (LmMessageNode *toplevel_node, LmMessageNode **muc_node)
{
  LmMessageNode *node;

  for (node = toplevel_node->children; node; node = node->next)
    {
      if (strcmp (node->name, "x") == 0)
        {
          const gchar *xmlns;

          xmlns = lm_message_node_get_attribute (node, "xmlns");
          if (xmlns && strcmp (xmlns, MUC_XMLNS_USER) == 0)
            {
              if (muc_node)
                {
                  *muc_node = node;
                }

              return TRUE;
            }
        }
    }

  return FALSE;
}

static GabbleMucChannel *
get_muc_from_jid (GabbleConnection *conn, const gchar *jid)
{
  gchar *base_jid;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GabbleHandle handle;
  GabbleMucChannel *chan = NULL;

  base_jid = gabble_handle_jid_get_base (jid);

  if (gabble_handle_for_room_exists (priv->handles, base_jid))
    {
      handle = gabble_handle_for_room (priv->handles, base_jid);

      chan = g_hash_table_lookup (priv->muc_channels,
                                  GUINT_TO_POINTER (handle));
    }

  g_free (base_jid);

  return chan;
}

/**
 * connection_message_cb:
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
  LmMessageNode *msg_node, *body_node, *muc_node, *node;
  const gchar *type, *from, *body, *body_offset;
  GabbleHandle handle;
  time_t stamp;

  g_assert (connection == priv->conn);

  msg_node = lm_message_get_node (message);
  type = lm_message_node_get_attribute (msg_node, "type");
  from = lm_message_node_get_attribute (msg_node, "from");
  body_node = lm_message_node_get_child (msg_node, "body");

  if (from == NULL)
    {
      HANDLER_DEBUG (msg_node, "got a message without a from field, ignoring");

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* is it a MUC message? */
  if (node_is_for_muc (msg_node, &muc_node))
    {
      GabbleMucChannel *chan;

      /* and an invitation? */
      node = lm_message_node_get_child (muc_node, "invite");
      if (node)
        {
          LmMessageNode *reason_node;
          const gchar *reason;
          GabbleGroupMixin *mixin;
          GIntSet *empty, *set;

          /* create the channel */
          handle = gabble_handle_for_room (priv->handles, from);

          chan = new_muc_channel (conn, handle, FALSE);

          reason_node = lm_message_node_get_child (node, "reason");
          if (reason_node)
            {
              reason = lm_message_node_get_value (reason_node);
            }
          else
            {
              reason = "";
              HANDLER_DEBUG (msg_node, "no MUC invite reason specified");
            }

          /* add ourself to local pending */
          mixin = GABBLE_GROUP_MIXIN (chan);

          empty = g_intset_new ();
          set = g_intset_new ();
          g_intset_add (set, mixin->self_handle);

          gabble_group_mixin_change_members (G_OBJECT (chan), reason, empty,
                                             empty, set, empty);

          g_intset_destroy (empty);
          g_intset_destroy (set);

          gabble_group_mixin_change_flags (G_OBJECT (chan),
                                           TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  if (body_node == NULL)
    {
      HANDLER_DEBUG (msg_node, "got a message without a body field, ignoring");

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  body = lm_message_node_get_value (body_node);

  /* parse timestamp of delayed messages */
  stamp = 0;

  for (node = msg_node->children; node; node = node->next)
    {
      if (strcmp (node->name, "x") == 0)
        {
          const gchar *xmlns, *stamp_str, *p;
          struct tm stamp_tm = { 0, };

          xmlns = lm_message_node_get_attribute (node, "xmlns");
          if (xmlns == NULL)
            continue;

          if (strcmp (xmlns, "jabber:x:delay") != 0)
            continue;

          stamp_str = lm_message_node_get_attribute (node, "stamp");
          if (stamp_str == NULL)
            continue;

          p = strptime (stamp_str, "%Y%m%dT%T", &stamp_tm);
          if (p == NULL || *p != '\0')
            {
              g_warning ("%s: malformed date string '%s' for jabber:x:delay",
                         G_STRFUNC, stamp_str);
              continue;
            }

          stamp = timegm (&stamp_tm);
        }
    }

  if (stamp == 0)
    stamp = time (NULL);

  if (type != NULL && 0 == strcmp (type, "groupchat"))
    {
      gchar *base_jid;
      GabbleHandle room_handle;
      GabbleMucChannel *chan;
      TpChannelTextMessageType msgtype;

      /* verify that the room exists and get its handle */
      base_jid = gabble_handle_jid_get_base (from);

      if (gabble_handle_for_room_exists (priv->handles, base_jid))
        {
          room_handle = gabble_handle_for_room (priv->handles, base_jid);
          g_free (base_jid);
        }
      else
        {
          g_free (base_jid);
          g_warning ("%s: ignoring groupchat message from unknown chat",
                     G_STRFUNC);

          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }

      /* find the MUC channel */
      chan = g_hash_table_lookup (priv->muc_channels,
                                  GUINT_TO_POINTER (room_handle));

      if (!chan)
        {
          g_warning ("%s: ignoring groupchat message from known handle with "
                     "no MUC channel", G_STRFUNC);

          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }

      /* get the handle of the sender, which is either the room
       * itself or one of its members */
      if (gabble_handle_for_room_exists (priv->handles, from))
        {
          handle = room_handle;
        }
      else
        {
          handle = gabble_handle_for_contact (priv->handles, from, TRUE);
        }

      if (0 == strncmp (body, "/me ", 4))
        {
          msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
          body_offset = body + 4;
        }
      else
        {
          msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
          body_offset = body;
        }

      if (_gabble_muc_channel_receive (chan, msgtype, handle, stamp,
                                       body_offset, msg_node))
        return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else
    {
      TpChannelTextMessageType msgtype;
      GabbleIMChannel *chan;

      handle = gabble_handle_for_contact (priv->handles, from, FALSE);

      if (handle == 0)
        {
          HANDLER_DEBUG (msg_node, "ignoring message node from malformed jid");

          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }

      /* messages starting with /me are ACTION messages, and the /me should be
       * removed. type="chat" messages are NORMAL.  everything else is
       * something that doesn't necessarily expect a reply or ongoing
       * conversation ("normal") or has been auto-sent, so we make it NOTICE in
       * all other cases. */
      if (0 == strncmp (body, "/me ", 4))
        {
          msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
          body_offset = body + 4;
        }
      else if (type != NULL && 0 == strcmp (type, "chat"))
        {
          msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
          body_offset = body;
        }
      else
        {
          msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
          body_offset = body;
        }

      g_debug ("%s: message from %s (handle %u), msgtype %d, body:\n%s",
               G_STRFUNC, from, handle, msgtype, body_offset);

      chan = g_hash_table_lookup (priv->im_channels, GINT_TO_POINTER (handle));

      if (chan == NULL)
        {
          g_debug ("%s: found no IM channel, creating one", G_STRFUNC);

          chan = new_im_channel (conn, handle, FALSE);
        }

      if (_gabble_im_channel_receive (chan, msgtype, handle,
                                      stamp, body_offset))
        return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/**
 * _get_contact_presence_quark:
 *
 * Returns: the quark used for storing presence information on
 *          a GabbleHandle
 */
GQuark
_get_contact_presence_quark()
{
  static GQuark presence_quark = 0;
  if (!presence_quark)
    presence_quark = g_quark_from_static_string("ContactPresenceQuark");
  return presence_quark;
}

/**
 * destroy_the_bastard:
 * @data: a GValue to destroy
 *
 * destroys a GValue allocated on the heap
 */
static void
destroy_the_bastard (GValue *value)
{
  g_value_unset (value);
  g_free (value);
}

/**
 * emit_presence_update:
 * @self: A #GabbleConnection
 * @contact_handles: A zero-terminated array of #GabbleHandle for
 *                    the contacts to emit presence for
 *
 * Emits the Telepathy PresenceUpdate signal with the current
 * stored presence information for the given contact.
 */
static void
emit_presence_update (GabbleConnection *self,
                      const GabbleHandle* contact_handles)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  GQuark data_key = _get_contact_presence_quark();
  ContactPresence *cp;
  GHashTable *presence;
  GValueArray *vals;
  GHashTable *contact_status, *parameters;
  guint timestamp = 0; /* this is never set at the moment*/

  presence = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                    (GDestroyNotify) g_value_array_free);

  for (;*contact_handles; contact_handles++)
    {
      GValue *message;

      cp = gabble_handle_get_qdata (priv->handles, TP_HANDLE_TYPE_CONTACT,
                                    *contact_handles, data_key);

      if (!cp)
        continue;

      message = g_new0 (GValue, 1);
      g_value_init (message, G_TYPE_STRING);
      g_value_set_static_string (message, cp->status_message);

      parameters =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) destroy_the_bastard);

      g_hash_table_insert (parameters, "message", message);

      contact_status =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) g_hash_table_destroy);
      g_hash_table_insert (contact_status,
          (gpointer) gabble_statuses[cp->presence_id].name,
          parameters);

      vals = g_value_array_new (2);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1),
          dbus_g_type_get_map ("GHashTable", G_TYPE_STRING,
            dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)));
      g_value_take_boxed (g_value_array_get_nth (vals, 1), contact_status);

      g_hash_table_insert (presence, GINT_TO_POINTER (*contact_handles),
                           vals);
    }

  g_signal_emit (self, signals[PRESENCE_UPDATE], 0, presence);
  g_hash_table_destroy (presence);
}

/**
 * signal_own_presence:
 * @self: A #GabbleConnection
 * @error: pointer in which to return a GError in case of failure.
 *
 * Signal the user's stored presence to the jabber server
 *
 * Retuns: FALSE if an error occured
 */
static gboolean
signal_own_presence (GabbleConnection *self, GError **error)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  GQuark data_key = _get_contact_presence_quark();
  ContactPresence *cp = gabble_handle_get_qdata (priv->handles,
      TP_HANDLE_TYPE_CONTACT, priv->self_handle, data_key);
  LmMessage *message = NULL;
  LmMessageNode *node;
  LmMessageSubType subtype;

  if (cp->presence_id == GABBLE_PRESENCE_OFFLINE)
    subtype = LM_MESSAGE_SUB_TYPE_UNAVAILABLE;
  else
    subtype = LM_MESSAGE_SUB_TYPE_AVAILABLE;

  message = lm_message_new_with_sub_type (NULL, LM_MESSAGE_TYPE_PRESENCE,
              subtype);

  node = lm_message_get_node (message);

  if (cp->status_message)
    {
      lm_message_node_add_child (node, "status", cp->status_message);
    }

  switch (cp->presence_id)
    {
    case GABBLE_PRESENCE_AVAILABLE:
    case GABBLE_PRESENCE_OFFLINE:
      break;
    case GABBLE_PRESENCE_AWAY:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_AWAY);
      break;
    case GABBLE_PRESENCE_CHAT:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_CHAT);
      break;
    case GABBLE_PRESENCE_DND:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_DND);
      break;
    case GABBLE_PRESENCE_XA:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_XA);
      break;
    default:
      g_critical ("%s: Unexpected Telepathy presence type", G_STRFUNC);
      break;
    }

  /* FIXME: use constants from libloudmouth and libjingle here */
  node = lm_message_node_add_child (node,
                                    "c", NULL);
  lm_message_node_set_attributes (node,
                                  "node",  "http://www.google.com/xmpp/client/caps",
                                  "ver",   "1.0.0.82", /* latest GTalk as of 20060117 */
                                  "ext",   "voice-v1",
                                  "xmlns", "http://jabber.org/protocol/caps",
                                  NULL);

  if (!_gabble_connection_send (self, message, error))
    goto ERROR;

  lm_message_unref (message);
  return TRUE;

ERROR:
  if (message)
    lm_message_unref(message);

  return FALSE;
}





/**
 * update_presence:
 * @self: A #GabbleConnection
 * @contact_handle: #GabbleHandle representing a contact to update
 * @presence_id: the presence to set
 * @status_message: message associated with new presence
 *
 * This checks the new presence against the stored presence information
 * for this contact, and if it is different, updates our store and
 * emits a PresenceUpdate signal using #emit_presence_update
 */
static void
update_presence (GabbleConnection *self, GabbleHandle contact_handle,
                 GabblePresenceId presence_id,
                 const gchar *status_message,
                 const gchar *voice_resource)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  GQuark data_key = _get_contact_presence_quark();
  ContactPresence *cp = gabble_handle_get_qdata (priv->handles,
      TP_HANDLE_TYPE_CONTACT, contact_handle, data_key);
  GabbleHandle handles[2] = {contact_handle, 0};

  if (cp)
    {
      if (cp->presence_id == presence_id &&
          ((cp->status_message == NULL && status_message == NULL) ||
           (cp->status_message && status_message &&
            strcmp(cp->status_message, status_message) == 0 &&
            (cp->voice_resource && voice_resource == NULL))))
        {
          return;
        }
    }
  else
    {
      cp = g_new0 (ContactPresence, 1);
      gabble_handle_set_qdata (priv->handles, TP_HANDLE_TYPE_CONTACT,
                               contact_handle, data_key, cp,
                               (GDestroyNotify) contact_presence_destroy);
    }

  cp->presence_id = presence_id;

  g_free (cp->status_message);
  if (status_message)
    cp->status_message = g_strdup (status_message);
  else
    cp->status_message = NULL;

  if (voice_resource)
    {
      g_debug ("%s: setting voice resource to %s (was %s) for GabbleHandle %d",
               G_STRFUNC, voice_resource,
               cp->voice_resource ? cp->voice_resource : "<unset>",
               contact_handle);
      g_free (cp->voice_resource);
      cp->voice_resource = g_strdup (voice_resource);
    }

  emit_presence_update (self, handles);
}

/**
 * connection_presence_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
connection_presence_cb (LmMessageHandler *handler,
                        LmConnection *connection,
                        LmMessage *message,
                        gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *pres_node, *child_node, *node;
  const char *from;
  LmMessageSubType sub_type;
  GabbleMucChannel *muc_chan;
  gboolean is_for_muc;
  GIntSet *empty, *tmp;
  GabbleHandle handle;
  LmMessage *reply = NULL;
  const gchar *presence_show = NULL;
  const gchar *status_message = NULL;
  GabblePresenceId presence_id;
  gchar *voice_resource = NULL;

  g_assert (connection == priv->conn);

  pres_node = lm_message_get_node (message);

  from = lm_message_node_get_attribute (pres_node, "from");

  if (from == NULL)
    {
      HANDLER_DEBUG (pres_node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  sub_type = lm_message_get_sub_type (message);

  /* is it an error and for a MUC? */
  if (sub_type == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      muc_chan = get_muc_from_jid (conn, from);

      if (muc_chan != NULL)
        {
          _gabble_muc_channel_presence_error (muc_chan, from, pres_node);

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  is_for_muc = node_is_for_muc (pres_node, NULL);

  handle = gabble_handle_for_contact (priv->handles, from, is_for_muc);

  if (handle == 0)
    {
      HANDLER_DEBUG (pres_node, "ignoring presence from malformed jid");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  if (handle == priv->self_handle)
    {
      HANDLER_DEBUG (pres_node, "ignoring presence from ourselves on another resource");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  g_assert (handle != 0);

  child_node = lm_message_node_get_child (pres_node, "status");
  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  switch (sub_type)
    {
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBE:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: making %s (handle %u) local pending on the publish channel",
          G_STRFUNC, from, handle);

      /* make the contact local pending on the publish channel */
      g_intset_add (tmp, handle);
      _gabble_roster_channel_change_members (priv->publish_channel,
          status_message, empty, empty, tmp, empty);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);
      break;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: removing %s (handle %u) from the publish channel",
          G_STRFUNC, from, handle);

      /* remove the contact from the publish channel */
      g_intset_add (tmp, handle);
      _gabble_roster_channel_change_members (priv->publish_channel,
          status_message, empty, tmp, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED);
      _gabble_connection_send (conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);
      break;
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBED:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: adding %s (handle %u) to the subscribe channel",
          G_STRFUNC, from, handle);

      /* add the contact to the subscribe channel */
      g_intset_add (tmp, handle);
      _gabble_roster_channel_change_members (priv->subscribe_channel,
          status_message, tmp, empty, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_SUBSCRIBE);
      _gabble_connection_send (conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);
      break;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: removing %s (handle %u) from the subscribe channel",
          G_STRFUNC, from, handle);

      /* remove the contact from the subscribe channel */
      g_intset_add (tmp, handle);
      _gabble_roster_channel_change_members (priv->subscribe_channel,
          status_message, empty, tmp, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);
      _gabble_connection_send (conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);
      break;

    case LM_MESSAGE_SUB_TYPE_ERROR:
      g_warning ("%s: XMPP Presence Error received, setting contact to offline",
                 G_STRFUNC);
      HANDLER_DEBUG (pres_node, "presence node");
    case LM_MESSAGE_SUB_TYPE_UNAVAILABLE:
      update_presence (conn, handle, GABBLE_PRESENCE_OFFLINE, status_message, NULL);
      break;
    case LM_MESSAGE_SUB_TYPE_NOT_SET:
    case LM_MESSAGE_SUB_TYPE_AVAILABLE:
      child_node = lm_message_node_get_child (pres_node, "show");
      if (!child_node)
        {
          presence_id = GABBLE_PRESENCE_AVAILABLE;
        }
      else
        {
          presence_show = lm_message_node_get_value (child_node);
          if (presence_show)
            {
              if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_AWAY))
                presence_id = GABBLE_PRESENCE_AWAY;
              else if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_CHAT))
                presence_id = GABBLE_PRESENCE_CHAT;
              else if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_DND))
                presence_id = GABBLE_PRESENCE_DND;
              else if (0 == strcmp (presence_show, JABBER_PRESENCE_SHOW_XA))
                presence_id = GABBLE_PRESENCE_XA;
              else
                {
                  HANDLER_DEBUG (pres_node, "unrecognised <show/> value received from "
                      "server, setting presence to available");
                  presence_id = GABBLE_PRESENCE_AVAILABLE;
                }
            }
          else
            {
              HANDLER_DEBUG (pres_node, "empty <show> tag received from "
                  "server, setting presence to available");
              presence_id = GABBLE_PRESENCE_AVAILABLE;
            }
        }

      for (node = pres_node->children; node; node = node->next)
        {
          const gchar *cap_node, *cap_ext, *cap_xmlns;
          gchar *username, *server;

          if (strcmp (node->name, "c") != 0)
            continue;

          cap_node = lm_message_node_get_attribute (node, "node");
          cap_ext = lm_message_node_get_attribute (node, "ext");
          cap_xmlns = lm_message_node_get_attribute (node, "xmlns");

          if (!cap_node || !cap_ext || !cap_xmlns)
            continue;

          if (strcmp (cap_node, "http://www.google.com/xmpp/client/caps") != 0)
            continue;

          if (strcmp (cap_ext, "voice-v1") != 0)
            continue;

          if (strcmp (cap_xmlns, "http://jabber.org/protocol/caps") != 0)
            continue;

          gabble_handle_decode_jid (from, &username, &server, &voice_resource);

          g_debug ("%s: %s has voice-v1 support, storing resource %s",
                   G_STRFUNC, from, voice_resource);

          g_free (username);
          g_free (server);

          break;
        }

      update_presence (conn, handle, presence_id, status_message, voice_resource);
      g_free (voice_resource);
      break;
    default:
      HANDLER_DEBUG (pres_node, "called with unknown subtype");
    }

  if (is_for_muc)
    {
      muc_chan = get_muc_from_jid (conn, from);

      if (muc_chan != NULL)
        {
          _gabble_muc_channel_member_presence_updated (muc_chan, handle,
                                                       pres_node);
        }
      else
        {
          g_warning ("%s: muc channel for %s (%d) does not exist",
                     G_STRFUNC, from, handle);
        }
    }

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}


/**
 * connection_iq_roster_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with roster queries, and allows other handlers
 * if queries other than rosters are received.
 */
static LmHandlerResult
connection_iq_roster_cb (LmMessageHandler *handler,
                         LmConnection *connection,
                         LmMessage *message,
                         gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *iq_node, *query_node;

  g_assert (connection == priv->conn);

  iq_node = lm_message_get_node (message);
  query_node = lm_message_node_get_child (iq_node, "query");

  if (!query_node || strcmp (XMLNS_ROSTER,
        lm_message_node_get_attribute (query_node, "xmlns")))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* if this is a result, it's from our initial query. if it's a set,
   * it's a roster push. either way, parse the items. */
  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_RESULT ||
      lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_SET)
    {
      LmMessageNode *item_node;
      GIntSet *empty, *pub_add, *pub_rem,
              *sub_add, *sub_rem, *sub_rp;

      /* asymmetry is because we don't get locally pending subscription
       * requests via <roster>, we get it via <presence> */
      empty = g_intset_new ();
      pub_add = g_intset_new ();
      pub_rem = g_intset_new ();
      sub_add = g_intset_new ();
      sub_rem = g_intset_new ();
      sub_rp = g_intset_new ();

      /* iterate every sub-node, which we expect to be <item>s */
      for (item_node = query_node->children;
           item_node;
           item_node = item_node->next)
        {
          const char *jid, *subscription, *ask;
          GabbleHandle handle;

          if (strcmp (item_node->name, "item"))
            {
              HANDLER_DEBUG (item_node, "query sub-node is not item, skipping");
              continue;
            }

          jid = lm_message_node_get_attribute (item_node, "jid");
          if (!jid)
            {
              HANDLER_DEBUG (item_node, "item node has no jid, skipping");
              continue;
            }

          handle = gabble_handle_for_contact (priv->handles, jid, FALSE);
          if (handle == 0)
            {
              HANDLER_DEBUG (item_node, "item jid is malformed, skipping");
              continue;
            }

          subscription = lm_message_node_get_attribute (item_node, "subscription");
          if (!subscription)
            {
              HANDLER_DEBUG (item_node, "item node has no subscription, skipping");
              continue;
            }

          ask = lm_message_node_get_attribute (item_node, "ask");

          if (!strcmp (subscription, "both"))
            {
              g_intset_add (pub_add, handle);
              g_intset_add (sub_add, handle);
            }
          else if (!strcmp (subscription, "from"))
            {
              g_intset_add (pub_add, handle);
              if (ask != NULL && !strcmp (ask, "subscribe"))
                g_intset_add (sub_rp, handle);
              else
                g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "none"))
            {
              g_intset_add (pub_rem, handle);
              if (ask != NULL && !strcmp (ask, "subscribe"))
                g_intset_add (sub_rp, handle);
              else
                g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "remove"))
            {
              g_intset_add (pub_rem, handle);
              g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "to"))
            {
              g_intset_add (pub_rem, handle);
              g_intset_add (sub_add, handle);
            }
          else
            {
              HANDLER_DEBUG (item_node, "got unexpected subscription value");
            }
        }

      if (g_intset_size (pub_add) > 0 ||
          g_intset_size (pub_rem) > 0)
        {
          g_debug ("%s: calling change members on publish channel", G_STRFUNC);
          _gabble_roster_channel_change_members (priv->publish_channel,
              "", pub_add, pub_rem, empty, empty);
        }

      if (g_intset_size (sub_add) > 0 ||
          g_intset_size (sub_rem) > 0 ||
          g_intset_size (sub_rp) > 0)
        {
          g_debug ("%s: calling change members on subscribe channel", G_STRFUNC);
          _gabble_roster_channel_change_members (priv->subscribe_channel,
              "", sub_add, sub_rem, empty, sub_rp);
        }

      g_intset_destroy (empty);
      g_intset_destroy (pub_add);
      g_intset_destroy (pub_rem);
      g_intset_destroy (sub_add);
      g_intset_destroy (sub_rem);
      g_intset_destroy (sub_rp);
    }
  else
    {
      HANDLER_DEBUG (iq_node, "unhandled roster IQ");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* if this is a SET, it's a roster push, so we need to send an
   * acknowledgement */
  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_SET)
    {
      const char *id;

      id = lm_message_node_get_attribute (iq_node, "id");
      if (id == NULL)
        {
          HANDLER_DEBUG (iq_node, "got roster iq set with no id, not replying");
        }
      else
        {
          LmMessage *reply;

          HANDLER_DEBUG (iq_node, "acknowledging roster push");

          reply = lm_message_new_with_sub_type (NULL,
              LM_MESSAGE_TYPE_IQ,
              LM_MESSAGE_SUB_TYPE_RESULT);
          lm_message_node_set_attribute (reply->node, "id", id);
          _gabble_connection_send (conn, reply, NULL);
          lm_message_unref (reply);
        }
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * media_channel_closed_cb:
 *
 * Signal callback for when a media channel is closed. Removes the references
 * that #GabbleConnection holds to them.
 */
static void
media_channel_closed_cb (GabbleMediaChannel *chan, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("%s: removing media channel %p with ref count %d", G_STRFUNC, chan, G_OBJECT (chan)->ref_count);

  g_ptr_array_remove (priv->media_channels, chan);

  g_object_unref (chan);
}

/**
 * new_media_channel
 *
 * Creates a new empty GabbleMediaChannel.
 */
static GabbleMediaChannel *
new_media_channel (GabbleConnection *conn, GabbleHandle creator, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv;
  GabbleMediaChannel *chan;
  gchar *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  object_path = g_strdup_printf ("%s/MediaChannel%u", priv->object_path,
                                 priv->media_channel_index);

  chan = g_object_new (GABBLE_TYPE_MEDIA_CHANNEL,
                       "connection", conn,
                       "object-path", object_path,
                       "creator", creator,
                       NULL);

  g_debug ("%s: object path %s", G_STRFUNC, object_path);

  g_signal_connect (chan, "closed", (GCallback) media_channel_closed_cb, conn);

  g_ptr_array_add (priv->media_channels, chan);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
                 0, 0, suppress_handler);

  g_free (object_path);

  return chan;
}

/**
 * _gabble_connection_send_iq_ack
 *
 * Function used to acknowledge an IQ stanza.
 */
void
_gabble_connection_send_iq_ack (GabbleConnection *conn, LmMessageNode *iq_node, LmMessageSubType type)
{
  const gchar *to, *id;
  LmMessage *msg;

  to = lm_message_node_get_attribute (iq_node, "from");
  id = lm_message_node_get_attribute (iq_node, "id");

  msg = lm_message_new_with_sub_type (to, LM_MESSAGE_TYPE_IQ, type);
  lm_message_node_set_attribute (msg->node, "id", id);
  if (!_gabble_connection_send (conn, msg, NULL)) {
      g_warning ("%s: _gabble_connection_send failed", G_STRFUNC);
  }
  lm_message_unref (msg);
}

guint32
_gabble_connection_jingle_session_allocate (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;
  guint32 val;
  gboolean unique = FALSE;

  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  while (!unique)
    {
      gpointer k, v;

      val = g_random_int_range (1000000, G_MAXINT);

      unique = !g_hash_table_lookup_extended (priv->jingle_sessions,
                                              GUINT_TO_POINTER (val), &k, &v);
    }

  g_hash_table_insert (priv->jingle_sessions, GUINT_TO_POINTER (val), NULL);

  return val;
}

void
_gabble_connection_jingle_session_register (GabbleConnection *conn,
                                            guint32 sid,
                                            gpointer channel)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("%s: binding sid %d to %p", G_STRFUNC, sid, channel);

  g_hash_table_insert (priv->jingle_sessions, GUINT_TO_POINTER (sid), channel);
}

void
_gabble_connection_jingle_session_unregister (GabbleConnection *conn,
                                              guint32 sid)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("%s: unbinding sid %d", G_STRFUNC, sid);

  g_hash_table_insert (priv->jingle_sessions, GUINT_TO_POINTER (sid), NULL);
}

gboolean
_gabble_connection_contact_supports_voice (GabbleConnection *conn,
                                           GabbleHandle handle)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GQuark data_key;
  ContactPresence *cp;

  data_key = _get_contact_presence_quark ();
  cp = gabble_handle_get_qdata (priv->handles, TP_HANDLE_TYPE_CONTACT,
                                handle, data_key);

  return (cp && cp->voice_resource);
}

/**
 * connection_iq_jingle_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with jingle session queries, and allows other
 * handlers to be called for other queries.
 */
static LmHandlerResult
connection_iq_jingle_cb (LmMessageHandler *handler,
                         LmConnection *connection,
                         LmMessage *message,
                         gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *iq_node, *session_node, *desc_node;
  const gchar *from, *id, *type, *action, *sid_str;
  GabbleHandle handle;
  guint32 sid;
  GabbleMediaChannel *chan = NULL;
  gpointer k, v;

  g_assert (connection == priv->conn);

  iq_node = lm_message_get_node (message);
  session_node = lm_message_node_get_child (iq_node, "session");

  /* is it for us? */
  if (!session_node || strcmp (lm_message_node_get_attribute (session_node, "xmlns"),
        "http://www.google.com/session"))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* determine the jingle action of the request */
  action = lm_message_node_get_attribute (session_node, "type");
  if (!action)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  from = lm_message_node_get_attribute (iq_node, "from");
  if (!from)
    {
      HANDLER_DEBUG (iq_node, "'from' attribute not found");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  id = lm_message_node_get_attribute (iq_node, "id");
  if (!id)
    {
      HANDLER_DEBUG (iq_node, "'id' attribute not found");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  type = lm_message_node_get_attribute (iq_node, "type");
  if (!type)
    {
      HANDLER_DEBUG (iq_node, "'type' attribute not found");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (strcmp (type, "set") != 0)
    {
      g_warning ("%s: ignoring jingle iq stanza with type \"%s\"",
                 G_STRFUNC, type);
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = gabble_handle_for_contact (priv->handles, from, FALSE);

  /* does the session exist? */
  sid_str = lm_message_node_get_attribute (session_node, "id");
  if (!sid_str)
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  sid = atoi(sid_str);



  /* is the session new and not a zombie? */
  if (g_hash_table_lookup_extended (priv->jingle_sessions,
                                    GUINT_TO_POINTER (sid),
                                    &k, &v))
    {
      chan = (GabbleMediaChannel *) v;
    }
  else
    {
      /* if the session is unknown, the only allowed action is "initiate" */
      if (strcmp (action, "initiate"))
        return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

      desc_node = lm_message_node_get_child (session_node, "description");
      if (!desc_node)
        return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

      if (strcmp (lm_message_node_get_attribute (desc_node, "xmlns"),
                  "http://www.google.com/session/phone"))
        {
          g_debug ("%s: ignoring unknown session description", G_STRFUNC);
          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }

      g_debug ("%s: creating media channel", G_STRFUNC);

      chan = new_media_channel (conn, handle, FALSE);
    }

  if (chan)
    {
      g_object_ref (chan);
      _gabble_media_channel_dispatch_session_action (chan, handle, sid, iq_node, session_node, action);
      g_object_unref (chan);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * connection_iq_unknown_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler is
 * at a lower priority than the others, and should reply with an error
 * about unsupported get/set attempts.
 */
static LmHandlerResult
connection_iq_unknown_cb (LmMessageHandler *handler,
                          LmConnection *connection,
                          LmMessage *message,
                          gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *iq_node;

  g_assert (connection == priv->conn);

  iq_node = lm_message_get_node (message);
  HANDLER_DEBUG (iq_node, "got unknown iq");

  /* TODO: return an IQ error for unknown get/set */

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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

  g_debug ("%s called: %s", G_STRFUNC, reason);

  if (response == LM_SSL_RESPONSE_CONTINUE)
    g_debug ("proceeding anyway!");
  else
    connection_disconnect (conn, TP_CONN_STATUS_REASON_ENCRYPTION_ERROR);

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
      g_debug ("%s failed", G_STRFUNC);

      connection_status_change (conn, TP_CONN_STATUS_DISCONNECTED,
                                TP_CONN_STATUS_REASON_NETWORK_ERROR);

      return;
    }

  g_debug ("%s: authenticating with username: %s, password: %s, resource: %s",
           G_STRFUNC, priv->username, priv->password, priv->resource);

  if (!lm_connection_authenticate (lmconn, priv->username, priv->password,
                                   priv->resource, connection_auth_cb,
                                   conn, NULL, &error))
    {
      g_debug ("%s failed: %s", G_STRFUNC, error->message);
      g_error_free (error);

      /* the reason this function can fail is through network errors,
       * authentication failures are reported to our auth_cb */
      connection_status_change (conn, TP_CONN_STATUS_DISCONNECTED,
                                TP_CONN_STATUS_REASON_NETWORK_ERROR);
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
  LmMessage *message = NULL;
  LmMessageNode *node;
  GError *error = NULL;

  g_assert (priv);
  g_assert (lmconn == priv->conn);

  if (!success)
    {
      g_debug ("%s failed", G_STRFUNC);

      connection_disconnect (conn,
        TP_CONN_STATUS_REASON_AUTHENTICATION_FAILED);

      return;
    }

  /* go go gadget on-line */
  connection_status_change (conn, TP_CONN_STATUS_CONNECTED, TP_CONN_STATUS_REASON_REQUESTED);

  /* send presence to the server to indicate availability */
  if (!signal_own_presence (conn, &error))
    {
      g_debug ("%s: sending initial presence failed: %s", G_STRFUNC,
          error->message);
      goto ERROR;
    }

  /* send <iq type="get"><query xmnls="jabber:iq:roster" /></iq> to
   * request the roster */
  message = lm_message_new_with_sub_type (NULL,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_MESSAGE_SUB_TYPE_GET);
  node = lm_message_node_add_child (lm_message_get_node (message),
                                    "query", NULL);
  lm_message_node_set_attribute (node, "xmlns", XMLNS_ROSTER);

  if (!lm_connection_send (lmconn, message, &error))
    {
      g_debug ("%s: initial roster request failed: %s",
               G_STRFUNC, error->message);

      goto ERROR;
    }

  lm_message_unref (message);

  make_roster_channels (conn);

  discover_services (conn);

  return;

ERROR:
  if (error)
    g_error_free (error);

  if (message)
    lm_message_unref(message);

  connection_disconnect (conn, TP_CONN_STATUS_REASON_NETWORK_ERROR);
}

/**
 * make_roster_channels
 */
static void
make_roster_channels (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;
  GabbleHandle handle;
  char *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->publish_channel == NULL);
  g_assert (priv->subscribe_channel == NULL);

  /* make publish list channel */
  handle = gabble_handle_for_list_publish (priv->handles);
  object_path = g_strdup_printf ("%s/RosterChannelPublish", priv->object_path);

  priv->publish_channel = g_object_new (GABBLE_TYPE_ROSTER_CHANNEL,
                                        "connection", conn,
                                        "object-path", object_path,
                                        "handle", handle,
                                        NULL);

  g_debug ("%s: created %s", G_STRFUNC, object_path);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
                 TP_HANDLE_TYPE_LIST, handle,
                 /* suppress handler: */ FALSE);

  _gabble_roster_channel_change_group_flags (priv->publish_channel,
      TP_CHANNEL_GROUP_FLAG_CAN_REMOVE, 0);

  g_free (object_path);

  /* make subscribe list channel */
  handle = gabble_handle_for_list_subscribe (priv->handles);
  object_path = g_strdup_printf ("%s/RosterChannelSubscribe", priv->object_path);

  priv->subscribe_channel = g_object_new (GABBLE_TYPE_ROSTER_CHANNEL,
                                          "connection", conn,
                                          "object-path", object_path,
                                          "handle", handle,
                                          NULL);

  g_debug ("%s: created %s", G_STRFUNC, object_path);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
                 TP_HANDLE_TYPE_LIST, handle,
                 /* supress handler: */ FALSE);

  _gabble_roster_channel_change_group_flags (priv->subscribe_channel,
      TP_CHANNEL_GROUP_FLAG_CAN_ADD ^
      TP_CHANNEL_GROUP_FLAG_CAN_REMOVE ^
      TP_CHANNEL_GROUP_FLAG_CAN_RESCIND, 0);

  g_free (object_path);
}

static void
roomlist_channel_closed_cb (GabbleRoomlistChannel *chan, gpointer data)
{
  GabbleConnection *conn = data;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  g_object_unref (priv->roomlist_channel);
  priv->roomlist_channel = NULL;
}

static void
make_roomlist_channel (GabbleConnection *conn, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (!priv->roomlist_channel)
    {
      gchar *object_path;

      object_path =
        g_strdup_printf ("%s/RoomlistChannel", priv->object_path);
      priv->roomlist_channel =
        gabble_roomlist_channel_new (conn, priv->disco, object_path,
            priv->conference_servers->data);

      g_signal_connect (priv->roomlist_channel, "closed",
                        (GCallback) roomlist_channel_closed_cb, conn);

      g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                     object_path, TP_IFACE_CHANNEL_TYPE_ROOM_LIST,
                     0, 0,
                     suppress_handler);

      g_free (object_path);

    }

}
static void
service_info_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
                 LmMessageNode *result, GError *error,
                 gpointer user_data)
{
  LmMessageNode *identity, *feature;
  gboolean is_muc = FALSE;
  const char *category, *type, *var;
  GabbleConnection *conn = user_data;
  GabbleConnectionPrivate *priv;
  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (error)
    {
      g_debug ("%s: got error %s", G_STRFUNC, error->message);
      return;
    }
  g_debug ("%s: got %s", G_STRFUNC, lm_message_node_to_string (result));

  identity = lm_message_node_get_child (result, "identity");
  if (identity)
    {
      category = lm_message_node_get_attribute (identity, "category");
      type = lm_message_node_get_attribute (identity, "type");
      g_debug ("%s: got identity, category=%s, type=%s", G_STRFUNC,
               category, type);
      if (category && 0 == strcmp (category, "conference")
                   && 0 == strcmp (type, "text"))
        {
          for (feature = result->children; feature; feature = feature->next)
            {
              g_debug ("%s: Got child %s", G_STRFUNC, 
                       lm_message_node_to_string (feature));
              if (0 == strcmp (feature->name, "feature"))
                {
                  var = lm_message_node_get_attribute (feature, "var");
                  if (var &&
                      0 == strcmp (var, "http://jabber.org/protocol/muc"))
                    {
                      is_muc = TRUE;
                      break;
                    }
                }
            }
          if (is_muc)
            {
              g_debug ("%s: Adding conference server %s", G_STRFUNC, jid);
              priv->conference_servers =
                g_list_prepend (priv->conference_servers, g_strdup (jid));
            }
        }
    }
}

static void
services_discover_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
                      LmMessageNode *result, GError *error,
                      gpointer user_data)
{
  LmMessageNode *iter;
  GabbleConnection *conn = user_data;
  GabbleConnectionPrivate *priv;
  const char *item_jid;
  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (error)
    {
      g_debug ("%s: got error %s", G_STRFUNC, error->message);
      return;
    }
  g_debug ("%s: got %s", G_STRFUNC, lm_message_node_to_string (result));

  iter = result->children;

  for (; iter; iter = iter->next)
    {
      if (0 == strcmp (iter->name, "item"))
        {
          item_jid = lm_message_node_get_attribute (iter, "jid");
          if (item_jid)
            gabble_disco_request (priv->disco, GABBLE_DISCO_TYPE_INFO,
                                  item_jid, NULL,
                                  service_info_cb, conn, G_OBJECT (conn), NULL);
        }
    }
}

static void
discover_services (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;
  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  gabble_disco_request (priv->disco, GABBLE_DISCO_TYPE_INFO,
                        priv->connect_server, NULL,
                        services_discover_cb, conn, G_OBJECT(conn), NULL);
}


/**
 * im_channel_closed_cb:
 *
 * Signal callback for when an IM channel is closed. Removes the references
 * that #GabbleConnection holds to them.
 */
static void
im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GabbleHandle contact_handle;

  g_object_get (chan, "handle", &contact_handle, NULL);

  g_debug ("%s: removing channel with handle %d", G_STRFUNC, contact_handle);
  g_hash_table_remove (priv->im_channels, GINT_TO_POINTER (contact_handle));
}

/**
 * new_im_channel
 */
static GabbleIMChannel *
new_im_channel (GabbleConnection *conn, GabbleHandle handle, gboolean suppress_handler)
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

  g_signal_connect (chan, "closed", (GCallback) im_channel_closed_cb, conn);

  g_hash_table_insert (priv->im_channels, GINT_TO_POINTER (handle), chan);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_TEXT,
                 TP_HANDLE_TYPE_CONTACT, handle,
                 suppress_handler);

  g_free (object_path);

  return chan;
}

static void
destroy_handle_sets (gpointer data)
{
  GabbleHandleSet *handle_set;

  handle_set = (GabbleHandleSet*) data;
  handle_set_destroy (handle_set);
}

/**
 * _gabble_connection_client_hold_handle:
 * @conn: a #GabbleConnection
 * @client_name: DBus bus name of client to hold ahandle for
 * @handle: handle to hold
 * @type: type of handle to hold
 *
 * Marks a handle as held by a given client.
 *
 * Returns: false if client didn't hold this handle
 */
void
_gabble_connection_client_hold_handle (GabbleConnection *conn,
                                      gchar* client_name,
                                      GabbleHandle handle,
                                      TpHandleType type)
{
  GabbleConnectionPrivate *priv;
  GabbleHandleSet *handle_set;
  GData **handle_set_list;
  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  switch (type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handle_set_list = &priv->client_contact_handle_sets;
      break;
    case TP_HANDLE_TYPE_ROOM:
      handle_set_list = &priv->client_room_handle_sets;
      break;
    case TP_HANDLE_TYPE_LIST:
      handle_set_list = &priv->client_list_handle_sets;
      break;
    default:
      g_critical ("%s: gabble_connection_client_hold_handle called with invalid handle type", G_STRFUNC);
      return;
    }

  handle_set = (GabbleHandleSet*) g_datalist_get_data (handle_set_list, client_name);

  if (!handle_set)
    {
      handle_set = handle_set_new (priv->handles, type);
      g_datalist_set_data_full (handle_set_list, client_name, handle_set, destroy_handle_sets);
    }

  handle_set_add (handle_set, handle);

}

/**
 * _gabble_connection_client_release_handle:
 * @conn: a #GabbleConnection
 * @client_name: DBus bus name of client to release handle for
 * @handle: handle to release
 * @type: type of handle to release
 *
 * Releases a handle held by a given client
 *
 * Returns: false if client didn't hold this handle
 */
gboolean
_gabble_connection_client_release_handle (GabbleConnection *conn,
                                         gchar* client_name,
                                         GabbleHandle handle,
                                         TpHandleType type)
{
  GabbleConnectionPrivate *priv;
  GabbleHandleSet *handle_set;
  GData **handle_set_list;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  switch (type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handle_set_list = &priv->client_contact_handle_sets;
      break;
    case TP_HANDLE_TYPE_ROOM:
      handle_set_list = &priv->client_room_handle_sets;
      break;
    case TP_HANDLE_TYPE_LIST:
      handle_set_list = &priv->client_list_handle_sets;
      break;
    default:
      g_critical ("%s called with invalid handle type", G_STRFUNC);
      return FALSE;
    }

  handle_set = (GabbleHandleSet *) g_datalist_get_data (handle_set_list,
                                                       client_name);

  if (handle_set)
    return handle_set_remove (handle_set, handle);
  else
    return FALSE;
}

static GHashTable *
get_statuses_arguments()
{
  static GHashTable *arguments = NULL;

  if (arguments == NULL)
    {
      arguments = g_hash_table_new (g_str_hash, g_str_equal);

      g_hash_table_insert (arguments, "message", "s");
    }

  return arguments;
}

/****************************************************************************
 *                          D-BUS EXPORTED METHODS                          *
 ****************************************************************************/


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
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error);

  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
      "Only one status is possible at a time with this protocol");

  return FALSE;
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
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error);


  add = NULL;
  remove = NULL;
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
  GabbleConnectionPrivate *priv;
  ContactPresence *cp;
  GQuark data_key = _get_contact_presence_quark();
  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error);

  cp = gabble_handle_get_qdata (priv->handles,
      TP_HANDLE_TYPE_CONTACT, priv->self_handle, data_key);

  update_presence (obj, priv->self_handle, GABBLE_PRESENCE_AVAILABLE, NULL, NULL);
  return signal_own_presence (obj, error);
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

  connection_disconnect (obj, TP_CONN_STATUS_REASON_REQUESTED);

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
  GValue vals ={0.};
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error);

  if (!gabble_handle_is_valid(priv->handles, TP_HANDLE_TYPE_CONTACT, handle))
    {
      g_debug ("get_capabilites: invalid handle %u", handle);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid handle %u", handle);

      return FALSE;
    }

  *ret = g_ptr_array_sized_new (1);

  g_value_init (&vals, TP_CAPABILITY_PAIR_TYPE);
  g_value_set_static_boxed (&vals,
    dbus_g_type_specialized_construct (TP_CAPABILITY_PAIR_TYPE));

  dbus_g_type_struct_set (&vals,
                        0, TP_IFACE_CHANNEL_TYPE_TEXT,
                        1, TP_CONN_CAPABILITY_TYPE_CREATE,
                        G_MAXUINT);

  g_ptr_array_add (*ret, g_value_get_boxed (&vals));

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
  const char *interfaces[] = {
      TP_IFACE_CONN_INTERFACE_PRESENCE,
      TP_IFACE_CONN_INTERFACE_CAPABILITIES,
      TP_IFACE_CONN_INTERFACE_CONTACT_INFO,
      NULL };
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

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

  ERROR_IF_NOT_CONNECTED (priv, *error)

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

  ERROR_IF_NOT_CONNECTED (priv, *error)

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
  GabbleConnectionPrivate *priv;
  GValueArray *status;
  int i;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

  g_debug ("%s called.", G_STRFUNC);

  *ret = g_hash_table_new_full (g_str_hash, g_str_equal,
                                NULL, (GDestroyNotify) g_value_array_free);

  for (i=0; i < LAST_GABBLE_PRESENCE; i++)
    {
      status = g_value_array_new (5);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (status, 0),
          gabble_statuses[i].presence_type);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 1), G_TYPE_BOOLEAN);
      g_value_set_boolean (g_value_array_get_nth (status, 1),
          gabble_statuses[i].self);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 2), G_TYPE_BOOLEAN);
      g_value_set_boolean (g_value_array_get_nth (status, 2),
          gabble_statuses[i].exclusive);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 3),
          DBUS_TYPE_G_STRING_STRING_HASHTABLE);
      g_value_set_static_boxed (g_value_array_get_nth (status, 3),
          get_statuses_arguments());

      g_hash_table_insert (*ret, (gchar*) gabble_statuses[i].name, status);

    }
  return TRUE;
}


/**
 * gabble_connection_hold_handle
 *
 * Implements DBus method HoldHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_hold_handle (GabbleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  gboolean valid;
  GError *error = NULL;
  gchar *sender;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (priv, error, context)

  if (!gabble_handle_type_is_valid (handle_type))
    {
      g_debug ("hold_handle: invalid handle type %u", handle_type);

      error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                           "invalid handle type %u", handle_type);
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  valid = gabble_handle_ref (priv->handles, handle_type, handle);

  if (!valid)
    {
      g_debug ("hold_handle: unknown handle %u", handle);

      error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                            "unknown handle %u", handle);
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  sender = dbus_g_method_get_sender (context);
  _gabble_connection_client_hold_handle (obj, sender, handle, handle_type);
  dbus_g_method_return (context);

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

  ERROR_IF_NOT_CONNECTED (priv, *error)

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
 * list_channel_hash_foreach:
 * @key: iterated key
 * @value: iterated value
 * @data: data attached to this key/value pair
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
  guint i;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

  count = g_hash_table_size (priv->im_channels);
  count += g_hash_table_size (priv->muc_channels);
  count += priv->media_channels->len;

  channels = g_ptr_array_sized_new (count);

  g_hash_table_foreach (priv->im_channels, list_channel_hash_foreach, channels);

  g_hash_table_foreach (priv->muc_channels, list_channel_hash_foreach, channels);

  for (i = 0; i < priv->media_channels->len; i++)
    {
      list_channel_hash_foreach (NULL, g_ptr_array_index (priv->media_channels, i), channels);
    }

  if (priv->publish_channel)
    list_channel_hash_foreach (NULL, priv->publish_channel, channels);

  if (priv->subscribe_channel)
    list_channel_hash_foreach (NULL, priv->subscribe_channel, channels);

  *ret = channels;

  return TRUE;
}


/**
 * gabble_connection_release_handle
 *
 * Implements DBus method ReleaseHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_release_handle (GabbleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  gboolean valid;
  char *sender;
  GError *error = NULL;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (priv, error, context)

  if (!gabble_handle_type_is_valid (handle_type))
    {
      g_debug ("release_handle: invalid handle type %u", handle_type);

      error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                           "invalid handle type %u", handle_type);
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  valid = gabble_handle_is_valid (priv->handles, handle_type, handle);

  if (!valid)
    {
      g_debug ("release_handle: invalid handle %u", handle);

      error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                            "unknown handle %u", handle);
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  sender = dbus_g_method_get_sender (context);
  _gabble_connection_client_release_handle (obj, sender, handle, handle_type);
  dbus_g_method_return (context);

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
  GQuark data_key = _get_contact_presence_quark();
  GabbleConnectionPrivate *priv;
  ContactPresence *cp;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

  cp = gabble_handle_get_qdata (priv->handles,
      TP_HANDLE_TYPE_CONTACT, priv->self_handle, data_key);

  if (strcmp (status, gabble_statuses[cp->presence_id].name) == 0)
    {
      update_presence (obj, priv->self_handle, GABBLE_PRESENCE_AVAILABLE, NULL, NULL);
      return signal_own_presence (obj, error);
    }
  else
    {
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "Attempting to remove non-existent presence.");
      return FALSE;
    }
}

static GabbleMediaChannel *
find_media_channel_with_handle (GabbleConnection *conn, GabbleHandle handle)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  guint i, j;

  for (i = 0; i < priv->media_channels->len; i++)
    {
      GArray *arr;
      GError *err;

      GabbleMediaChannel *chan = g_ptr_array_index (priv->media_channels, i);

      /* search members */
      if (!gabble_group_mixin_get_members (G_OBJECT (chan), &arr, &err))
        {
          g_debug ("%s: get_members failed", G_STRFUNC);
          g_free (err);
          continue;
        }

      for (j = 0; j < arr->len; j++)
        if (g_array_index (arr, guint32, i) == handle)
          {
            g_array_free (arr, TRUE);
            return chan;
          }

      g_array_free (arr, TRUE);

      /* search local pending */
      if (!gabble_group_mixin_get_local_pending_members (G_OBJECT (chan), &arr, &err))
        {
          g_debug ("%s: get_local_pending_members failed", G_STRFUNC);
          g_free (err);
          continue;
        }

      for (j = 0; j < arr->len; j++)
        if (g_array_index (arr, guint32, i) == handle)
          {
            g_array_free (arr, TRUE);
            return chan;
          }

      g_array_free (arr, TRUE);

      /* search remote pending */
      if (!gabble_group_mixin_get_remote_pending_members (G_OBJECT (chan), &arr, &err))
        {
          g_debug ("%s: get_remote_pending_members failed", G_STRFUNC);
          g_free (err);
          continue;
        }

      for (j = 0; j < arr->len; j++)
        if (g_array_index (arr, guint32, i) == handle)
          {
            g_array_free (arr, TRUE);
            return chan;
          }

      g_array_free (arr, TRUE);
    }

  return NULL;
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
gboolean gabble_connection_request_channel (GabbleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean suppress_handler, gchar ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

  if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      GObject *chan;

      if (!gabble_handle_is_valid (priv->handles,
                                   handle_type,
                                   handle))
        {
          goto INVALID_HANDLE;
        }

      if (handle_type == TP_HANDLE_TYPE_CONTACT)
        {
          chan = g_hash_table_lookup (priv->im_channels, GINT_TO_POINTER (handle));

          if (chan == NULL)
            {
              chan = G_OBJECT (new_im_channel (obj, handle, suppress_handler));
            }
        }
      else if (handle_type == TP_HANDLE_TYPE_ROOM)
        {
          chan = g_hash_table_lookup (priv->muc_channels, GINT_TO_POINTER (handle));

          if (chan == NULL)
            {
              GArray *members;
              gboolean ret;

              chan = G_OBJECT (new_muc_channel (obj, handle, suppress_handler));

              members = g_array_sized_new (FALSE, FALSE, sizeof (GabbleHandle), 1);
              g_array_append_val (members, priv->self_handle);

              ret = gabble_group_mixin_add_members (chan, members, "", error);

              g_array_free (members, TRUE);

              if (!ret)
                {
                  GError *close_err;

                  if (!gabble_muc_channel_close (GABBLE_MUC_CHANNEL (chan),
                                                 &close_err))
                    {
                      g_error_free (close_err);
                    }

                  return FALSE;
                }
            }
        }
      else
        {
          goto NOT_AVAILABLE;
        }

      if (chan)
        {
          g_object_get (chan, "object-path", ret, NULL);
        }
      else
        {
          goto NOT_AVAILABLE;
        }
    }
  else if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
    {
      GabbleRosterChannel *chan;

      if (handle_type != TP_HANDLE_TYPE_LIST)
        goto NOT_AVAILABLE;

      if (handle == gabble_handle_for_list_publish (priv->handles))
        chan = priv->publish_channel;
      else if (handle == gabble_handle_for_list_subscribe (priv->handles))
        chan = priv->subscribe_channel;
      else
        goto INVALID_HANDLE;

      g_object_get (chan, "object-path", ret, NULL);
    }
  else if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    {
      GabbleMediaChannel *chan;

      if (handle_type == 0)
        {
          /* create an empty channel */
          chan = new_media_channel (obj, priv->self_handle, suppress_handler);
        }
      else
        {
          /* have we already got a channel with this handle? */
          chan = find_media_channel_with_handle (obj, handle);

          /* no: create it and add the peer to it */
          if (!chan)
            {
              GArray *members;
              gboolean ret;

              chan = new_media_channel (obj, priv->self_handle, suppress_handler);

              members = g_array_sized_new (FALSE, FALSE, sizeof (GabbleHandle), 1);
              g_array_append_val (members, handle);

              ret = gabble_group_mixin_add_members (G_OBJECT (chan), members, "", error);

              g_array_free (members, TRUE);

              if (!ret)
                {
                  GError *close_err;

                  if (!gabble_media_channel_close (chan, &close_err))
                    {
                      g_error_free (close_err);
                    }

                  return FALSE;
                }
            }
        }

      g_object_get (chan, "object-path", ret, NULL);
    }
  else if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    {
      if (!priv->conference_servers)
        goto NOT_AVAILABLE;
      make_roomlist_channel (obj, suppress_handler);
      g_object_get (priv->roomlist_channel, "object-path", ret, NULL);
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


static LmHandlerResult
contact_info_got_vcard (GabbleConnection *conn, LmMessage *sent_msg,
                        LmMessage *reply_msg, GObject *object,
                        gpointer user_data)
{
  GabbleHandle contact = GPOINTER_TO_INT (user_data);
  LmMessageNode *node, *child;
  node = lm_message_node_get_child (reply_msg->node, "vCard");
  GString *vcard = g_string_new("");
  gchar *str;

  child = node->children;
  while (child)
    {
      if (0 != strcmp (child->name, "PHOTO")
       && 0 != strcmp (child->name, "photo"))
        {
          str = lm_message_node_to_string (child);
          g_string_append_printf (vcard, "  %s", str);
          g_free (str);
        }
    }

  g_signal_emit (conn, signals[GOT_CONTACT_INFO], 0, contact, vcard->str);

  g_string_free (vcard, TRUE);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * gabble_connection_request_contact_info
 *
 * Implements DBus method RequestContactInfo
 * on interface org.freedesktop.Telepathy.Connection.Interface.ContactInfo
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_contact_info (GabbleConnection *obj, guint contact, GError **error)
{
  GabbleConnectionPrivate *priv;
  LmMessage *msg;
  LmMessageNode *vcard_node;
  const char *contact_jid;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!gabble_handle_is_valid(priv->handles, TP_HANDLE_TYPE_CONTACT, contact))
    {
      g_debug ("%s: invalid handle %u", G_STRFUNC, contact);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid handle %u", contact);

      return FALSE;
    }

  contact_jid = gabble_handle_inspect (priv->handles, TP_HANDLE_TYPE_CONTACT,
                                       contact);

  /* build the message */
  msg = lm_message_new_with_sub_type (contact_jid, LM_MESSAGE_TYPE_IQ,
                                     LM_MESSAGE_SUB_TYPE_GET);

  vcard_node = lm_message_node_add_child (msg->node, "vCard", NULL);

  lm_message_node_set_attribute (vcard_node, "xmlns", "vcard-temp");


  if (!_gabble_connection_send_with_reply (obj, msg, contact_info_got_vcard,
                                           G_OBJECT(obj), GINT_TO_POINTER (contact),
                                           error))
    {
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

typedef struct {
    GabbleConnection *conn;
    gchar *jid;
    DBusGMethodInvocation *context;
} RoomVerifyContext;

static void
room_jid_disco_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
                   LmMessageNode *query_result, GError *error,
                   gpointer user_data)
{
  RoomVerifyContext *rvctx = user_data;
  LmMessageNode *lm_node;
  GabbleConnectionPrivate *priv;

  priv = GABBLE_CONNECTION_GET_PRIVATE (rvctx->conn);

  if (error != NULL)
    {
      goto ERROR;
    }

  for (lm_node = query_result->children; lm_node; lm_node = lm_node->next)
    {
      if (strcmp (lm_node->name, "feature") == 0)
        {
          const gchar *name;

          name = lm_message_node_get_attribute (lm_node, "var");
          if (name != NULL)
            {
              if (strcmp (name, MUC_XMLNS) == 0)
                {
                  gchar *sender;
                  GabbleHandle handle;

                  handle = gabble_handle_for_room (priv->handles, rvctx->jid);
                  g_assert (handle != 0);

                  sender = dbus_g_method_get_sender (rvctx->context);
                  _gabble_connection_client_hold_handle (rvctx->conn, sender, handle, TP_HANDLE_TYPE_ROOM);

                  g_debug ("%s: DISCO reported MUC support for service name in jid %s", G_STRFUNC, rvctx->jid);

                  dbus_g_method_return (rvctx->context, handle);

                  goto OUT;
                }
            }
        }
    }

ERROR:
  g_debug ("%s: DISCO reply error %s", G_STRFUNC, error->message);
  dbus_g_method_return_error (rvctx->context, error);

OUT:
  g_free (rvctx->jid);
  g_free (rvctx);
}

/**
 * room_jid_verify:
 *
 * Utility function that verifies that the service name of
 * the specified jid exists and reports MUC support.
 */
static gboolean
room_jid_verify (GabbleConnection *conn, const gchar *jid,
                 DBusGMethodInvocation *context, GError **error)
{
  GabbleConnectionPrivate *priv;
  gchar *room, *service;
  gboolean ret;
  RoomVerifyContext *rvctx;

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  room = service = NULL;
  gabble_handle_decode_jid (jid, &room, &service, NULL);

  g_assert (room && service);

  rvctx = g_new (RoomVerifyContext, 1);
  rvctx->conn = conn;
  rvctx->jid = g_strdup (jid);
  rvctx->context = context;

  ret = (gabble_disco_request (priv->disco, GABBLE_DISCO_TYPE_INFO, service, NULL,
                               room_jid_disco_cb, rvctx, G_OBJECT (conn), error) != NULL);

  g_free (room);
  g_free (service);

  return ret;
}

/**
 * gabble_connection_request_handle
 *
 * Implements DBus method RequestHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_request_handle (GabbleConnection *obj, guint handle_type, const gchar * name, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  GabbleHandle handle;
  gchar *sender;
  GError *error = NULL;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (priv, error, context)

  if (!gabble_handle_type_is_valid (handle_type))
    {
      g_debug ("request_handle: invalid handle type %u", handle_type);

      error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                           "invalid handle type %u", handle_type);
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
    case TP_HANDLE_TYPE_ROOM:

      if (!gabble_handle_jid_is_valid (handle_type, name, &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);

          return FALSE;
        }

      if (handle_type == TP_HANDLE_TYPE_CONTACT)
        {
          handle = gabble_handle_for_contact (priv->handles, name, FALSE);

          if (handle == 0)
            {
              g_debug ("%s: requested handle %s was invalid", G_STRFUNC, name);

              error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                                   "requested handle %s was invalid", name);
              dbus_g_method_return_error (context, error);
              g_error_free (error);

              return FALSE;
            }
        }
      else /* TP_HANDLE_TYPE_ROOM */
        {
          /* has the handle been verified before? */
          if (gabble_handle_for_room_exists (priv->handles, name))
            {
              handle = gabble_handle_for_room (priv->handles, name);
            }
          else
            {
              /* verify it */
              if (room_jid_verify (obj, name, context, &error))
                {
                  return TRUE;
                }
              else
                {
                  dbus_g_method_return_error (context, error);
                  g_error_free (error);

                  return FALSE;
                }
            }
        }

      break;
    case TP_HANDLE_TYPE_LIST:
      if (!strcmp (name, "publish"))
        {
          handle = gabble_handle_for_list_publish (priv->handles);
        }
      else if (!strcmp (name, "subscribe"))
        {
          handle = gabble_handle_for_list_subscribe (priv->handles);
        }
      else
        {
          g_debug ("%s: requested list channel %s not available", G_STRFUNC, name);

          error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                               "requested list channel %s not available", name);
          dbus_g_method_return_error (context, error);
          g_error_free (error);

          return FALSE;
        }
      break;
    default:
      g_debug ("%s: unimplemented handle type %u", G_STRFUNC, handle_type);

      error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                          "unimplemented handle type %u", handle_type);
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  sender = dbus_g_method_get_sender (context);
  _gabble_connection_client_hold_handle (obj, sender, handle, handle_type);
  dbus_g_method_return (context, handle);

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
  GabbleConnectionPrivate *priv;
  GabbleHandle *handles = (GabbleHandle *)contacts->data;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

  /*TODO; what do we do about requests for non-rostered contacts?*/

  if (contacts->len)
    {
      emit_presence_update (obj, handles);
    }

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
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

  return TRUE;
}

struct _i_hate_g_hash_table_foreach
{
  GabbleConnection *conn;
  GError **error;
  gboolean retval;
};

static void
setstatuses_foreach (gpointer key, gpointer value, gpointer user_data)
{
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach*) user_data;

  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (data->conn);
  int i;


  for (i = 0; i < LAST_GABBLE_PRESENCE; i++)
    {
      if (0 == strcmp (gabble_statuses[i].name, (const gchar*) key))
        break;
    }

  if (i < LAST_GABBLE_PRESENCE)
    {
      GHashTable *args = (GHashTable *)value;
      GValue *message = g_hash_table_lookup (args, "message");
      const gchar *status = NULL;

      if (message)
        {
          if (!G_VALUE_HOLDS_STRING (message))
            {
              g_debug ("%s: got a status message which was not a string", G_STRFUNC);
              *(data->error) = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                 "Status argument 'message' requires a string");
              data->retval = FALSE;
              return;
            }
          status = g_value_get_string (message);
        }

      update_presence (data->conn, priv->self_handle, i, status, NULL);
      data->retval = signal_own_presence (data->conn, data->error);
    }
  else
    {
      g_debug ("%s: got unknown status identifier %s", G_STRFUNC, (const gchar *) key);
      *(data->error) = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                    "unknown status identifier: %s",
                                    (const gchar *) key);
      data->retval = FALSE;
    }
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
  GabbleConnectionPrivate *priv;
  struct _i_hate_g_hash_table_foreach data = { NULL, NULL, TRUE };

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (priv, *error)

  if (g_hash_table_size (statuses) != 1)
    {
      g_debug ("%s: got more than one status", G_STRFUNC);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                 "Only one status may be set at a time in this protocol");
      return FALSE;
    }

  data.conn = obj;
  data.error = error;
  g_hash_table_foreach (statuses, setstatuses_foreach, &data);

  return data.retval;
}

