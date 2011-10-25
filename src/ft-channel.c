/*
 * ft-channel.c - Source for GabbleFileTransferChannel
 * Copyright (C) 2009-2010 Collabora Ltd.
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


static void channel_iface_init (gpointer g_iface, gpointer iface_data);
static void file_transfer_iface_init (gpointer g_iface, gpointer iface_data);
static void transferred_chunk (GabbleFileTransferChannel *self, guint64 count);
static gboolean set_bytestream (GabbleFileTransferChannel *self,
    GabbleBytestreamIface *bytestream);
static gboolean set_gtalk_file_collection (GabbleFileTransferChannel *self,
    GTalkFileCollection *gtalk_file_collection);



G_DEFINE_TYPE_WITH_CODE (GabbleFileTransferChannel, gabble_file_transfer_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_FILE_TRANSFER,
                           file_transfer_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_FILETRANSFER_FUTURE,
                           NULL);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA,
                           NULL);
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
  PROP_FILE_COLLECTION,
  PROP_URI,

  PROP_CONNECTION,
  PROP_BYTESTREAM,

  /* Chan.Type.FileTransfer.FUTURE */
  PROP_GTALK_FILE_COLLECTION,

  /* Chan.Iface.FileTransfer.Metadata */
  PROP_SERVICE_NAME,
  PROP_METADATA,

  LAST_PROPERTY
};

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
  gboolean resume_supported;

  GTalkFileCollection *gtalk_file_collection;

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
  gchar *file_collection;
  gchar *uri;
  gchar *service_name;
  GHashTable *metadata;
  gboolean channel_opened;
};


void
gabble_file_transfer_channel_do_close (GabbleFileTransferChannel *self)
{
  if (self->priv->closed)
    return;

  DEBUG ("Emitting closed signal for %s", self->priv->object_path);
  self->priv->closed = TRUE;
  tp_svc_channel_emit_closed (self);
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
      case PROP_FILE_COLLECTION:
        g_value_set_string (value, self->priv->file_collection);
        break;
      case PROP_URI:
        g_value_set_string (value,
            self->priv->uri != NULL ? self->priv->uri: "");
        break;
      case PROP_CHANNEL_DESTROYED:
        g_value_set_boolean (value, self->priv->closed);
        break;
      case PROP_RESUME_SUPPORTED:
        g_value_set_boolean (value, self->priv->resume_supported);
        break;
      case PROP_CHANNEL_PROPERTIES:
        {
          GHashTable *props;

          props = tp_dbus_properties_mixin_make_properties_hash (object,
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
              GABBLE_IFACE_CHANNEL_TYPE_FILETRANSFER_FUTURE, "FileCollection",
              NULL);

          /* URI is immutable only for outgoing transfers */
          if (self->priv->initiator == base_conn->self_handle)
            {
              tp_dbus_properties_mixin_fill_properties_hash (object, props,
                  TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "URI", NULL);
            }

          g_value_take_boxed (value, props);
        }
        break;
      case PROP_BYTESTREAM:
        g_value_set_object (value, self->priv->bytestream);
        break;
      case PROP_GTALK_FILE_COLLECTION:
        g_value_set_object (value, self->priv->gtalk_file_collection);
        break;
      case PROP_SERVICE_NAME:
        g_value_set_string (value, self->priv->service_name);
        break;
      case PROP_METADATA:
        {
          /* We're fine with priv->metadata being NULL but dbus-glib
           * doesn't like iterating NULL as if it was a hash table. */
          if (self->priv->metadata == NULL)
            {
              g_value_take_boxed (value,
                  g_hash_table_new (g_str_hash, g_str_equal));
            }
          else
            {
              g_value_set_boxed (value, self->priv->metadata);
            }
        }
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
      /* these properties are writable in the interface, but not actually
       * meaningfully changeable on this channel, so we do nothing */
      case PROP_HANDLE_TYPE:
      case PROP_CHANNEL_TYPE:
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
      case PROP_FILE_COLLECTION:
        g_free (self->priv->file_collection);
        self->priv->file_collection = g_value_dup_string (value);
        break;
      case PROP_URI:
        g_assert (self->priv->uri == NULL); /* construct only */
        self->priv->uri = g_value_dup_string (value);
        break;
      case PROP_RESUME_SUPPORTED:
        self->priv->resume_supported = g_value_get_boolean (value);
        break;
      case PROP_BYTESTREAM:
        set_bytestream (self,
            GABBLE_BYTESTREAM_IFACE (g_value_get_object (value)));
        break;
      case PROP_GTALK_FILE_COLLECTION:
        set_gtalk_file_collection (self,
            GTALK_FILE_COLLECTION (g_value_get_object (value)));
        break;
      case PROP_SERVICE_NAME:
        self->priv->service_name = g_value_dup_string (value);
        break;
      case PROP_METADATA:
        self->priv->metadata = g_value_dup_boxed (value);
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
  TpDBusDaemon *bus;
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
  bus = tp_base_connection_get_dbus_daemon (base_conn);
  tp_dbus_daemon_register_object (bus, self->priv->object_path, obj);

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

  if (self->priv->initiator != base_conn->self_handle)
    /* Incoming transfer, URI has to be set by the handler */
    g_assert (self->priv->uri == NULL);

  return obj;
}

static void close_session_and_transport (GabbleFileTransferChannel *self);
static void gabble_file_transfer_channel_dispose (GObject *object);
static void gabble_file_transfer_channel_finalize (GObject *object);

static gboolean
file_transfer_channel_properties_setter (GObject *object,
    GQuark interface,
    GQuark name,
    const GValue *value,
    gpointer setter_data,
    GError **error)
{
  GabbleFileTransferChannel *self = (GabbleFileTransferChannel *) object;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->connection);

  g_return_val_if_fail (interface == TP_IFACE_QUARK_CHANNEL_TYPE_FILE_TRANSFER,
      FALSE);

  /* There is only one property with write access. So TpDBusPropertiesMixin
   * already checked this. */
  g_assert (name == g_quark_from_static_string ("URI"));

  /* TpDBusPropertiesMixin already checked this */
  g_assert (G_VALUE_HOLDS_STRING (value));

  if (self->priv->uri != NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "URI has already be set");
      return FALSE;
    }

  if (self->priv->initiator == base_conn->self_handle)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Channel is not an incoming transfer");
      return FALSE;
    }

  if (self->priv->state != TP_FILE_TRANSFER_STATE_PENDING)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "State is not pending; cannot set URI");
      return FALSE;
    }

  self->priv->uri = g_value_dup_string (value);

  tp_svc_channel_type_file_transfer_emit_uri_defined (self, self->priv->uri);

  return TRUE;
}

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
    { "URI", "uri", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinPropImpl file_future_props[] = {
    { "FileCollection", "file-collection", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinPropImpl file_metadata_props[] = {
    { "ServiceName", "service-name", NULL },
    { "Metadata", "metadata", NULL },
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
      file_transfer_channel_properties_setter,
      file_props
    },
    { GABBLE_IFACE_CHANNEL_TYPE_FILETRANSFER_FUTURE,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      file_future_props
    },
    { GABBLE_IFACE_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      file_metadata_props
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

  param_spec = g_param_spec_object (
      "bytestream",
      "Object implementing the GabbleBytestreamIface interface",
      "Bytestream object used to send the file",
      G_TYPE_OBJECT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_BYTESTREAM,
      param_spec);

  param_spec = g_param_spec_object (
      "gtalk-file-collection",
      "GTalkFileCollection object for gtalk-compatible file transfer",
      "GTalk compatible file transfer collection",
      G_TYPE_OBJECT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_GTALK_FILE_COLLECTION,
      param_spec);

  param_spec = g_param_spec_boolean (
      "resume-supported",
      "resume is supported",
      "TRUE if resume is supported on this file transfer channel",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RESUME_SUPPORTED,
      param_spec);

  param_spec = g_param_spec_string (
      "file-collection",
      "gchar *file_colletion",
      "Token identifying a collection of files",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FILE_COLLECTION,
      param_spec);

  param_spec = g_param_spec_string (
      "uri", "URI",
      "URI of the file being transferred",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_URI,
      param_spec);

  param_spec = g_param_spec_string ("service-name",
      "ServiceName",
      "The Metadata.ServiceName property of this channel",
      "",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SERVICE_NAME,
      param_spec);

  param_spec = g_param_spec_boxed ("metadata",
      "Metadata",
      "The Metadata.Metadata property of this channel",
      TP_HASH_TYPE_STRING_STRING_MAP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_METADATA,
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
  gabble_file_transfer_channel_do_close (self);

  tp_handle_unref (handle_repo, self->priv->handle);
  tp_handle_unref (handle_repo, self->priv->initiator);


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
  g_free (self->priv->file_collection);
  g_free (self->priv->uri);
  g_free (self->priv->service_name);
  if (self->priv->metadata != NULL)
    g_hash_table_unref (self->priv->metadata);

  G_OBJECT_CLASS (gabble_file_transfer_channel_parent_class)->finalize (object);
}

static void
close_session_and_transport (GabbleFileTransferChannel *self)
{

  DEBUG ("Closing session and transport");

  if (self->priv->gtalk_file_collection != NULL)
    gtalk_file_collection_terminate (self->priv->gtalk_file_collection, self);

  tp_clear_object (&self->priv->gtalk_file_collection);

  if (self->priv->bytestream != NULL)
    gabble_bytestream_iface_close (self->priv->bytestream, NULL);

  tp_clear_object (&self->priv->bytestream);
  tp_clear_object (&self->priv->listener);
  tp_clear_object (&self->priv->transport);
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
  DEBUG ("Channel open");

  /* This is needed in case the ProvideFile wasn't called yet, to know if we
     should go into OPEN state when ProvideFile gets called. */
  self->priv->channel_opened = TRUE;

  if (self->priv->socket_address != NULL)
    {
      /* ProvideFile has already been called. Channel is Open */
      tp_svc_channel_type_file_transfer_emit_initial_offset_defined (self,
          self->priv->initial_offset);

      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_OPEN,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

      if (self->priv->transport != NULL)
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
bytestream_closed (GabbleFileTransferChannel *self)
{
  if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED &&
      self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
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
      bytestream_closed (self);
    }
}

static void bytestream_write_blocked_cb (GabbleBytestreamIface *bytestream,
                                         gboolean blocked,
                                         GabbleFileTransferChannel *self);
static gboolean
set_bytestream (GabbleFileTransferChannel *self,
    GabbleBytestreamIface *bytestream)

{
  if (bytestream == NULL)
    return FALSE;

  g_return_val_if_fail (self->priv->bytestream == NULL, FALSE);
  g_return_val_if_fail (self->priv->gtalk_file_collection == NULL, FALSE);

  DEBUG ("Setting bytestream to %p", bytestream);

  self->priv->bytestream = g_object_ref (bytestream);

  gabble_signal_connect_weak (bytestream, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), G_OBJECT (self));
  gabble_signal_connect_weak (bytestream, "write-blocked",
      G_CALLBACK (bytestream_write_blocked_cb), G_OBJECT (self));

  return TRUE;
}

static gboolean
set_gtalk_file_collection (
    GabbleFileTransferChannel *self, GTalkFileCollection *gtalk_file_collection)
{
  if (gtalk_file_collection == NULL)
      return FALSE;

  g_return_val_if_fail (self->priv->bytestream == NULL, FALSE);
  g_return_val_if_fail (self->priv->gtalk_file_collection == NULL, FALSE);

  self->priv->gtalk_file_collection = g_object_ref (gtalk_file_collection);

  /* No need to listen to any signals, the GTalkFileCollection will call our callbacks
     on his own */

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

  file = lm_message_node_find_child (wocky_stanza_get_top_node (msg), "file");
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

  set_bytestream (self, bytestream);

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

  DEBUG ("Offering SI Bytestream file transfer to %s", full_jid);

  /* Outgoing FT , we'll need SOCK5 proxies */
  gabble_bytestream_factory_query_socks5_proxies (
      self->priv->connection->bytestream_factory);


  stream_id = gabble_bytestream_factory_generate_stream_id ();

  msg = gabble_bytestream_factory_make_stream_init_iq (full_jid,
      stream_id, NS_FILE_TRANSFER);

  si_node = lm_message_node_get_child_with_namespace (
      wocky_stanza_get_top_node (msg), "si", NS_SI);
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


void
gabble_file_transfer_channel_gtalk_file_collection_state_changed (
    GabbleFileTransferChannel *self,
    GTalkFileCollectionState state, gboolean local_terminator)
{
  DEBUG ("gtalk ft state changed to %d", state);
  switch (state)
    {
      case GTALK_FILE_COLLECTION_STATE_PENDING:
        gabble_file_transfer_channel_set_state (
            TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
            TP_FILE_TRANSFER_STATE_PENDING,
            TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
        break;
      case GTALK_FILE_COLLECTION_STATE_ACCEPTED:
        if (self->priv->state == TP_FILE_TRANSFER_STATE_PENDING)
          {
            gabble_file_transfer_channel_set_state (
                TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
                TP_FILE_TRANSFER_STATE_ACCEPTED,
                TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
          }
        break;
      case GTALK_FILE_COLLECTION_STATE_OPEN:
        channel_open (self);
        break;
      case GTALK_FILE_COLLECTION_STATE_TERMINATED:
        if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED &&
            self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
          {
            gabble_file_transfer_channel_set_state (
                TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
                TP_FILE_TRANSFER_STATE_CANCELLED,
                local_terminator ?
                TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_STOPPED:
                TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_STOPPED);
          }
        close_session_and_transport (self);
        break;
      case GTALK_FILE_COLLECTION_STATE_ERROR:
      case GTALK_FILE_COLLECTION_STATE_CONNECTION_FAILED:
        gabble_file_transfer_channel_set_state (
            TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
            TP_FILE_TRANSFER_STATE_CANCELLED,
            TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR);

        close_session_and_transport (self);
        break;
      case GTALK_FILE_COLLECTION_STATE_COMPLETED:
        gabble_file_transfer_channel_set_state (
            TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
            TP_FILE_TRANSFER_STATE_COMPLETED,
            TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

        if (self->priv->transport &&
            gibber_transport_buffer_is_empty (self->priv->transport))
          gibber_transport_disconnect (self->priv->transport);
        break;
    }
}

static gboolean
offer_gtalk_file_transfer (GabbleFileTransferChannel *self,
    const gchar *full_jid, GError **error)
{

  GTalkFileCollection *gtalk_file_collection;

  DEBUG ("Offering Gtalk file transfer to %s", full_jid);

  gtalk_file_collection = gtalk_file_collection_new (self,
      self->priv->connection->jingle_factory, self->priv->handle, full_jid);

  g_return_val_if_fail (gtalk_file_collection != NULL, FALSE);

  set_gtalk_file_collection (self, gtalk_file_collection);

  gtalk_file_collection_initiate (self->priv->gtalk_file_collection, self);

  /* We would have gotten a set_gtalk_file_collection so we already hold an
     additional reference to the object, so we can drop the reference we got
     from the gtalk_file_collection_new. If we didn't get our
     set_gtalk_file_collection called, then the ft manager doesn't handle us,
     so it's best to just destroy it anyways */
  g_object_unref (gtalk_file_collection);

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
  gboolean jingle_share = FALSE;
  const gchar *si_resource = NULL;
  const gchar *share_resource = NULL;
  g_assert (!CHECK_STR_EMPTY (self->priv->filename));
  g_assert (self->priv->size != GABBLE_UNDEFINED_FILE_SIZE);
  g_return_val_if_fail (self->priv->bytestream == NULL, FALSE);
  g_return_val_if_fail (self->priv->gtalk_file_collection == NULL, FALSE);

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
      si_resource = gabble_presence_pick_resource_by_caps (presence, 0,
         gabble_capability_set_predicate_has, NS_FILE_TRANSFER);
      si = (si_resource != NULL);

      share_resource = gabble_presence_pick_resource_by_caps (presence, 0,
          gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_SHARE);
      jingle_share  = (share_resource != NULL);
    }
  else
    {
      /* MUC jid, we already have the full jid */
      si = gabble_presence_has_cap (presence, NS_FILE_TRANSFER);
      jingle_share = gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_SHARE);
    }

  /* Use bytestream if we have SI, but no jingle-share or if we have SI and
     jingle-share but we have no google relay token */
  if (si &&
      (!jingle_share ||
          gabble_jingle_factory_get_google_relay_token (
              self->priv->connection->jingle_factory) == NULL))
    {
      result = offer_bytestream (self, jid, si_resource, error);
    }
  else if (jingle_share)
    {
      gchar *full_jid = gabble_peer_to_jid (self->priv->connection,
        self->priv->handle, share_resource);
      result = offer_gtalk_file_transfer (self, full_jid, error);
      g_free (full_jid);
    }
  else
    {
      DEBUG ("contact doesn't have file transfer capabilities");
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_CAPABLE,
          "contact doesn't have file transfer capabilities");
      result = FALSE;
    }

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
data_received_cb (GabbleFileTransferChannel *self, const guint8 *data, guint len)
{
  GError *error = NULL;

  g_assert (self->priv->transport != NULL);

  if (!gibber_transport_send (self->priv->transport, data, len, &error))
    {
      DEBUG ("sending to transport failed: %s", error->message);
      g_error_free (error);

      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR);
      return;
    }

  transferred_chunk (self, (guint64) len);

  if (self->priv->bytestream != NULL &&
      self->priv->transferred_bytes + self->priv->initial_offset >=
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
      if (self->priv->bytestream != NULL)
        gabble_bytestream_iface_block_reading (self->priv->bytestream, TRUE);
      else if (self->priv->gtalk_file_collection != NULL)
        gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
            self, TRUE);
    }
}


void
gabble_file_transfer_channel_gtalk_file_collection_data_received (
    GabbleFileTransferChannel *self, const gchar *data, guint len)
{
  data_received_cb (self, (const guint8 *) data, len);
}


static void
bytestream_data_received_cb (GabbleBytestreamIface *stream,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  data_received_cb (self, (const guint8 *) data->str, data->len);
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

  if (self->priv->bytestream != NULL)
    {
      gabble_signal_connect_weak (self->priv->bytestream, "data-received",
          G_CALLBACK (bytestream_data_received_cb), G_OBJECT (self));


      /* Block the bytestream while the user is not connected to the socket */
      gabble_bytestream_iface_block_reading (self->priv->bytestream, TRUE);

      /* channel state will change to open once the bytestream is open */
      gabble_bytestream_iface_accept (self->priv->bytestream, augment_si_reply,
          self);
    }
  else if (self->priv->gtalk_file_collection != NULL)
    {
      /* Block the gtalk ft stream while the user is not connected
         to the socket */
      gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
          self, TRUE);
      gtalk_file_collection_accept (self->priv->gtalk_file_collection, self);
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

  if (self->priv->channel_opened)
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

  if (self->priv->bytestream != NULL)
    {
      if (!gabble_bytestream_iface_send (self->priv->bytestream, data->length,
              (const gchar *) data->data))
        {
          DEBUG ("Sending failed. Closing the bytestream");
          close_session_and_transport (self);
          return;
        }
    }
  else if (self->priv->gtalk_file_collection != NULL)
    {
      if (!gtalk_file_collection_send_data (self->priv->gtalk_file_collection,
              self, (const gchar *) data->data, data->length))
        {
          DEBUG ("Sending failed. Closing the jingle session");
          close_session_and_transport (self);
          return;
        }
    }

  transferred_chunk (self, (guint64) data->length);

  if (self->priv->transferred_bytes + self->priv->initial_offset >=
      self->priv->size)
    {
      if (self->priv->bytestream != NULL)
        {
          DEBUG ("All the file has been sent. Closing the bytestream");
          gabble_file_transfer_channel_set_state (
              TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
              TP_FILE_TRANSFER_STATE_COMPLETED,
              TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
          gabble_bytestream_iface_close (self->priv->bytestream, NULL);
        }
      else if (self->priv->gtalk_file_collection != NULL)
        {
          DEBUG ("All the file has been sent.");
          gtalk_file_collection_completed (self->priv->gtalk_file_collection,
              self);
        }
    }
}

static void
bytestream_write_blocked_cb (GabbleBytestreamIface *bytestream,
                             gboolean blocked,
                             GabbleFileTransferChannel *self)
{
  if (self->priv->transport != NULL)
    gibber_transport_block_receiving (self->priv->transport, blocked);
}

void
gabble_file_transfer_channel_gtalk_file_collection_write_blocked (
    GabbleFileTransferChannel *self, gboolean blocked)
{
  if (self->priv->transport != NULL)
    gibber_transport_block_receiving (self->priv->transport, blocked);
}


static void
file_transfer_send (GabbleFileTransferChannel *self)
{
  /* We shouldn't receive data if the bytestream isn't open otherwise it
     will error out */
  if (self->priv->state == TP_FILE_TRANSFER_STATE_OPEN)
    gibber_transport_block_receiving (self->priv->transport, FALSE);
  else
    gibber_transport_block_receiving (self->priv->transport, TRUE);

  gibber_transport_set_handler (self->priv->transport, transport_handler,
      self);
}

static void
file_transfer_receive (GabbleFileTransferChannel *self)
{
  /* Client is connected, we can now receive data. Unblock the bytestream */
  if (self->priv->bytestream != NULL)
    gabble_bytestream_iface_block_reading (self->priv->bytestream, FALSE);
  else if (self->priv->gtalk_file_collection != NULL)
    gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
        self, FALSE);
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           GabbleFileTransferChannel *self)
{
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->connection;
  gboolean requested = (self->priv->initiator == base_conn->self_handle);

  DEBUG ("transport to local socket has been disconnected");

  /* If we are sending the file, we can expect the transport to be closed as
     soon as we received all the data. Otherwise, it should only get closed once
     the channel has gone to state COMPLETED.
     This allows to make sure we detect an error if the channel is closed while
     receiving a gtalk-ft folder where the size is an approximation of the real
     size to be received */
  if ((requested &&
          self->priv->transferred_bytes + self->priv->initial_offset <
          self->priv->size) ||
      (!requested && self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED))
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
  if (self->priv->bytestream != NULL)
    gabble_bytestream_iface_block_reading (self->priv->bytestream, FALSE);

  if (self->priv->gtalk_file_collection != NULL)
    gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
        self, FALSE);

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
  tp_clear_object (&self->priv->listener);
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
          tp_clear_object (&self->priv->listener);
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
          DEBUG ("Error listening on ipv4 socket: %s", error->message);
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
          DEBUG ("Error listening on ipv6 socket: %s", error->message);
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
                                  gboolean resume_supported,
                                  GabbleBytestreamIface *bytestream,
                                  GTalkFileCollection *gtalk_file_collection,
                                  const gchar *file_collection,
                                  const gchar *uri,
                                  const gchar *service_name,
                                  const GHashTable *metadata)

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
      "file-collection", file_collection,
      "bytestream", bytestream,
      "gtalk-file-collection", gtalk_file_collection,
      "uri", uri,
      "service-name", service_name,
      "metadata", metadata,
      NULL);
}
