/*
 * ft-channel.c - Source for GabbleFileTransferChannel
 * Copyright (C) 2009 Collabora Ltd.
 *   @author: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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

#include <glib/gstdio.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include <gibber/gibber-sockets.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#define DEBUG_FLAG GABBLE_DEBUG_FT
#include "debug.h"

#include <loudmouth/loudmouth.h>

#include <gibber/gibber-listener.h>
#include <gibber/gibber-transport.h>
#include <gibber/gibber-unix-transport.h>       /* just for the feature-test */

#include "jingle-factory.h"
#include "jingle-session.h"
#include "jingle-share.h"
#include "bytestream-factory.h"
#include "connection.h"
#include "ft-channel.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "util.h"

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/svc-channel.h>

#include <nice/agent.h>


static void channel_iface_init (gpointer g_iface, gpointer iface_data);
static void file_transfer_iface_init (gpointer g_iface, gpointer iface_data);
static void nice_data_received_cb (NiceAgent *agent, guint stream_id,
    guint component_id, guint len, gchar *buffer, gpointer user_data);
static void transferred_chunk (GabbleFileTransferChannel *self, guint64 count);


G_DEFINE_TYPE_WITH_CODE (GabbleFileTransferChannel, gabble_file_transfer_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_FILE_TRANSFER,
                           file_transfer_iface_init);
);

#define GABBLE_UNDEFINED_FILE_SIZE G_MAXUINT64

static const char *gabble_file_transfer_channel_interfaces[] = { NULL };

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,

   /* org.freedesktop.Telepathy.Channel D-Bus properties */
  PROP_CHANNEL_TYPE,
  PROP_INTERFACES,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_HANDLE_TYPE,
  PROP_REQUESTED,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,

  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,

  /* org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties */
  PROP_STATE,
  PROP_CONTENT_TYPE,
  PROP_FILENAME,
  PROP_SIZE,
  PROP_CONTENT_HASH_TYPE,
  PROP_CONTENT_HASH,
  PROP_DESCRIPTION,
  PROP_DATE,
  PROP_AVAILABLE_SOCKET_TYPES,
  PROP_TRANSFERRED_BYTES,
  PROP_INITIAL_OFFSET,
  PROP_RESUME_SUPPORTED,

  PROP_CONNECTION,
  LAST_PROPERTY
};

typedef enum
  {
    HTTP_SERVER_IDLE,
    HTTP_SERVER_HEADERS,
    HTTP_SERVER_SEND,
    HTTP_CLIENT_IDLE,
    HTTP_CLIENT_RECEIVE,
    HTTP_CLIENT_HEADERS,
    HTTP_CLIENT_CHUNK_SIZE,
    HTTP_CLIENT_CHUNK_END,
    HTTP_CLIENT_CHUNK_FINAL,
    HTTP_CLIENT_BODY,
  } HttpStatus;


typedef struct
{
  NiceAgent *agent;
  guint stream_id;
  guint component_id;
  gboolean agent_attached;
  GabbleJingleShare *content;
  gint channel_id;
  HttpStatus http_status;
  gboolean is_chunked;
  guint64 content_length;
  gchar *write_buffer;
  guint write_len;
  gchar *read_buffer;
  guint read_len;
  gint manifest_entry;
} JingleChannel;

/* private structure */
struct _GabbleFileTransferChannelPrivate {
  gboolean dispose_has_run;
  gboolean closed;
  gchar *object_path;
  TpHandle handle;
  GabbleConnection *connection;
  GTimeVal last_transferred_bytes_emitted;
  guint progress_timer;
  TpSocketAddressType socket_type;
  GValue *socket_address;
  TpHandle initiator;
  gboolean remote_accepted;
  gboolean resume_supported;

  GabbleJingleSession *jingle;
  GHashTable *jingle_channels;

  GabbleBytestreamIface *bytestream;
  GibberListener *listener;
  GibberTransport *transport;

  /* properties */
  TpFileTransferState state;
  gchar *content_type;
  gchar *filename;
  guint64 size;
  TpFileHashType content_hash_type;
  gchar *content_hash;
  gchar *description;
  GHashTable *available_socket_types;
  guint64 transferred_bytes;
  guint64 initial_offset;
  guint64 date;
};


static void
gabble_file_transfer_channel_do_close (GabbleFileTransferChannel *self)
{
  if (self->priv->closed)
    return;

  DEBUG ("Emitting closed signal for %s", self->priv->object_path);
  tp_svc_channel_emit_closed (self);
  self->priv->closed = TRUE;
}

static void
gabble_file_transfer_channel_init (GabbleFileTransferChannel *obj)
{
  obj->priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      GABBLE_TYPE_FILE_TRANSFER_CHANNEL, GabbleFileTransferChannelPrivate);

  /* allocate any data required by the object here */
  obj->priv->object_path = NULL;
  obj->priv->connection = NULL;
}

static void gabble_file_transfer_channel_set_state (
    TpSvcChannelTypeFileTransfer *iface, TpFileTransferState state,
    TpFileTransferStateChangeReason reason);

static void
gabble_file_transfer_channel_get_property (GObject *object,
                                           guint property_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (object);
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->connection;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, self->priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value,
            TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_TARGET_ID:
        {
           TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
             TP_HANDLE_TYPE_CONTACT);

           g_value_set_string (value, tp_handle_inspect (repo,
                 self->priv->handle));
        }
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, self->priv->handle);
        break;
      case PROP_REQUESTED:
        g_value_set_boolean (value, (self->priv->initiator ==
              base_conn->self_handle));
        break;
      case PROP_INITIATOR_HANDLE:
        g_value_set_uint (value, self->priv->initiator);
        break;
      case PROP_INITIATOR_ID:
          {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (
                base_conn, TP_HANDLE_TYPE_CONTACT);

            g_assert (self->priv->initiator != 0);
            g_value_set_string (value,
                tp_handle_inspect (repo, self->priv->initiator));
          }
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
      case PROP_INTERFACES:
        g_value_set_boxed (value, gabble_file_transfer_channel_interfaces);
        break;
      case PROP_STATE:
        g_value_set_uint (value, self->priv->state);
        break;
      case PROP_CONTENT_TYPE:
        g_value_set_string (value, self->priv->content_type);
        break;
      case PROP_FILENAME:
        g_value_set_string (value, self->priv->filename);
        break;
      case PROP_SIZE:
        g_value_set_uint64 (value, self->priv->size);
        break;
      case PROP_CONTENT_HASH_TYPE:
        g_value_set_uint (value, self->priv->content_hash_type);
        break;
      case PROP_CONTENT_HASH:
        g_value_set_string (value, self->priv->content_hash);
        break;
      case PROP_DESCRIPTION:
        g_value_set_string (value, self->priv->description);
        break;
      case PROP_AVAILABLE_SOCKET_TYPES:
        g_value_set_boxed (value, self->priv->available_socket_types);
        break;
      case PROP_TRANSFERRED_BYTES:
        g_value_set_uint64 (value, self->priv->transferred_bytes);
        break;
      case PROP_INITIAL_OFFSET:
        g_value_set_uint64 (value, self->priv->initial_offset);
        break;
      case PROP_DATE:
        g_value_set_uint64 (value, self->priv->date);
        break;
      case PROP_CHANNEL_DESTROYED:
        g_value_set_boolean (value, self->priv->closed);
        break;
      case PROP_RESUME_SUPPORTED:
        g_value_set_boolean (value, self->priv->resume_supported);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_take_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "Interfaces",
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "Requested",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "State",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "ContentType",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Filename",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Size",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "ContentHashType",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "ContentHash",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Description",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Date",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "AvailableSocketTypes",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "TransferredBytes",
                TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "InitialOffset",
                NULL));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_file_transfer_channel_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (object);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (self->priv->object_path);
        self->priv->object_path = g_value_dup_string (value);
        break;
      case PROP_HANDLE:
        self->priv->handle = g_value_get_uint (value);
        break;
      case PROP_CONNECTION:
        self->priv->connection = g_value_get_object (value);
        break;
      case PROP_HANDLE_TYPE:
        g_assert (g_value_get_uint (value) == 0
                  || g_value_get_uint (value) == TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_CHANNEL_TYPE:
        /* these properties are writable in the interface, but not actually
         * meaningfully changeable on this channel, so we do nothing */
        break;
      case PROP_STATE:
        gabble_file_transfer_channel_set_state (
            TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (object),
            g_value_get_uint (value),
            TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
        break;
      case PROP_CONTENT_TYPE:
        g_free (self->priv->content_type);
        self->priv->content_type = g_value_dup_string (value);
        break;
      case PROP_FILENAME:
        g_free (self->priv->filename);
        self->priv->filename = g_value_dup_string (value);
        break;
      case PROP_SIZE:
        self->priv->size = g_value_get_uint64 (value);
        break;
      case PROP_CONTENT_HASH_TYPE:
        self->priv->content_hash_type = g_value_get_uint (value);
        break;
      case PROP_CONTENT_HASH:
        g_free (self->priv->content_hash);
        self->priv->content_hash = g_value_dup_string (value);
        break;
      case PROP_DESCRIPTION:
        g_free (self->priv->description);
        self->priv->description = g_value_dup_string (value);
        break;
      case PROP_INITIATOR_HANDLE:
        self->priv->initiator = g_value_get_uint (value);
        g_assert (self->priv->initiator != 0);
        break;
      case PROP_DATE:
        self->priv->date = g_value_get_uint64 (value);
        break;
      case PROP_INITIAL_OFFSET:
        self->priv->initial_offset = g_value_get_uint64 (value);
        break;
      case PROP_RESUME_SUPPORTED:
        self->priv->resume_supported = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
free_array (GArray *array)
{
  g_array_free (array, TRUE);
}

static void
connection_presences_updated_cb (GabblePresenceCache *cache,
                                 GArray *handles,
                                 GabbleFileTransferChannel *self)
{
  guint i;

  for (i = 0; i < handles->len ; i++)
    {
      TpHandle handle;

      handle = g_array_index (handles, TpHandle, i);
      if (handle == self->priv->handle)
        {
          GabblePresence *presence;

          presence = gabble_presence_cache_get (
              self->priv->connection->presence_cache, handle);

          if (presence == NULL || presence->status < GABBLE_PRESENCE_XA)
            {
              /* Contact is disconnected */
              if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED &&
                  self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
                {
                  DEBUG ("peer disconnected. FileTransfer is cancelled");

                  gabble_file_transfer_channel_set_state (
                      TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
                      TP_FILE_TRANSFER_STATE_CANCELLED,
                      TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_STOPPED);
                }
            }
        }
    }
}

static GObject *
gabble_file_transfer_channel_constructor (GType type,
                                          guint n_props,
                                          GObjectConstructParam *props)
{
  GObject *obj;
  GabbleFileTransferChannel *self;
  DBusGConnection *bus;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_repo;
  GArray *socket_access;
  TpSocketAccessControl access_control;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (gabble_file_transfer_channel_parent_class)->
          constructor (type, n_props, props);
  self = GABBLE_FILE_TRANSFER_CHANNEL (obj);
  base_conn = TP_BASE_CONNECTION (self->priv->connection);

  /* Ref the target and initiator handles; they can't be reffed in
   * _set_property as we may not have the TpConnection at that point.
   */
  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  tp_handle_ref (contact_repo, self->priv->handle);
  tp_handle_ref (contact_repo, self->priv->initiator);

  self->priv->object_path = g_strdup_printf ("%s/FileTransferChannel/%p",
      base_conn->object_path, self);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, self->priv->object_path, obj);

  /* Initialise the available socket types hash table */
  self->priv->available_socket_types = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) free_array);

#ifdef GIBBER_TYPE_UNIX_TRANSPORT
  /* Socket_Address_Type_Unix */
  socket_access = g_array_sized_new (FALSE, FALSE,
      sizeof (TpSocketAccessControl), 1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (socket_access, access_control);
  g_hash_table_insert (self->priv->available_socket_types,
      GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_UNIX), socket_access);
#endif

  /* Socket_Address_Type_IPv4 */
  socket_access = g_array_sized_new (FALSE, FALSE,
      sizeof (TpSocketAccessControl), 1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (socket_access, access_control);
  g_hash_table_insert (self->priv->available_socket_types,
      GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV4), socket_access);

  /* Socket_Address_Type_IPv6 */
  socket_access = g_array_sized_new (FALSE, FALSE,
      sizeof (TpSocketAccessControl), 1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (socket_access, access_control);
  g_hash_table_insert (self->priv->available_socket_types,
      GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV6), socket_access);

  gabble_signal_connect_weak (self->priv->connection->presence_cache,
      "presences-updated", G_CALLBACK (connection_presences_updated_cb), obj);

  DEBUG ("New FT channel created: %s (contact: %s, initiator: %s, "
      "file: \"%s\", size: %" G_GUINT64_FORMAT ")",
       self->priv->object_path,
       tp_handle_inspect (contact_repo, self->priv->handle),
       tp_handle_inspect (contact_repo, self->priv->initiator),
       self->priv->filename, self->priv->size);

  return obj;
}

static void close_session_and_transport (GabbleFileTransferChannel *self);
static void gabble_file_transfer_channel_dispose (GObject *object);
static void gabble_file_transfer_channel_finalize (GObject *object);

static void
gabble_file_transfer_channel_class_init (
    GabbleFileTransferChannelClass *gabble_file_transfer_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gabble_file_transfer_channel_class);
  GParamSpec *param_spec;

  static TpDBusPropertiesMixinPropImpl channel_props[] = {
    { "TargetHandleType", "handle-type", NULL },
    { "TargetHandle", "handle", NULL },
    { "TargetID", "target-id", NULL },
    { "ChannelType", "channel-type", NULL },
    { "Interfaces", "interfaces", NULL },
    { "Requested", "requested", NULL },
    { "InitiatorHandle", "initiator-handle", NULL },
    { "InitiatorID", "initiator-id", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinPropImpl file_props[] = {
    { "State", "state", NULL },
    { "ContentType", "content-type", NULL },
    { "Filename", "filename", NULL },
    { "Size", "size", NULL },
    { "ContentHashType", "content-hash-type", NULL },
    { "ContentHash", "content-hash", NULL },
    { "Description", "description", NULL },
    { "AvailableSocketTypes", "available-socket-types", NULL },
    { "TransferredBytes", "transferred-bytes", NULL },
    { "InitialOffset", "initial-offset", NULL },
    { "Date", "date", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
    { TP_IFACE_CHANNEL,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      channel_props
    },
    { TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      file_props
    },
    { NULL }
  };

  g_type_class_add_private (gabble_file_transfer_channel_class,
      sizeof (GabbleFileTransferChannelPrivate));

  object_class->dispose = gabble_file_transfer_channel_dispose;
  object_class->finalize = gabble_file_transfer_channel_finalize;

  object_class->constructor = gabble_file_transfer_channel_constructor;
  object_class->get_property = gabble_file_transfer_channel_get_property;
  object_class->set_property = gabble_file_transfer_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");
  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "The string obtained by inspecting this channel's handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

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

  param_spec = g_param_spec_object ("connection",
      "GabbleConnection object",
      "Gabble Connection that owns the"
      "connection for this IM channel",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "TpFileTransferState state",
      "State of the file transfer in this channel",
      0,
      NUM_TP_FILE_TRANSFER_STATES,
      TP_FILE_TRANSFER_STATE_NONE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_string (
      "content-type",
      "gchar *content-type",
      "ContentType of the file",
      "application/octet-stream",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTENT_TYPE,
      param_spec);

  param_spec = g_param_spec_string (
      "filename",
      "gchar *filename",
      "Name of the file",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FILENAME, param_spec);

  param_spec = g_param_spec_uint64 (
      "size",
      "guint size",
      "Size of the file in bytes",
      0,
      G_MAXUINT64,
      GABBLE_UNDEFINED_FILE_SIZE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SIZE, param_spec);

  param_spec = g_param_spec_uint (
      "content-hash-type",
      "TpFileHashType content-hash-type",
      "Hash type",
      0,
      NUM_TP_FILE_HASH_TYPES,
      TP_FILE_HASH_TYPE_NONE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTENT_HASH_TYPE,
      param_spec);

  param_spec = g_param_spec_string (
      "content-hash",
      "gchar *content-hash",
      "Hash of the file contents",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTENT_HASH,
      param_spec);

  param_spec = g_param_spec_string (
      "description",
      "gchar *description",
      "Description of the file",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DESCRIPTION, param_spec);

  param_spec = g_param_spec_boxed (
      "available-socket-types",
      "GabbleSupportedSocketMap available-socket-types",
      "Available socket types",
      TP_HASH_TYPE_SUPPORTED_SOCKET_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AVAILABLE_SOCKET_TYPES,
      param_spec);

  param_spec = g_param_spec_uint64 (
      "transferred-bytes",
      "guint64 transferred-bytes",
      "Bytes transferred",
      0,
      G_MAXUINT64,
      0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TRANSFERRED_BYTES,
      param_spec);

  param_spec = g_param_spec_uint64 (
      "initial-offset",
      "guint64 initial_offset",
      "Offset set at the beginning of the transfer",
      0,
      G_MAXUINT64,
      0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_OFFSET,
      param_spec);

  param_spec = g_param_spec_uint64 (
      "date",
      "Epoch time",
      "the last modification time of the file being transferred",
      0,
      G_MAXUINT64,
      0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DATE,
      param_spec);

  param_spec = g_param_spec_boolean (
      "resume-supported",
      "resume is supported",
      "TRUE if resume is supported on this file transfer channel",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RESUME_SUPPORTED,
      param_spec);

  gabble_file_transfer_channel_class->dbus_props_class.interfaces =
      prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleFileTransferChannelClass, dbus_props_class));
}

void
gabble_file_transfer_channel_dispose (GObject *object)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (object);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  self->priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, self->priv->handle);
  tp_handle_unref (handle_repo, self->priv->initiator);

  gabble_file_transfer_channel_do_close (self);

  if (self->priv->progress_timer != 0)
    {
      g_source_remove (self->priv->progress_timer);
      self->priv->progress_timer = 0;
    }

  close_session_and_transport (self);


  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_file_transfer_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_file_transfer_channel_parent_class)->dispose (object);
}

static void
erase_socket (GabbleFileTransferChannel *self)
{
  GArray *array;

  if (self->priv->socket_type != TP_SOCKET_ADDRESS_TYPE_UNIX)
    /* only UNIX sockets have to be erased */
    return;

  if (self->priv->socket_address == NULL)
    return;

  array = g_value_get_boxed (self->priv->socket_address);
  if (g_unlink (array->data) != 0)
    {
      DEBUG ("unlink failed: %s", g_strerror (errno));
    }
}

static void
gabble_file_transfer_channel_finalize (GObject *object)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (object);

  /* free any data held directly by the object here */
  erase_socket (self);
  g_free (self->priv->object_path);
  g_free (self->priv->filename);
  if (self->priv->socket_address != NULL)
    tp_g_value_slice_free (self->priv->socket_address);
  g_free (self->priv->content_type);
  g_free (self->priv->content_hash);
  g_free (self->priv->description);
  g_hash_table_destroy (self->priv->available_socket_types);

  G_OBJECT_CLASS (gabble_file_transfer_channel_parent_class)->finalize (object);
}

static void
close_session_and_transport (GabbleFileTransferChannel *self)
{
  if (self->priv->jingle != NULL)
    {
      gabble_jingle_session_terminate (self->priv->jingle,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL, NULL);
      /* the terminate could synchronously unref it and set it to NULL */
      if (self->priv->jingle != NULL)
        {
          g_object_unref (self->priv->jingle);
          self->priv->jingle = NULL;
        }
    }

  if (self->priv->jingle_channels != NULL)
    {
      g_hash_table_destroy (self->priv->jingle_channels);
      self->priv->jingle_channels = NULL;
    }

  if (self->priv->bytestream != NULL)
    {
      gabble_bytestream_iface_close (self->priv->bytestream, NULL);
      g_object_unref (self->priv->bytestream);
      self->priv->bytestream = NULL;
    }

  if (self->priv->listener != NULL)
    {
      g_object_unref (self->priv->listener);
      self->priv->listener = NULL;
    }

  if (self->priv->transport != NULL)
    {
      g_object_unref (self->priv->transport);
      self->priv->transport = NULL;
    }
}

/**
 * gabble_file_transfer_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_file_transfer_channel_close (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (iface);

  if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED &&
      self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
    {
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (iface),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_STOPPED);

      close_session_and_transport (self);
    }

  gabble_file_transfer_channel_do_close (GABBLE_FILE_TRANSFER_CHANNEL (iface));
  tp_svc_channel_return_from_close (context);
}

/**
 * gabble_file_transfer_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_file_transfer_channel_get_channel_type (TpSvcChannel *iface,
                                              DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
}

/**
 * gabble_file_transfer_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_file_transfer_channel_get_handle (TpSvcChannel *iface,
                                        DBusGMethodInvocation *context)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (iface);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
      self->priv->handle);
}

/**
 * gabble_file_transfer_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_file_transfer_channel_get_interfaces (TpSvcChannel *iface,
                                            DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      gabble_file_transfer_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_file_transfer_channel_##x)
  IMPLEMENT (close);
  IMPLEMENT (get_channel_type);
  IMPLEMENT (get_handle);
  IMPLEMENT (get_interfaces);
#undef IMPLEMENT
}

static gboolean setup_local_socket (GabbleFileTransferChannel *self,
    TpSocketAddressType address_type, TpSocketAccessControl access_control,
    const GValue *access_control_param);

static void
gabble_file_transfer_channel_set_state (
    TpSvcChannelTypeFileTransfer *iface,
    TpFileTransferState state,
    TpFileTransferStateChangeReason reason)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (iface);

  if (self->priv->state == state)
    return;

  self->priv->state = state;
  tp_svc_channel_type_file_transfer_emit_file_transfer_state_changed (iface,
      state, reason);
}

static gboolean
check_address_and_access_control (GabbleFileTransferChannel *self,
                                  TpSocketAddressType address_type,
                                  TpSocketAccessControl access_control,
                                  const GValue *access_control_param,
                                  GError **error)
{
  GArray *access_arr;
  guint i;

  /* Do we support this AddressType? */
  access_arr = g_hash_table_lookup (self->priv->available_socket_types,
      GUINT_TO_POINTER (address_type));
  if (access_arr == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "AddressType %u is not implemented", address_type);
      return FALSE;
    }

  /* Do we support this AccessControl? */
  for (i = 0; i < access_arr->len; i++)
    {
      TpSocketAccessControl control;

      control = g_array_index (access_arr, TpSocketAccessControl, i);
      if (control == access_control)
        return TRUE;
    }

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "AccesControl %u is not implemented with AddressType %u",
      access_control, address_type);

  return FALSE;
}

static void
channel_open (GabbleFileTransferChannel *self)
{
  if (self->priv->socket_address != NULL)
    {
      /* ProvideFile has already been called. Channel is Open */
      tp_svc_channel_type_file_transfer_emit_initial_offset_defined (self,
          self->priv->initial_offset);

      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_OPEN,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

      if (self->priv->transport)
        gibber_transport_block_receiving (self->priv->transport, FALSE);
    }
  else
    {
      /* Client has to call ProvideFile to open the channel */
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_ACCEPTED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
    }
}

static void
channel_closed (GabbleFileTransferChannel *self)
{
  if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED)
    {
      TpBaseConnection *base_conn = (TpBaseConnection *)
          self->priv->connection;
      gboolean receiver;

      receiver = (self->priv->initiator != base_conn->self_handle);

      /* Something did wrong */
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          receiver ?
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR :
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_ERROR);
    }
}

static void
bytestream_state_changed_cb (GabbleBytestreamIface *bytestream,
                             GabbleBytestreamState state,
                             gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);

  if (state == GABBLE_BYTESTREAM_STATE_OPEN)
    {
      channel_open (self);
    }
  else if (state == GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      channel_closed (self);
    }
}

static void bytestream_write_blocked_cb (GabbleBytestreamIface *bytestream,
                                         gboolean blocked,
                                         GabbleFileTransferChannel *self);
gboolean
gabble_file_transfer_channel_set_bytestream (GabbleFileTransferChannel *self,
                                             GabbleBytestreamIface *bytestream)

{
  if (bytestream == NULL)
    return FALSE;

  if (self->priv->bytestream || self->priv->jingle)
    return FALSE;

  self->priv->bytestream = g_object_ref (bytestream);

  gabble_signal_connect_weak (bytestream, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), G_OBJECT (self));
  gabble_signal_connect_weak (self->priv->bytestream, "write-blocked",
      G_CALLBACK (bytestream_write_blocked_cb), G_OBJECT (self));

  return TRUE;
}

static JingleChannel *
get_jingle_channel (GabbleFileTransferChannel *self, NiceAgent *agent)
{
  GHashTableIter iter;
  gpointer key, value;
  JingleChannel *ret = NULL;

  g_hash_table_iter_init (&iter, self->priv->jingle_channels);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      JingleChannel *channel = (JingleChannel *) value;
      if (channel->agent == agent)
        {
          ret = channel;
          break;
        }
    }

  return ret;
}


static void
jingle_session_state_changed_cb (GabbleJingleSession *session,
                                 GParamSpec *arg1,
                                 GabbleFileTransferChannel *self)
{
  JingleSessionState state;

  DEBUG ("called");

  g_object_get (session,
      "state", &state,
      NULL);

  switch (state)
    {
      case JS_STATE_INVALID:
      case JS_STATE_PENDING_CREATED:
        break;
      case JS_STATE_PENDING_INITIATE_SENT:
      case JS_STATE_PENDING_INITIATED:
        gabble_file_transfer_channel_set_state (
            TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
            TP_FILE_TRANSFER_STATE_PENDING,
            TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
        break;
      case JS_STATE_PENDING_ACCEPT_SENT:
        gabble_file_transfer_channel_set_state (
            TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
            TP_FILE_TRANSFER_STATE_ACCEPTED,
            TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
        break;
      case JS_STATE_ACTIVE:
        channel_open (self);
        break;
      case JS_STATE_ENDED:
      default:
        channel_closed (self);
        break;
    }
}

static void
jingle_session_terminated_cb (GabbleJingleSession *session,
                       gboolean local_terminator,
                       TpChannelGroupChangeReason reason,
                       const gchar *text,
                       gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);

  g_assert (session == self->priv->jingle);

  if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED &&
      self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
    {
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          local_terminator ?
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_STOPPED:
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_STOPPED);

      close_session_and_transport (self);
    }

  gabble_file_transfer_channel_do_close (self);

}

static void
content_new_remote_candidates_cb (GabbleJingleContent *content,
    GList *clist, gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  GList *li;

  DEBUG ("Got new remote candidates");

  for (li = clist; li; li = li->next)
    {
      JingleCandidate *candidate = li->data;
      NiceCandidate *cand = NULL;
      JingleChannel *channel = NULL;
      GSList *candidates = NULL;

      if (candidate->type != JINGLE_TRANSPORT_PROTOCOL_UDP)
        continue;

      channel = g_hash_table_lookup (self->priv->jingle_channels,
          GINT_TO_POINTER (candidate->component));
      if (channel == NULL)
        continue;

      cand = nice_candidate_new (
          candidate->type == JINGLE_CANDIDATE_TYPE_LOCAL?
          NICE_CANDIDATE_TYPE_HOST:
          candidate->type == JINGLE_CANDIDATE_TYPE_STUN?
          NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
          NICE_CANDIDATE_TYPE_RELAYED);


      cand->transport = JINGLE_TRANSPORT_PROTOCOL_UDP;
      nice_address_init (&cand->addr);
      nice_address_set_from_string (&cand->addr, candidate->address);
      nice_address_set_port (&cand->addr, candidate->port);
      cand->priority = candidate->preference * 1000;
      cand->stream_id = channel->stream_id;
      cand->component_id = channel->component_id;
      /*
      if (c->id == NULL)
        candidate_id = g_strdup_printf ("R%d", ++priv->remote_candidate_count);
      else
      candidate_id = c->id;*/
      if (candidate->id)
        strncpy (cand->foundation, candidate->id,
            NICE_CANDIDATE_MAX_FOUNDATION - 1);
      cand->username = g_strdup (candidate->username?candidate->username:"");
      cand->password = g_strdup (candidate->password?candidate->password:"");

      candidates = g_slist_append (candidates, cand);
      nice_agent_set_remote_candidates (channel->agent,
          channel->stream_id, channel->component_id, candidates);
      g_slist_foreach (candidates, (GFunc)nice_candidate_free, NULL);
      g_slist_free (candidates);
    }
}

static void
nice_candidate_gathering_done (NiceAgent *agent, guint stream_id,
    gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (channel->content);
  GList *candidates = NULL;
  GList *remote_candidates = NULL;
  GSList *local_candidates;
  GSList *li;

  DEBUG ("libnice candidate gathering done!!!!");

  /* Send remote candidates to libnice and listen to new signal */
  remote_candidates = gabble_jingle_content_get_remote_candidates (content);
  content_new_remote_candidates_cb (content, remote_candidates, self);

  gabble_signal_connect_weak (content, "new-candidates",
      (GCallback) content_new_remote_candidates_cb, G_OBJECT (self));

  /* Send gathered local candidates to the content */
  local_candidates = nice_agent_get_local_candidates (agent, stream_id,
      channel->component_id);

  for (li = local_candidates; li; li = li->next)
    {
      NiceCandidate *cand = li->data;
      JingleCandidate *candidate;
      gchar ip[NICE_ADDRESS_STRING_LEN];

      nice_address_to_string (&cand->addr, ip);

      candidate = jingle_candidate_new (
          /* protocol */
          cand->transport == NICE_CANDIDATE_TRANSPORT_UDP?
          JINGLE_TRANSPORT_PROTOCOL_UDP:
          JINGLE_TRANSPORT_PROTOCOL_TCP,
          /* candidate type */
          cand->type == NICE_CANDIDATE_TYPE_HOST?
          JINGLE_CANDIDATE_TYPE_LOCAL:
          cand->type == NICE_CANDIDATE_TYPE_RELAYED?
          JINGLE_CANDIDATE_TYPE_RELAY:
          JINGLE_CANDIDATE_TYPE_STUN,
          /* id */
          cand->foundation,
          /* component */
          channel->channel_id,
          /* address */
          ip,
          /* port */
          nice_address_get_port (&cand->addr),
          /* generation */
          0,
          /* preference */
          cand->priority / 1000,
          /* username */
          cand->username?cand->username:"",
          /* password */
          cand->password?cand->password:"",
          /* network */
          0);

      candidates = g_list_prepend (candidates, candidate);
    }

  gabble_jingle_content_add_candidates (content, candidates);
}

static void
nice_component_state_changed (NiceAgent *agent,  guint stream_id,
    guint component_id, guint state, gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (channel->content);
  JingleTransportState ts = JINGLE_TRANSPORT_STATE_DISCONNECTED;

  DEBUG ("libnice component state changed %d!!!!", state);

  switch (state)
    {
      case NICE_COMPONENT_STATE_DISCONNECTED:
      case NICE_COMPONENT_STATE_GATHERING:
        ts = JINGLE_TRANSPORT_STATE_DISCONNECTED;
        break;
      case NICE_COMPONENT_STATE_CONNECTING:
        ts = JINGLE_TRANSPORT_STATE_CONNECTING;
        break;
      case NICE_COMPONENT_STATE_CONNECTED:
      case NICE_COMPONENT_STATE_READY:
        ts = JINGLE_TRANSPORT_STATE_CONNECTED;
        break;
      case NICE_COMPONENT_STATE_FAILED:

        gabble_file_transfer_channel_set_state (
            TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
            TP_FILE_TRANSFER_STATE_CANCELLED,
            TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR);

        close_session_and_transport (self);
        /* return because we don't want to use the content after it
           has been destroyed.. */
        return;
    }
  gabble_jingle_content_set_transport_state (content, ts);
}

static void get_next_manifest_entry (GabbleFileTransferChannel *self,
    JingleChannel *channel)
{
  GabbleJingleShareManifest *manifest = NULL;
  GabbleJingleShareManifestEntry *entry = NULL;

  manifest = gabble_jingle_share_get_manifest (channel->content);
  channel->manifest_entry++;
  entry = g_list_nth_data (manifest->entries, channel->manifest_entry);

  /* FIXME: We only support one file per manifest for now! */
  if (channel->manifest_entry > 0)
    entry = NULL;

  if (entry == NULL)
    {
      GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (channel->content);
      DEBUG ("Received all the file. Transfer is complete");
      gabble_jingle_content_send_complete (content);

      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_COMPLETED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

      if (gibber_transport_buffer_is_empty (self->priv->transport))
        gibber_transport_disconnect (self->priv->transport);

    }
  else
    {
      gchar *buffer = NULL;
      gchar *source_url = manifest->source_url;
      guint url_len = strlen (source_url);
      gchar *separator = "";

      if (source_url[url_len -1] != '/')
        separator = "/";

      /* The session initiator will always be the full JID of the peer */
      buffer = g_strdup_printf ("GET %s%s%s HTTP/1.1\r\n"
          "Connection: Keep-Alive\r\n"
          "Content-Length: 0\r\n"
          "Host: %s:0\r\n"
          "User-Agent: %s\r\n\r\n",
          source_url, separator, entry->name,
          gabble_jingle_session_get_initiator (self->priv->jingle),
          PACKAGE_STRING);

      /* FIXME: check for success */
      nice_agent_send (channel->agent, channel->stream_id,
          channel->component_id, strlen (buffer), buffer);
      g_free (buffer);

      channel->http_status = HTTP_CLIENT_RECEIVE;
      /* Do not receive anything until the local socket is connected */
      if (self->priv->transport == NULL)
        {
          nice_agent_attach_recv (channel->agent, channel->stream_id,
              channel->component_id, NULL, NULL, NULL);
          channel->agent_attached = FALSE;
        }
    }
}

static void
nice_component_writable (NiceAgent *agent, guint stream_id, guint component_id,
    gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);

  if (channel->http_status == HTTP_CLIENT_IDLE)
    {
      get_next_manifest_entry (self, channel);
    }
  else if (channel->http_status == HTTP_SERVER_SEND)
    {
      gibber_transport_block_receiving (self->priv->transport, FALSE);
      if (channel->write_buffer)
        {
          gint ret = nice_agent_send (agent, stream_id, component_id,
              channel->write_len, channel->write_buffer);
          if (ret < 0 || (guint) ret < channel->write_len)
            {
              gchar *to_free = channel->write_buffer;
              if (ret < 0)
                ret = 0;

              channel->write_buffer = g_memdup (channel->write_buffer + ret,
                  channel->write_len - ret);
              channel->write_len = channel->write_len - ret;
              g_free (to_free);

              gibber_transport_block_receiving (self->priv->transport, TRUE);
            }
          else
            {
              g_free (channel->write_buffer);
              channel->write_buffer = NULL;
              channel->write_len = 0;
            }
          transferred_chunk (self, (guint64) channel->write_len);
        }
    }

}

typedef struct
{
  GabbleFileTransferChannel *self;
  JingleChannel *channel;
} GoogleRelaySessionData;

static void
set_relay_info (gpointer item, gpointer user_data)
{
  GoogleRelaySessionData *data = user_data;
  GHashTable *relay = item;
  const gchar *server_ip = NULL;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *type_str = NULL;
  guint server_port;
  NiceRelayType type;
  GValue *value;

  value = g_hash_table_lookup (relay, "ip");
  if (value)
    server_ip = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "port");
  if (value)
    server_port = g_value_get_uint (value);
  else
    return;

  value = g_hash_table_lookup (relay, "username");
  if (value)
    username = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "password");
  if (value)
    password = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "type");
  if (value)
    type_str = g_value_get_string (value);
  else
    return;

  if (!strcmp (type_str, "udp"))
    type = NICE_RELAY_TYPE_TURN_UDP;
  else if (!strcmp (type_str, "tcp"))
    type = NICE_RELAY_TYPE_TURN_TCP;
  else if (!strcmp (type_str, "tls"))
    type = NICE_RELAY_TYPE_TURN_TLS;
  else
    return;

  nice_agent_set_relay_info (data->channel->agent,
      data->channel->stream_id, data->channel->component_id,
      server_ip, server_port,
      username, password, type);

}

static void
google_relay_session_cb (GPtrArray *relays, gpointer user_data)
{
  GoogleRelaySessionData *data = user_data;

  if (data->self == NULL)
    {
      DEBUG ("Received relay session callback but self got destroyed");
      g_slice_free (GoogleRelaySessionData, data);
      return;
    }

  if (relays)
      g_ptr_array_foreach (relays, set_relay_info, user_data);

  nice_agent_gather_candidates (data->channel->agent, data->channel->stream_id);

  g_object_remove_weak_pointer (G_OBJECT (data->self), (gpointer *)&data->self);
  g_slice_free (GoogleRelaySessionData, data);
}


static void
content_new_channel_cb (GabbleJingleContent *content, const gchar *name,
    gint channel_id, gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->connection;
  gboolean requested = (self->priv->initiator == base_conn->self_handle);
  JingleChannel *channel = g_slice_new0 (JingleChannel);
  NiceAgent *agent = nice_agent_new_reliable (g_main_context_default (),
      NICE_COMPATIBILITY_GOOGLE);
  guint stream_id = nice_agent_add_stream (agent, 1);
  gchar *stun_server;
  guint stun_port;
  GoogleRelaySessionData *relay_data = NULL;

  DEBUG ("New channel %s was created and linked to id %d", name, channel_id);

  channel->agent = agent;
  channel->stream_id = stream_id;
  channel->component_id = NICE_COMPONENT_TYPE_RTP;
  channel->content = GABBLE_JINGLE_SHARE (content);
  channel->channel_id = channel_id;

  /* We increment the manifest_entry before fetching the entry, so we
     initialize it at -1, in order to start the index at 0 */
  channel->manifest_entry = -1;

  if (requested)
      channel->http_status = HTTP_SERVER_IDLE;
  else
      channel->http_status = HTTP_CLIENT_IDLE;

  gabble_signal_connect_weak (agent, "candidate-gathering-done",
      G_CALLBACK (nice_candidate_gathering_done), G_OBJECT (self));

  gabble_signal_connect_weak (agent, "component-state-changed",
      G_CALLBACK (nice_component_state_changed), G_OBJECT (self));

  gabble_signal_connect_weak (agent, "reliable-transport-writable",
      G_CALLBACK (nice_component_writable), G_OBJECT (self));


  /* Add the agent to the hash table before gathering candidates in case the
     gathering finishes synchronously, and the callback tries to add local
     candidates to the content, it needs to find the channel id.. */
  g_hash_table_insert (self->priv->jingle_channels,
      GINT_TO_POINTER (channel_id), channel);

  channel->agent_attached = TRUE;
  nice_agent_attach_recv (agent, stream_id, channel->component_id,
      g_main_context_default (), nice_data_received_cb, self);

  if (gabble_jingle_factory_get_stun_server (
          self->priv->connection->jingle_factory, &stun_server, &stun_port))
    {
      g_object_set (agent,
          "stun-server", stun_server,
          "stun-server-port", stun_port,
          NULL);
      g_free (stun_server);
    }

  relay_data = g_slice_new0 (GoogleRelaySessionData);
  relay_data->self = self;
  relay_data->channel = channel;
  g_object_add_weak_pointer (G_OBJECT (relay_data->self),
      (gpointer *)&relay_data->self);
  gabble_jingle_factory_create_google_relay_session (
      self->priv->connection->jingle_factory, 1,
      google_relay_session_cb, relay_data);
}

static void
content_completed (GabbleJingleContent *content, gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  gabble_file_transfer_channel_set_state (
      TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
      TP_FILE_TRANSFER_STATE_COMPLETED,
      TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
}

static void
free_jingle_channel (gpointer data)
{
  JingleChannel *channel = (JingleChannel *) data;

  DEBUG ("Freeing jingle channel");

  if (channel->write_buffer)
    {
      g_free (channel->write_buffer);
      channel->write_buffer = NULL;
    }
  if (channel->read_buffer)
    {
      g_free (channel->read_buffer);
      channel->read_buffer = NULL;
    }
  g_object_unref (channel->agent);
  g_slice_free (JingleChannel, channel);
}


gboolean
gabble_file_transfer_channel_set_jingle_session (
    GabbleFileTransferChannel *self, GabbleJingleSession *session)
{
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->connection;
  gboolean requested = (self->priv->initiator == base_conn->self_handle);
  GList *cs;

  if (session == NULL)
    return FALSE;

  if (self->priv->bytestream || self->priv->jingle)
    return FALSE;

  if (self->priv->jingle_channels != NULL)
    {
      g_hash_table_destroy (self->priv->jingle_channels);
      self->priv->jingle_channels = NULL;
    }

  self->priv->jingle_channels = g_hash_table_new_full (NULL, NULL,
      NULL, free_jingle_channel);

  /* FIXME: we should start creating a nice agent already and have it start
     the candidate gathering.. but we don't know which channel name to
     assign it to... */

  self->priv->jingle = g_object_ref (session);

  gabble_signal_connect_weak (session, "notify::state",
      (GCallback) jingle_session_state_changed_cb, G_OBJECT (self));
  gabble_signal_connect_weak (session, "terminated",
      (GCallback) jingle_session_terminated_cb, G_OBJECT (self));

  cs = gabble_jingle_session_get_contents (session);

  if (cs != NULL)
    {
      GabbleJingleShare *content = GABBLE_JINGLE_SHARE (cs->data);

      if (!requested)
        {
          GabbleJingleShareManifest *manifest = NULL;
          GabbleJingleShareManifestEntry *entry = NULL;

          /* FIXME: We only support one file per manifest for now! */
          manifest = gabble_jingle_share_get_manifest (content);
          entry = g_list_nth_data (manifest->entries, 0);
          if (entry != NULL)
            {
              g_free (self->priv->filename);
              if (entry->folder)
                self->priv->filename = g_strdup_printf ("%s.tar", entry->name);
              else
                self->priv->filename = g_strdup (entry->name);

              self->priv->size = entry->size;
            }
        }

      gabble_signal_connect_weak (content, "new-channel",
          (GCallback) content_new_channel_cb, G_OBJECT (self));
      gabble_signal_connect_weak (content, "completed",
          (GCallback) content_completed, G_OBJECT (self));
    }

  return TRUE;
}

static void
bytestream_negotiate_cb (GabbleBytestreamIface *bytestream,
                         const gchar *stream_id,
                         LmMessage *msg,
                         GObject *object,
                         gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  LmMessageNode *file;

  if (bytestream == NULL)
    {
      DEBUG ("receiver refused file offer");
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_STOPPED);
      return;
    }

  file = lm_message_node_find_child (msg->node, "file");
  if (file != NULL)
    {
      LmMessageNode *range;

      range = lm_message_node_get_child_any_ns (file, "range");
      if (range != NULL)
        {
          const gchar *offset_str;

          offset_str = lm_message_node_get_attribute (range, "offset");
          if (offset_str != NULL)
            {
              self->priv->initial_offset = g_ascii_strtoull (offset_str, NULL,
                  0);
            }
        }
    }

  DEBUG ("receiver accepted file offer (offset: %" G_GUINT64_FORMAT ")",
      self->priv->initial_offset);

  gabble_file_transfer_channel_set_bytestream (self, bytestream);

  self->priv->remote_accepted = TRUE;
}

static gboolean
offer_bytestream (GabbleFileTransferChannel *self, const gchar *jid,
                  const gchar *resource, GError **error)
{
  gboolean result;
  LmMessage *msg;
  LmMessageNode *si_node, *file_node;
  gchar *stream_id, *size_str, *full_jid;

  if (resource)
    full_jid = g_strdup_printf ("%s/%s", jid, resource);
  else
    full_jid = g_strdup (jid);

  /* Outgoing FT , we'll need SOCK5 proxies */
  gabble_bytestream_factory_query_socks5_proxies (
      self->priv->connection->bytestream_factory);


  stream_id = gabble_bytestream_factory_generate_stream_id ();

  msg = gabble_bytestream_factory_make_stream_init_iq (full_jid,
      stream_id, NS_FILE_TRANSFER);

  si_node = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  g_assert (si_node != NULL);

  size_str = g_strdup_printf ("%" G_GUINT64_FORMAT, self->priv->size);

  file_node = lm_message_node_add_child (si_node, "file", NULL);
  lm_message_node_set_attributes (file_node,
      "xmlns", NS_FILE_TRANSFER,
      "name", self->priv->filename,
      "size", size_str,
      "mime-type", self->priv->content_type,
      NULL);

  if (self->priv->content_hash != NULL)
    lm_message_node_set_attribute (file_node, "hash", self->priv->content_hash);

  if (self->priv->date != 0)
    {
      time_t t;
      struct tm *tm;
      char date_str[21];

      t = (time_t) self->priv->date;
      tm = gmtime (&t);

      strftime (date_str, sizeof (date_str), "%FT%H:%M:%SZ", tm);

      lm_message_node_set_attribute (file_node, "date", date_str);
    }

  lm_message_node_add_child (file_node, "desc", self->priv->description);

  /* we support resume */
  lm_message_node_add_child (file_node, "range", NULL);

  result = gabble_bytestream_factory_negotiate_stream (
      self->priv->connection->bytestream_factory, msg, stream_id,
      bytestream_negotiate_cb, self, G_OBJECT (self), error);

  lm_message_unref (msg);
  g_free (stream_id);
  g_free (size_str);
  g_free (full_jid);

  return result;
}


static gboolean
offer_jingle (GabbleFileTransferChannel *self, const gchar *jid,
              const gchar *resource, GError **error)
{

  GabbleJingleSession *jingle;
  GabbleJingleContent *content;

  jingle = gabble_jingle_factory_create_session (
      self->priv->connection->jingle_factory,
      self->priv->handle, resource, FALSE);

  if (!jingle)
    return FALSE;

  g_object_set (jingle, "dialect", JINGLE_DIALECT_GTALK4, NULL);

  content = gabble_jingle_session_add_content (jingle,
      JINGLE_MEDIA_TYPE_FILE, "share", NS_GOOGLE_SESSION_SHARE,
      NS_GOOGLE_TRANSPORT_P2P);

  if (content)
    {
      g_object_set (content,
          "filename", self->priv->filename,
          "filesize", self->priv->size,
          NULL);
    }
  else
    {
      g_object_unref (jingle);
      return FALSE;
    }

  gabble_file_transfer_channel_set_jingle_session (self, jingle);

  gabble_jingle_session_accept (self->priv->jingle);

  return TRUE;
}

gboolean
gabble_file_transfer_channel_offer_file (GabbleFileTransferChannel *self,
                                         GError **error)
{
  GabblePresence *presence;
  gboolean result;
  TpHandleRepoIface *contact_repo, *room_repo;
  const gchar *jid;
  gboolean si = FALSE;
  const gchar *resource = NULL;

  g_assert (!CHECK_STR_EMPTY (self->priv->filename));
  g_assert (self->priv->size != GABBLE_UNDEFINED_FILE_SIZE);
  g_assert (self->priv->bytestream == NULL && self->priv->jingle == NULL);

  presence = gabble_presence_cache_get (self->priv->connection->presence_cache,
      self->priv->handle);
  if (presence == NULL)
    {
      DEBUG ("can't find contact's presence");
      g_set_error (error, TP_ERRORS, TP_ERROR_OFFLINE,
          "can't find contact's presence");

      return FALSE;
    }

  contact_repo = tp_base_connection_get_handles (
     (TpBaseConnection *) self->priv->connection, TP_HANDLE_TYPE_CONTACT);
  room_repo = tp_base_connection_get_handles (
     (TpBaseConnection *) self->priv->connection, TP_HANDLE_TYPE_ROOM);

  jid = tp_handle_inspect (contact_repo, self->priv->handle);

  if (gabble_get_room_handle_from_jid (room_repo, jid) == 0)
    {
      /* Not a MUC jid, need to get a resource */

      /* FIXME: should we check for SI, bytestreams and/or IBB too?
       * http://bugs.freedesktop.org/show_bug.cgi?id=23777 */
      resource = gabble_presence_pick_resource_by_caps (presence,
          gabble_capability_set_predicate_has, NS_FILE_TRANSFER);

      if (resource == NULL)
        {
          resource = gabble_presence_pick_resource_by_caps (presence,
              gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_SHARE);
          if (resource == NULL)
            {
              DEBUG ("contact doesn't have file transfer capabilities");
              if (error != NULL)
                g_set_error (error, TP_ERRORS, TP_ERROR_NOT_CAPABLE,
                    "contact doesn't have file transfer capabilities");

              return FALSE;
            }
          else
            {
              si = FALSE;
            }
        }
      else
      {
        si = TRUE;
      }
    }
  else
    {
      /* MUC jid, we already have the full jid */
    }

  DEBUG ("Offering file transfer to %s%s%s", jid,
      resource? "/":"", resource? resource:"");

  if (si)
    result = offer_bytestream (self, jid, resource, error);
  else
    result = offer_jingle (self, jid, resource, error);

  return result;
}

static void
emit_progress_update (GabbleFileTransferChannel *self)
{
  TpSvcChannelTypeFileTransfer *iface =
      TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self);

  g_get_current_time (&self->priv->last_transferred_bytes_emitted);

  tp_svc_channel_type_file_transfer_emit_transferred_bytes_changed (
    iface, self->priv->transferred_bytes);

  if (self->priv->progress_timer != 0)
    {
      g_source_remove (self->priv->progress_timer);
      self->priv->progress_timer = 0;
    }
}

static gboolean
emit_progress_update_cb (gpointer user_data)
{
  GabbleFileTransferChannel *self =
      GABBLE_FILE_TRANSFER_CHANNEL (user_data);

  emit_progress_update (self);

  return FALSE;
}

static void
transferred_chunk (GabbleFileTransferChannel *self,
                   guint64 count)
{
  GTimeVal timeval;
  gint interval;

  self->priv->transferred_bytes += count;

  if (self->priv->transferred_bytes + self->priv->initial_offset >=
      self->priv->size)
    {
      /* If the transfer has finished send an update right away */
      emit_progress_update (self);
      return;
    }

  if (self->priv->progress_timer != 0)
    {
      /* A progress update signal is already scheduled */
      return;
    }

  /* Only emit the TransferredBytes signal if it has been one second since its
   * last emission.
   */
  g_get_current_time (&timeval);
  interval = timeval.tv_sec -
    self->priv->last_transferred_bytes_emitted.tv_sec;

  if (interval > 1)
    {
      /* At least more then a second apart, emit right away */
      emit_progress_update (self);
      return;
    }

  /* Convert interval to milliseconds and calculate it more precisely */
  interval *= 1000;

  interval += (timeval.tv_usec -
    self->priv->last_transferred_bytes_emitted.tv_usec)/1000;

  /* Protect against clock skew, if the interval is negative the worst thing
   * that can happen is that we wait an extra second before emitting the signal
   */
  interval = ABS (interval);

  if (interval > 1000)
    emit_progress_update (self);
  else
    self->priv->progress_timer = g_timeout_add (1000 - interval,
       emit_progress_update_cb, self);
}

static void
send_transport_data (GabbleFileTransferChannel *self,
    JingleChannel *channel, const guint8 *data, guint data_len)
{
  GError *error = NULL;
  if (!gibber_transport_send (self->priv->transport, data, data_len, &error))
    {
      DEBUG ("sending to transport failed: %s", error->message);
      g_error_free (error);

      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR);
    }
  else
    {
      transferred_chunk (self, (guint64) data_len);

      if (!gibber_transport_buffer_is_empty (self->priv->transport))
        {
          /* We don't want to send more data while the buffer isn't empty */
          /* FIXME: what if we miss TCP syn/ack messages and cause longer
             timeouts because rto becomes big ? */
          nice_agent_attach_recv (channel->agent, channel->stream_id,
              channel->component_id, NULL, NULL, NULL);
          channel->agent_attached = FALSE;
        }
    }
}

/* Return the pointer at the end of the line or NULL if not \n found */
static gchar *
http_read_line (gchar *buffer, guint len)
{
  gchar *p = memchr (buffer, '\n', len);

  if (p != NULL)
    {
      *p = 0;
      if (p > buffer && *(p-1) == '\r')
        *(p-1) = 0;
      p++;
    }

  return p;
}

static guint
http_data_received (GabbleFileTransferChannel *self, JingleChannel *channel,
    gchar *buffer, guint len)
{

  switch (channel->http_status)
    {
      case HTTP_SERVER_IDLE:
        {
          gchar *headers = http_read_line (buffer, len);
          if (headers == NULL)
            return 0;

          channel->http_status = HTTP_SERVER_HEADERS;

          return headers - buffer;
        }
        break;
      case HTTP_SERVER_HEADERS:
        {
          gchar *line = buffer;
          gchar *next_line = http_read_line (buffer, len);
          if (next_line == NULL)
            return 0;

          DEBUG ("Found server headers line (%d) : %s", strlen (line), line);
          if (*line == 0)
            {
              gchar *response = NULL;
              /* FIXME: how about content-length and an actual body ? */
              channel->http_status = HTTP_SERVER_SEND;
              DEBUG ("Found empty line, now sending our response");

              /* FIXME: actually check for the requested file's uri */
              /* FIXME: check for success of nice_agent_send */
              response = g_strdup_printf ("HTTP/1.1 200\r\n"
                  "Connection: Keep-Alive\r\n"
                  "Content-Length: %llu\r\n"
                  "Content-Type: application/octet-stream\r\n\r\n",
                  self->priv->size - self->priv->initial_offset);
              nice_agent_send (channel->agent, channel->stream_id,
                  channel->component_id, strlen (response), response);
              g_free (response);
              gibber_transport_block_receiving (self->priv->transport,
                  FALSE);
            }

          return next_line - buffer;
        }
        break;
      case HTTP_SERVER_SEND:
        DEBUG ("received data when we're supposed to be sending data.. "
            "not supposed to happen");
        break;
      case HTTP_CLIENT_IDLE:
        DEBUG ("received data when we're supposed to be sending the GET.. "
            "not supposed to happen");
        break;
      case HTTP_CLIENT_RECEIVE:
        {
          gchar *headers = http_read_line (buffer, len);
          if (headers == NULL)
            return 0;

          channel->http_status = HTTP_CLIENT_HEADERS;

          return headers - buffer;
        }
      case HTTP_CLIENT_HEADERS:
        {
          gchar *line = buffer;
          gchar *next_line = http_read_line (buffer, len);
          if (next_line == NULL)
            return 0;

          DEBUG ("Found client headers line (%d) : %s", strlen (line), line);
          if (*line == 0)
            {
              DEBUG ("Found empty line, now receiving file data");
              if (channel->is_chunked)
                {
                  channel->http_status = HTTP_CLIENT_CHUNK_SIZE;
                }
              else
                {
                  channel->http_status = HTTP_CLIENT_BODY;
                  if (channel->content_length == 0)
                    get_next_manifest_entry (self, channel);
                }
            }
          else if (!g_ascii_strncasecmp (line, "Content-Length: ", 16))
            {
              channel->is_chunked = FALSE;
              /* Check strtoull read all the length */
              channel->content_length = g_ascii_strtoull (line + 16,
                  NULL, 10);
              DEBUG ("Found data length : %llu", channel->content_length);
            }
          else if (!g_ascii_strncasecmp (line,
                  "Transfer-Encoding: chunked", 26))
            {
              channel->is_chunked = TRUE;
              channel->content_length = 0;
              DEBUG ("Found file is chunked");
            }

          return next_line - buffer;
        }
        break;
      case HTTP_CLIENT_CHUNK_SIZE:
        {
          gchar *line = buffer;
          gchar *next_line = http_read_line (buffer, len);
          if (next_line == NULL)
            return 0;

          /* FIXME : check validity of strtoul */
          channel->content_length = strtoul (line, NULL, 16);
          if (channel->content_length > 0)
              channel->http_status = HTTP_CLIENT_BODY;
          else
              channel->http_status = HTTP_CLIENT_CHUNK_FINAL;

          /* Reset the size of the file as we receive more data than expected */
          if (self->priv->transferred_bytes + self->priv->initial_offset +
              channel->content_length >= self->priv->size)
            self->priv->size = self->priv->transferred_bytes +          \
                self->priv->initial_offset + channel->content_length;

          return next_line - buffer;
        }
        break;
      case HTTP_CLIENT_BODY:
        {
          guint consumed = 0;

          if (len >= channel->content_length)
            {
              consumed = channel->content_length;
              send_transport_data (self, channel, (const guint8 *) buffer,
                  channel->content_length);
              channel->content_length = 0;
              if (channel->is_chunked)
                channel->http_status = HTTP_CLIENT_CHUNK_END;
              else
                get_next_manifest_entry (self, channel);
            }
          else
            {
              consumed = len;
              channel->content_length -= len;
              send_transport_data (self, channel, (const guint8 *) buffer, len);
            }

          return consumed;
        }
        break;
      case HTTP_CLIENT_CHUNK_END:
        {
          gchar *chunk = http_read_line (buffer, len);
          if (chunk == NULL)
            return 0;

          channel->http_status = HTTP_CLIENT_CHUNK_SIZE;

          return chunk - buffer;
        }
        break;
      case HTTP_CLIENT_CHUNK_FINAL:
        {
          gchar *end = http_read_line (buffer, len);
          if (end == NULL)
            return 0;

          channel->http_status = HTTP_CLIENT_IDLE;
          get_next_manifest_entry (self, channel);

          return end - buffer;
        }
        break;
    }

  return 0;
}

static void
nice_data_received_cb (NiceAgent *agent,
                       guint stream_id,
                       guint component_id,
                       guint len,
                       gchar *buffer,
                       gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);
  gchar *free_buffer = NULL;


  if (channel->read_buffer != NULL)
    {
      gchar *tmp = g_malloc (channel->read_len + len);
      memcpy (tmp, channel->read_buffer, channel->read_len);
      memcpy (tmp + channel->read_len, buffer, len);

      free_buffer = buffer = tmp;
      len += channel->read_len;

      g_free (channel->read_buffer);
      channel->read_buffer = NULL;
      channel->read_len = 0;
    }
  while (len > 0)
    {
      guint consumed = http_data_received (self, channel, buffer, len);

      if (consumed == 0)
        {
          channel->read_buffer = g_memdup (buffer, len);
          channel->read_len = len;
          break;
        }
      else
        {
          /* we assume http_data_received never returns consumed > len */
          len -= consumed;
          buffer += consumed;
        }
    }

  if (free_buffer != NULL)
    g_free (free_buffer);

}

static void
data_received_cb (GabbleBytestreamIface *stream,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  GError *error = NULL;

  g_assert (self->priv->transport != NULL);

  if (!gibber_transport_send (self->priv->transport, (const guint8 *) data->str,
        data->len, &error))
    {
      DEBUG ("sending to transport failed: %s", error->message);
      g_error_free (error);

      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR);
      return;
    }

  transferred_chunk (self, (guint64) data->len);

  if (self->priv->transferred_bytes + self->priv->initial_offset >=
      self->priv->size)
    {
      DEBUG ("Received all the file. Transfer is complete");
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_COMPLETED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

      if (gibber_transport_buffer_is_empty (self->priv->transport))
        gibber_transport_disconnect (self->priv->transport);

      return;
    }

  if (!gibber_transport_buffer_is_empty (self->priv->transport))
    {
      /* We don't want to send more data while the buffer isn't empty */
      gabble_bytestream_iface_block_reading (self->priv->bytestream, TRUE);
    }
}

static void
augment_si_reply (LmMessageNode *si,
                  gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  LmMessageNode *file;

  file = lm_message_node_add_child (si, "file", NULL);
  lm_message_node_set_attribute (file, "xmlns", NS_FILE_TRANSFER);

  if (self->priv->initial_offset != 0)
    {
      LmMessageNode *range;
      gchar *offset_str;

      range = lm_message_node_add_child (file, "range", NULL);
      offset_str = g_strdup_printf ("%" G_GUINT64_FORMAT,
          self->priv->initial_offset);
      lm_message_node_set_attribute (range, "offset", offset_str);

      /* Don't set "length" attribute as the default is the length of the file
       * from offset to the end which is what we want when resuming a FT. */

      g_free (offset_str);
    }
}

/**
 * gabble_file_transfer_channel_accept_file
 *
 * Implements D-Bus method AcceptFile
 * on interface org.freedesktop.Telepathy.Channel.Type.FileTransfer
 */
static void
gabble_file_transfer_channel_accept_file (TpSvcChannelTypeFileTransfer *iface,
                                          guint address_type,
                                          guint access_control,
                                          const GValue *access_control_param,
                                          guint64 offset,
                                          DBusGMethodInvocation *context)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (iface);
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->connection;
  GError *error = NULL;

  if (self->priv->initiator == base_conn->self_handle)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Channel is not an incoming transfer");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (self->priv->state != TP_FILE_TRANSFER_STATE_PENDING)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "State is not pending; cannot accept file");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!check_address_and_access_control (self, address_type, access_control,
        access_control_param, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!setup_local_socket (self, address_type, access_control,
        access_control_param))
    {
      DEBUG ("Could not set up local socket");
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Could not set up local socket");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  gabble_file_transfer_channel_set_state (iface,
      TP_FILE_TRANSFER_STATE_ACCEPTED,
      TP_FILE_TRANSFER_STATE_CHANGE_REASON_REQUESTED);

  tp_svc_channel_type_file_transfer_return_from_accept_file (context,
      self->priv->socket_address);

  if (self->priv->resume_supported)
    {
      self->priv->initial_offset = offset;
    }
  else
    {
      DEBUG ("Resume is not supported on this file transfer");
      self->priv->initial_offset = 0;
    }

  if (self->priv->bytestream)
    {
      gabble_signal_connect_weak (self->priv->bytestream, "data-received",
          G_CALLBACK (data_received_cb), G_OBJECT (self));


      /* Block the bytestream while the user is not connected to the socket */
      gabble_bytestream_iface_block_reading (self->priv->bytestream, TRUE);

      /* channel state will change to open once the bytestream is open */
      gabble_bytestream_iface_accept (self->priv->bytestream, augment_si_reply,
          self);
    }
  else if (self->priv->jingle)
    {
      GList *cs = gabble_jingle_session_get_contents (self->priv->jingle);

      if (cs != NULL)
        {
          GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (cs->data);
          guint initial_id = 0;
          gint channel_id;

          /* The new-channel signal will take care of the rest.. */
          do
            {
              gchar *channel_name = NULL;

              channel_name = g_strdup_printf ("gabble-%d", ++initial_id);
              channel_id = gabble_jingle_content_create_channel (content,
                  channel_name);
              g_free (channel_name);
            } while (channel_id <= 0 && initial_id < 10);

          /* FIXME: not assert but actually cancel the FT? */
          g_assert (channel_id > 0);
        }
      gabble_jingle_session_accept (self->priv->jingle);
    }
  else
    {
      g_assert_not_reached ();
    }
}

/**
 * gabble_file_transfer_channel_provide_file
 *
 * Implements D-Bus method ProvideFile
 * on interface org.freedesktop.Telepathy.Channel.Type.FileTransfer
 */
static void
gabble_file_transfer_channel_provide_file (
    TpSvcChannelTypeFileTransfer *iface,
    guint address_type,
    guint access_control,
    const GValue *access_control_param,
    DBusGMethodInvocation *context)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (iface);
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->connection;
  GError *error = NULL;

  if (self->priv->initiator != base_conn->self_handle)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Channel is not an outgoing transfer");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (self->priv->state != TP_FILE_TRANSFER_STATE_PENDING &&
      self->priv->state != TP_FILE_TRANSFER_STATE_ACCEPTED)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "State is not pending or accepted; cannot provide file");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (self->priv->socket_address != NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "ProvideFile has already been called for this channel");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!check_address_and_access_control (self, address_type, access_control,
        access_control_param, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!setup_local_socket (self, address_type, access_control,
        access_control_param))
    {
      DEBUG ("Could not set up local socket");
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Could not set up local socket");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (self->priv->remote_accepted)
    {
      /* Remote already accepted the file. Channel is Open.
       * If not channel stay Pending. */
      tp_svc_channel_type_file_transfer_emit_initial_offset_defined (self,
          self->priv->initial_offset);

      gabble_file_transfer_channel_set_state (iface,
          TP_FILE_TRANSFER_STATE_OPEN,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_REQUESTED);
    }

  tp_svc_channel_type_file_transfer_return_from_provide_file (context,
      self->priv->socket_address);
}

static void
file_transfer_iface_init (gpointer g_iface,
                          gpointer iface_data)
{
  TpSvcChannelTypeFileTransferClass *klass =
      (TpSvcChannelTypeFileTransferClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_file_transfer_implement_##x (\
    klass, gabble_file_transfer_channel_##x)
  IMPLEMENT (accept_file);
  IMPLEMENT (provide_file);
#undef IMPLEMENT
}

#ifdef GIBBER_TYPE_UNIX_TRANSPORT
static gchar *
get_local_unix_socket_path (GabbleFileTransferChannel *self)
{
  const gchar *tmp_dir;
  gchar *path = NULL;
  gchar *name;
  struct stat buf;

  tmp_dir = gabble_ft_manager_get_tmp_dir (self->priv->connection->ft_manager);
  if (tmp_dir == NULL)
    return NULL;

  name = g_strdup_printf ("ft-channel-%p", self);
  path = g_build_filename (tmp_dir, name, NULL);
  g_free (name);

  if (g_stat (path, &buf) == 0)
    {
      /* The file is not supposed to exist */
      DEBUG ("file %s already exists", path);
      g_assert_not_reached ();
    }

  return path;
}
#endif

/*
 * Data is available from the channel so we can send it.
 */
static void
transport_handler (GibberTransport *transport,
                   GibberBuffer *data,
                   gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);

  if (self->priv->bytestream)
    {
      if (!gabble_bytestream_iface_send (self->priv->bytestream, data->length,
              (const gchar *) data->data))
        {
          DEBUG ("Sending failed. Closing the bytestream");
          close_session_and_transport (self);
          return;
        }
    }
  else if (self->priv->jingle)
    {
      JingleChannel *channel = g_hash_table_lookup (self->priv->jingle_channels,
          GINT_TO_POINTER (1));
      gint ret = nice_agent_send (channel->agent, channel->stream_id,
          channel->component_id, data->length, (const gchar *) data->data);

      if (ret < 0 || (guint) ret < data->length)
        {
          if (ret < 0)
            ret = 0;

          channel->write_buffer = g_memdup (data->data + ret,
              data->length - ret);
          channel->write_len = data->length - ret;
          gibber_transport_block_receiving (self->priv->transport, TRUE);
        }
    }
  transferred_chunk (self, (guint64) data->length);

  /* nothing to do here for jingle.. wait until the other side requests
     another file (hypothetically) or sends the <complete> info before setting
     out state to COMPLETED */
  if (self->priv->bytestream)
    {
      if (self->priv->transferred_bytes + self->priv->initial_offset >=
          self->priv->size)
        {
          DEBUG ("All the file has been sent. Closing the bytestream");

          gabble_file_transfer_channel_set_state (
              TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
              TP_FILE_TRANSFER_STATE_COMPLETED,
              TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

          gabble_bytestream_iface_close (self->priv->bytestream, NULL);
        }
    }
}

static void
bytestream_write_blocked_cb (GabbleBytestreamIface *bytestream,
                             gboolean blocked,
                             GabbleFileTransferChannel *self)
{
  if (self->priv->transport)
    gibber_transport_block_receiving (self->priv->transport, blocked);
}

static void
file_transfer_send (GabbleFileTransferChannel *self)
{
  if (self->priv->bytestream)
    {
      gibber_transport_set_handler (self->priv->transport, transport_handler,
          self);
      /* We shouldn't receive data if the bytestream isn't open otherwise it
         will error out */
      if (self->priv->state == TP_FILE_TRANSFER_STATE_OPEN)
        gibber_transport_block_receiving (self->priv->transport, FALSE);
      else
        gibber_transport_block_receiving (self->priv->transport, TRUE);
    }
  else
    {
      JingleChannel *channel = g_hash_table_lookup (self->priv->jingle_channels,
          GINT_TO_POINTER (1));
      gibber_transport_set_handler (self->priv->transport, transport_handler,
          self);
      if (channel == NULL || channel->http_status != HTTP_SERVER_SEND)
        {
          gibber_transport_block_receiving (self->priv->transport, TRUE);
        }
      else
        {
          gibber_transport_block_receiving (self->priv->transport, FALSE);
        }
    }
}

static void
file_transfer_receive (GabbleFileTransferChannel *self)
{
  /* Client is connected, we can now receive data. Unblock the bytestream */
  if (self->priv->bytestream)
    {
      g_assert (self->priv->bytestream != NULL);
      gabble_bytestream_iface_block_reading (self->priv->bytestream, FALSE);
    }
  else if (self->priv->jingle)
    {
      JingleChannel *channel = g_hash_table_lookup (self->priv->jingle_channels,
          GINT_TO_POINTER (1));
      if (channel && !channel->agent_attached)
        {
          channel->agent_attached = TRUE;
          nice_agent_attach_recv (channel->agent, channel->stream_id,
              channel->component_id, g_main_context_default (),
              nice_data_received_cb, self);
        }
    }
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           GabbleFileTransferChannel *self)
{
  DEBUG ("transport to local socket has been disconnected");

  if (self->priv->transferred_bytes + self->priv->initial_offset <
      self->priv->size)
    {

      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR);

      close_session_and_transport (self);
    }
}

static void
transport_buffer_empty_cb (GibberTransport *transport,
                           GabbleFileTransferChannel *self)
{
  /* Buffer is empty so we can unblock the buffer if it was blocked */
  if (self->priv->bytestream)
    gabble_bytestream_iface_block_reading (self->priv->bytestream, FALSE);

  if (self->priv->jingle)
    {
      JingleChannel *channel = g_hash_table_lookup (self->priv->jingle_channels,
          GINT_TO_POINTER (1));
      if (channel && !channel->agent_attached)
        {
          channel->agent_attached = TRUE;
          nice_agent_attach_recv (channel->agent, channel->stream_id,
              channel->component_id, g_main_context_default (),
              nice_data_received_cb, self);
        }
    }

  if (self->priv->state > TP_FILE_TRANSFER_STATE_OPEN)
    gibber_transport_disconnect (transport);
}

/*
 * Some client is connecting to the Unix socket.
 */
static void
new_connection_cb (GibberListener *listener,
                   GibberTransport *transport,
                   struct sockaddr_storage *addr,
                   guint size,
                   gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  gboolean requested;
  TpBaseConnection *base_conn = (TpBaseConnection *)
      self->priv->connection;

  DEBUG ("Client connected to local socket");

  self->priv->transport = g_object_ref (transport);
  gabble_signal_connect_weak (transport, "disconnected",
    G_CALLBACK (transport_disconnected_cb), G_OBJECT (self));
  gabble_signal_connect_weak (transport, "buffer-empty",
    G_CALLBACK (transport_buffer_empty_cb), G_OBJECT (self));

  requested = (self->priv->initiator == base_conn->self_handle);

  if (!requested)
    /* Incoming file transfer */
    file_transfer_receive (self);
  else
    /* Outgoing file transfer */
    file_transfer_send (self);

  /* stop listening on local socket */
  g_object_unref (self->priv->listener);
  self->priv->listener = NULL;
}

static gboolean
setup_local_socket (GabbleFileTransferChannel *self,
                    TpSocketAddressType address_type,
                    TpSocketAccessControl access_control,
                    const GValue *access_control_param)
{
  GError *error = NULL;

  self->priv->listener = gibber_listener_new ();

  /* Add this stage the address_type and access_control have been checked and
   * are supposed to be valid */
#ifdef GIBBER_TYPE_UNIX_TRANSPORT
  if (address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      gchar *path;
      GArray *array;

      g_assert (access_control == TP_SOCKET_ACCESS_CONTROL_LOCALHOST);

      path = get_local_unix_socket_path (self);
      if (path == NULL)
        return FALSE;

      if (!gibber_listener_listen_socket (self->priv->listener, (gchar *) path,
            FALSE, &error))
        {
          DEBUG ("listen_socket failed: %s", error->message);
          g_error_free (error);
          g_object_unref (self->priv->listener);
          self->priv->listener = NULL;
          return FALSE;
        }

      array = g_array_sized_new (TRUE, FALSE, sizeof (gchar), strlen (path));
      g_array_insert_vals (array, 0, path, strlen (path));

      self->priv->socket_address = tp_g_value_slice_new (
          DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_set_boxed (self->priv->socket_address, array);

      DEBUG ("local socket %s", path);
      g_free (path);
      g_array_free (array, TRUE);
    }
  else
#endif
  if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      int ret;

      g_assert (access_control == TP_SOCKET_ACCESS_CONTROL_LOCALHOST);

      ret = gibber_listener_listen_tcp_loopback_af (self->priv->listener, 0,
          GIBBER_AF_IPV4, &error);
      if (!ret)
        {
          DEBUG ("Error listening on socket: %s", error->message);
          g_error_free (error);
          return FALSE;
        }

      self->priv->socket_address = tp_g_value_slice_new (
          TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4);
      g_value_take_boxed (self->priv->socket_address,
          dbus_g_type_specialized_construct (
              TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4));

      dbus_g_type_struct_set (self->priv->socket_address,
          0, "127.0.0.1",
          1, gibber_listener_get_port (self->priv->listener),
          G_MAXUINT);
    }
  else if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      int ret;

      g_assert (access_control == TP_SOCKET_ACCESS_CONTROL_LOCALHOST);

      ret = gibber_listener_listen_tcp_loopback_af (self->priv->listener, 0,
          GIBBER_AF_IPV6, &error);
      if (!ret)
        {
          DEBUG ("Error listening on socket: %s", error->message);
          g_error_free (error);
          return FALSE;
        }

      self->priv->socket_address = tp_g_value_slice_new (
          TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6);
      g_value_take_boxed (self->priv->socket_address,
          dbus_g_type_specialized_construct (
            TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6));

      dbus_g_type_struct_set (self->priv->socket_address,
          0, "::1",
          1, gibber_listener_get_port (self->priv->listener),
          G_MAXUINT);
    }
  else
    {
      g_assert_not_reached ();
    }

  self->priv->socket_type = address_type;

  gabble_signal_connect_weak (self->priv->listener, "new-connection",
    G_CALLBACK (new_connection_cb), G_OBJECT (self));

  return TRUE;
}

GabbleFileTransferChannel *
gabble_file_transfer_channel_new (GabbleConnection *conn,
                                  TpHandle handle,
                                  TpHandle initiator_handle,
                                  TpFileTransferState state,
                                  const gchar *content_type,
                                  const gchar *filename,
                                  guint64 size,
                                  TpFileHashType content_hash_type,
                                  const gchar *content_hash,
                                  const gchar *description,
                                  guint64 date,
                                  guint64 initial_offset,
                                  gboolean resume_supported)

{
  return g_object_new (GABBLE_TYPE_FILE_TRANSFER_CHANNEL,
      "connection", conn,
      "handle", handle,
      "initiator-handle", initiator_handle,
      "state", state,
      "content-type", content_type,
      "filename", filename,
      "size", size,
      "content-hash-type", content_hash_type,
      "content-hash", content_hash,
      "description", description,
      "date", date,
      "initial-offset", initial_offset,
      "resume-supported", resume_supported,
      NULL);
}
