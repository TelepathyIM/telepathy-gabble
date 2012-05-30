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

#include <gibber/gibber-listener.h>
#include <gibber/gibber-transport.h>
#include <gibber/gibber-unix-transport.h>       /* just for the feature-test */

#include "connection.h"
#include "ft-channel.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "util.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

static void file_transfer_iface_init (gpointer g_iface, gpointer iface_data);
static void transferred_chunk (GabbleFileTransferChannel *self, guint64 count);
static gboolean set_bytestream (GabbleFileTransferChannel *self,
    GabbleBytestreamIface *bytestream);
#ifdef ENABLE_JINGLE_FILE_TRANSFER
static gboolean set_gtalk_file_collection (GabbleFileTransferChannel *self,
    GTalkFileCollection *gtalk_file_collection);
#endif


G_DEFINE_TYPE_WITH_CODE (GabbleFileTransferChannel, gabble_file_transfer_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_FILE_TRANSFER,
                           file_transfer_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_FILETRANSFER_FUTURE,
                           NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA,
                           NULL);
);

#define GABBLE_UNDEFINED_FILE_SIZE G_MAXUINT64

static const char *gabble_file_transfer_channel_interfaces[] = { NULL };

/* properties */
enum
{
  /* Channel.Type.FileTransfer D-Bus properties */
  PROP_STATE = 1,
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

#ifdef ENABLE_JINGLE_FILE_TRANSFER
  /* Chan.Type.FileTransfer.FUTURE */
  PROP_GTALK_FILE_COLLECTION,
#endif

  /* Chan.Iface.FileTransfer.Metadata */
  PROP_SERVICE_NAME,
  PROP_METADATA,

  LAST_PROPERTY
};

/* private structure */
struct _GabbleFileTransferChannelPrivate {
  gboolean dispose_has_run;
  GTimeVal last_transferred_bytes_emitted;
  guint progress_timer;
  TpSocketAddressType socket_type;
  GValue *socket_address;
  gboolean resume_supported;

#ifdef ENABLE_JINGLE_FILE_TRANSFER
  GTalkFileCollection *gtalk_file_collection;
#endif

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

static void gabble_file_transfer_channel_set_state (
    TpSvcChannelTypeFileTransfer *iface, TpFileTransferState state,
    TpFileTransferStateChangeReason reason);
static void close_session_and_transport (GabbleFileTransferChannel *self);

static void
gabble_file_transfer_channel_close (TpBaseChannel *base)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (base);

  if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED &&
      self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
    {
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_STOPPED);

      close_session_and_transport (self);
    }

  tp_base_channel_destroyed (base);
}

static void
gabble_file_transfer_channel_init (GabbleFileTransferChannel *obj)
{
  obj->priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      GABBLE_TYPE_FILE_TRANSFER_CHANNEL, GabbleFileTransferChannelPrivate);
}

static void
gabble_file_transfer_channel_get_property (GObject *object,
                                           guint property_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (object);

  switch (property_id)
    {
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
      case PROP_RESUME_SUPPORTED:
        g_value_set_boolean (value, self->priv->resume_supported);
        break;
      case PROP_BYTESTREAM:
        g_value_set_object (value, self->priv->bytestream);
        break;
#ifdef ENABLE_JINGLE_FILE_TRANSFER
      case PROP_GTALK_FILE_COLLECTION:
        g_value_set_object (value, self->priv->gtalk_file_collection);
        break;
#endif
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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
      case PROP_GTALK_FILE_COLLECTION:
        set_gtalk_file_collection (self,
            GTALK_FILE_COLLECTION (g_value_get_object (value)));
        break;
#endif
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
  g_array_unref (array);
}

static void
connection_presences_updated_cb (GabblePresenceCache *cache,
                                 GArray *handles,
                                 GabbleFileTransferChannel *self)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  guint i;

  for (i = 0; i < handles->len ; i++)
    {
      TpHandle handle;

      handle = g_array_index (handles, TpHandle, i);
      if (handle == tp_base_channel_get_target_handle (base))
        {
          GabblePresence *presence;

          presence = gabble_presence_cache_get (
              conn->presence_cache, handle);

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

static void
gabble_file_transfer_channel_constructed (GObject *obj)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (obj);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);
  GArray *socket_access;
  TpSocketAccessControl access_control;

  /* Parent constructed chain */
  void (*chain_up) (GObject *) =
    ((GObjectClass *) gabble_file_transfer_channel_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (obj);

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

  gabble_signal_connect_weak (conn->presence_cache,
      "presences-updated", G_CALLBACK (connection_presences_updated_cb), obj);

  DEBUG ("New FT channel created: %s (contact: %s, initiator: %s, "
      "file: \"%s\", size: %" G_GUINT64_FORMAT ")",
      tp_base_channel_get_object_path (base),
      tp_handle_inspect (contact_repo, tp_base_channel_get_target_handle (base)),
      tp_handle_inspect (contact_repo,
          tp_base_channel_get_initiator (base)),
       self->priv->filename, self->priv->size);

  if (!tp_base_channel_is_requested (base))
    /* Incoming transfer, URI has to be set by the handler */
    g_assert (self->priv->uri == NULL);
}

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
  TpBaseChannel *base = TP_BASE_CHANNEL (self);

  g_return_val_if_fail (interface == TP_IFACE_QUARK_CHANNEL_TYPE_FILE_TRANSFER,
      FALSE);

  /* There is only one property with write access. So TpDBusPropertiesMixin
   * already checked this. */
  g_assert (name == g_quark_from_static_string ("URI"));

  /* TpDBusPropertiesMixin already checked this */
  g_assert (G_VALUE_HOLDS_STRING (value));

  if (self->priv->uri != NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "URI has already be set");
      return FALSE;
    }

  if (tp_base_channel_is_requested (base))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Channel is not an incoming transfer");
      return FALSE;
    }

  if (self->priv->state != TP_FILE_TRANSFER_STATE_PENDING)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
        "State is not pending; cannot set URI");
      return FALSE;
    }

  self->priv->uri = g_value_dup_string (value);

  tp_svc_channel_type_file_transfer_emit_uri_defined (self, self->priv->uri);

  return TRUE;
}

static void
gabble_file_transfer_channel_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      gabble_file_transfer_channel_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
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
      TP_IFACE_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA, "ServiceName",
      TP_IFACE_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA, "Metadata",
      NULL);

  /* URI is immutable only for outgoing transfers */
  if (tp_base_channel_is_requested (chan))
    {
      tp_dbus_properties_mixin_fill_properties_hash (G_OBJECT (chan), properties,
          TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "URI", NULL);
    }
}

static gchar *
gabble_file_transfer_channel_get_object_path_suffix (TpBaseChannel *chan)
{
  return g_strdup_printf ("FileTransferChannel/%p", chan);
}

static void
gabble_file_transfer_channel_class_init (
    GabbleFileTransferChannelClass *gabble_file_transfer_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gabble_file_transfer_channel_class);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (
      gabble_file_transfer_channel_class);
  GParamSpec *param_spec;

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
    { TP_IFACE_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA,
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
  object_class->constructed = gabble_file_transfer_channel_constructed;
  object_class->get_property = gabble_file_transfer_channel_get_property;
  object_class->set_property = gabble_file_transfer_channel_set_property;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER;
  base_class->interfaces = gabble_file_transfer_channel_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_class->close = gabble_file_transfer_channel_close;
  base_class->fill_immutable_properties =
    gabble_file_transfer_channel_fill_immutable_properties;
  base_class->get_object_path_suffix =
    gabble_file_transfer_channel_get_object_path_suffix;

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

#ifdef ENABLE_JINGLE_FILE_TRANSFER
  param_spec = g_param_spec_object (
      "gtalk-file-collection",
      "GTalkFileCollection object for gtalk-compatible file transfer",
      "GTalk compatible file transfer collection",
      G_TYPE_OBJECT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_GTALK_FILE_COLLECTION,
      param_spec);
#endif

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
      TP_HASH_TYPE_METADATA,
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

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  self->priv->dispose_has_run = TRUE;

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
  g_free (self->priv->filename);
  if (self->priv->socket_address != NULL)
    tp_g_value_slice_free (self->priv->socket_address);
  g_free (self->priv->content_type);
  g_free (self->priv->content_hash);
  g_free (self->priv->description);
  g_hash_table_unref (self->priv->available_socket_types);
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

#ifdef ENABLE_JINGLE_FILE_TRANSFER
  if (self->priv->gtalk_file_collection != NULL)
    gtalk_file_collection_terminate (self->priv->gtalk_file_collection, self);

  tp_clear_object (&self->priv->gtalk_file_collection);
#endif

  if (self->priv->bytestream != NULL)
    gabble_bytestream_iface_close (self->priv->bytestream, NULL);

  tp_clear_object (&self->priv->bytestream);
  tp_clear_object (&self->priv->listener);
  tp_clear_object (&self->priv->transport);
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
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
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

  g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
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
      gboolean receiver = !tp_base_channel_is_requested (
          TP_BASE_CHANNEL (self));

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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
  g_return_val_if_fail (self->priv->gtalk_file_collection == NULL, FALSE);
#endif

  DEBUG ("Setting bytestream to %p", bytestream);

  self->priv->bytestream = g_object_ref (bytestream);

  gabble_signal_connect_weak (bytestream, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), G_OBJECT (self));
  gabble_signal_connect_weak (bytestream, "write-blocked",
      G_CALLBACK (bytestream_write_blocked_cb), G_OBJECT (self));

  return TRUE;
}

#ifdef ENABLE_JINGLE_FILE_TRANSFER
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
#endif

static void
bytestream_negotiate_cb (GabbleBytestreamIface *bytestream,
                         const gchar *stream_id,
                         WockyStanza *msg,
                         GObject *object,
                         gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  WockyNode *si;
  WockyNode *file = NULL;

  if (bytestream == NULL)
    {
      DEBUG ("receiver refused file offer");
      gabble_file_transfer_channel_set_state (
          TP_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
          TP_FILE_TRANSFER_STATE_CANCELLED,
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_STOPPED);
      return;
    }

  si = wocky_node_get_child_ns (wocky_stanza_get_top_node (msg), "si", NS_SI);
  if (si != NULL)
    file = wocky_node_get_child_ns (si, "file", NULL);

  if (file != NULL)
    {
      WockyNode *range;

      range = wocky_node_get_child (file, "range");
      if (range != NULL)
        {
          const gchar *offset_str;

          offset_str = wocky_node_get_attribute (range, "offset");
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

static void
add_metadata_forms (GabbleFileTransferChannel *self,
    WockyNode *file)
{
  if (!tp_str_empty (self->priv->service_name))
    {
      WockyStanza *tmp = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT, NULL, NULL,
          '(', "x",
            ':', NS_X_DATA,
            '@', "type", "result",
            '(', "field",
              '@', "var", "FORM_TYPE",
              '@', "type", "hidden",
              '(', "value",
                '$', NS_TP_FT_METADATA_SERVICE,
              ')',
            ')',
            '(', "field",
              '@', "var", "ServiceName",
              '(', "value",
                '$', self->priv->service_name,
              ')',
            ')',
          ')',
          NULL);
      WockyNode *x = wocky_node_get_first_child (wocky_stanza_get_top_node (tmp));
      WockyNodeTree *tree = wocky_node_tree_new_from_node (x);

      wocky_node_add_node_tree (file, tree);
      g_object_unref (tree);
      g_object_unref (tmp);
    }

  if (self->priv->metadata != NULL
      && g_hash_table_size (self->priv->metadata) > 0)
    {
      WockyStanza *tmp = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT, NULL, NULL,
          '(', "x",
            ':', NS_X_DATA,
            '@', "type", "result",
            '(', "field",
              '@', "var", "FORM_TYPE",
              '@', "type", "hidden",
              '(', "value",
                '$', NS_TP_FT_METADATA,
              ')',
            ')',
          ')',
          NULL);
      WockyNode *x = wocky_node_get_first_child (wocky_stanza_get_top_node (tmp));
      WockyNodeTree *tree;
      GHashTableIter iter;
      gpointer key, val;

      g_hash_table_iter_init (&iter, self->priv->metadata);
      while (g_hash_table_iter_next (&iter, &key, &val))
        {
          const gchar * const *values = val;

          WockyNode *field = wocky_node_add_child (x, "field");
          wocky_node_set_attribute (field, "var", key);

          for (; values != NULL && *values != NULL; values++)
            {
              wocky_node_add_child_with_content (field, "value", *values);
            }
        }

      tree = wocky_node_tree_new_from_node (x);
      wocky_node_add_node_tree (file, tree);
      g_object_unref (tree);
      g_object_unref (tmp);
    }
}

static gboolean
offer_bytestream (GabbleFileTransferChannel *self, const gchar *jid,
                  const gchar *resource, GError **error)
{
  GabbleConnection *conn = GABBLE_CONNECTION (tp_base_channel_get_connection (
          TP_BASE_CHANNEL (self)));
  gboolean result;
  WockyStanza *msg;
  WockyNode *si_node, *file_node;
  gchar *stream_id, *size_str, *full_jid;

  if (resource)
    full_jid = g_strdup_printf ("%s/%s", jid, resource);
  else
    full_jid = g_strdup (jid);

  DEBUG ("Offering SI Bytestream file transfer to %s", full_jid);

  /* Outgoing FT , we'll need SOCK5 proxies */
  gabble_bytestream_factory_query_socks5_proxies (
      conn->bytestream_factory);


  stream_id = gabble_bytestream_factory_generate_stream_id ();

  msg = gabble_bytestream_factory_make_stream_init_iq (full_jid,
      stream_id, NS_FILE_TRANSFER);

  si_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "si", NS_SI);
  g_assert (si_node != NULL);

  size_str = g_strdup_printf ("%" G_GUINT64_FORMAT, self->priv->size);

  file_node = wocky_node_add_child_with_content (si_node, "file", NULL);
  file_node->ns = g_quark_from_string (NS_FILE_TRANSFER);
  wocky_node_set_attributes (file_node,
      "name", self->priv->filename,
      "size", size_str,
      "mime-type", self->priv->content_type,
      NULL);

  add_metadata_forms (self, file_node);

  if (self->priv->content_hash != NULL)
    wocky_node_set_attribute (file_node, "hash", self->priv->content_hash);

  if (self->priv->date != 0)
    {
      time_t t;
      struct tm *tm;
      char date_str[21];

      t = (time_t) self->priv->date;
      tm = gmtime (&t);

#ifdef G_OS_WIN32
      strftime (date_str, sizeof (date_str), "%Y-%m-%dT%H:%M:%SZ", tm);
#else
      strftime (date_str, sizeof (date_str), "%FT%H:%M:%SZ", tm);
#endif

      wocky_node_set_attribute (file_node, "date", date_str);
    }

  wocky_node_add_child_with_content (file_node, "desc", self->priv->description);

  /* we support resume */
  wocky_node_add_child_with_content (file_node, "range", NULL);

  result = gabble_bytestream_factory_negotiate_stream (
      conn->bytestream_factory, msg, stream_id,
      bytestream_negotiate_cb, self, G_OBJECT (self), error);

  g_object_unref (msg);
  g_free (stream_id);
  g_free (size_str);
  g_free (full_jid);

  return result;
}

#ifdef ENABLE_JINGLE_FILE_TRANSFER
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
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GabbleConnection *conn = GABBLE_CONNECTION (
      tp_base_channel_get_connection (base));
  GabbleJingleFactory *jf;
  GTalkFileCollection *gtalk_file_collection;

  DEBUG ("Offering Gtalk file transfer to %s", full_jid);

  jf = gabble_jingle_mint_get_factory (conn->jingle_mint);
  g_return_val_if_fail (jf != NULL, FALSE);

  gtalk_file_collection = gtalk_file_collection_new (self,
      jf,
      tp_base_channel_get_target_handle (base), full_jid);

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
#endif

gboolean
gabble_file_transfer_channel_offer_file (GabbleFileTransferChannel *self,
                                         GError **error)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  GabblePresence *presence;
  gboolean result;
  TpHandleRepoIface *contact_repo, *room_repo;
  const gchar *jid;
  gboolean si = FALSE;
  gboolean use_si = FALSE;
  const gchar *si_resource = NULL;
#ifdef ENABLE_JINGLE_FILE_TRANSFER
  gboolean jingle_share = FALSE;
  const gchar *share_resource = NULL;
#endif

  g_assert (!tp_str_empty (self->priv->filename));
  g_assert (self->priv->size != GABBLE_UNDEFINED_FILE_SIZE);
  g_return_val_if_fail (self->priv->bytestream == NULL, FALSE);
#ifdef ENABLE_JINGLE_FILE_TRANSFER
  g_return_val_if_fail (self->priv->gtalk_file_collection == NULL, FALSE);
#endif

  presence = gabble_presence_cache_get (conn->presence_cache,
      tp_base_channel_get_target_handle (base));

  if (presence == NULL)
    {
      DEBUG ("can't find contact's presence");
      g_set_error (error, TP_ERROR, TP_ERROR_OFFLINE,
          "can't find contact's presence");

      return FALSE;
    }

  if (self->priv->service_name != NULL || self->priv->metadata != NULL)
    {
      if (!gabble_presence_has_cap (presence, NS_TP_FT_METADATA))
        {
          DEBUG ("trying to use Metadata properties on a contact "
              "who doesn't support it");
          g_set_error (error, TP_ERROR, TP_ERROR_NOT_CAPABLE,
              "The specified contact does not support the "
              "Metadata extension; you should ensure both ServiceName and "
              "Metadata properties are not present in the channel "
              "request");
          return FALSE;
        }
    }

  contact_repo = tp_base_connection_get_handles (base_conn,
     TP_HANDLE_TYPE_CONTACT);
  room_repo = tp_base_connection_get_handles (base_conn,
     TP_HANDLE_TYPE_ROOM);

  jid = tp_handle_inspect (contact_repo,
      tp_base_channel_get_target_handle (base));
  if (gabble_get_room_handle_from_jid (room_repo, jid) == 0)
    {
      /* Not a MUC jid, need to get a resource */

      /* FIXME: should we check for SI, bytestreams and/or IBB too?
       * http://bugs.freedesktop.org/show_bug.cgi?id=23777 */
      si_resource = gabble_presence_pick_resource_by_caps (presence, 0,
         gabble_capability_set_predicate_has, NS_FILE_TRANSFER);
      si = (si_resource != NULL);

#ifdef ENABLE_JINGLE_FILE_TRANSFER
      share_resource = gabble_presence_pick_resource_by_caps (presence, 0,
          gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_SHARE);
      jingle_share  = (share_resource != NULL);
#endif
    }
  else
    {
      /* MUC jid, we already have the full jid */
      si = gabble_presence_has_cap (presence, NS_FILE_TRANSFER);
#ifdef ENABLE_JINGLE_FILE_TRANSFER
      jingle_share = gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_SHARE);
#endif
    }

  /* Use bytestream if we have SI, but no jingle-share or if we have SI and
     jingle-share but we have no google relay token */
#ifdef ENABLE_JINGLE_FILE_TRANSFER
  use_si = si &&
    (!jingle_share ||
     gabble_jingle_info_get_google_relay_token (
       gabble_jingle_mint_get_info (conn->jingle_mint)) == NULL);
#else
  use_si = si;
#endif

  if (use_si)
    {
      result = offer_bytestream (self, jid, si_resource, error);
    }
#ifdef ENABLE_JINGLE_FILE_TRANSFER
  else if (jingle_share)
    {
      gchar *full_jid = gabble_peer_to_jid (conn,
          tp_base_channel_get_target_handle (base), share_resource);
      result = offer_gtalk_file_transfer (self, full_jid, error);
      g_free (full_jid);
    }
#endif
  else
    {
      DEBUG ("contact doesn't have file transfer capabilities");
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_CAPABLE,
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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
      else if (self->priv->gtalk_file_collection != NULL)
        gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
            self, TRUE);
#endif
    }
}

#ifdef ENABLE_JINGLE_FILE_TRANSFER
void
gabble_file_transfer_channel_gtalk_file_collection_data_received (
    GabbleFileTransferChannel *self, const gchar *data, guint len)
{
  data_received_cb (self, (const guint8 *) data, len);
}
#endif

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
augment_si_reply (WockyNode *si,
                  gpointer user_data)
{
  GabbleFileTransferChannel *self = GABBLE_FILE_TRANSFER_CHANNEL (user_data);
  WockyNode *file;

  file = wocky_node_add_child_with_content (si, "file", NULL);
  file->ns = g_quark_from_string (NS_FILE_TRANSFER);

  if (self->priv->initial_offset != 0)
    {
      WockyNode *range;
      gchar *offset_str;

      range = wocky_node_add_child_with_content (file, "range", NULL);
      offset_str = g_strdup_printf ("%" G_GUINT64_FORMAT,
          self->priv->initial_offset);
      wocky_node_set_attribute (range, "offset", offset_str);

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
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GError *error = NULL;

  if (tp_base_channel_is_requested (base))
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Channel is not an incoming transfer");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (self->priv->state != TP_FILE_TRANSFER_STATE_PENDING)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
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
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
  else if (self->priv->gtalk_file_collection != NULL)
    {
      /* Block the gtalk ft stream while the user is not connected
         to the socket */
      gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
          self, TRUE);
      gtalk_file_collection_accept (self->priv->gtalk_file_collection, self);
    }
#endif
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
  GError *error = NULL;

  if (!tp_base_channel_is_requested (TP_BASE_CHANNEL (self)))
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Channel is not an outgoing transfer");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (self->priv->state != TP_FILE_TRANSFER_STATE_PENDING &&
      self->priv->state != TP_FILE_TRANSFER_STATE_ACCEPTED)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
        "State is not pending or accepted; cannot provide file");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (self->priv->socket_address != NULL)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
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
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
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
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  const gchar *tmp_dir;
  gchar *path = NULL;
  gchar *name;
  struct stat buf;

  tmp_dir = gabble_ft_manager_get_tmp_dir (conn->ft_manager);
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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
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
#endif

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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
      else if (self->priv->gtalk_file_collection != NULL)
        {
          DEBUG ("All the file has been sent.");
          gtalk_file_collection_completed (self->priv->gtalk_file_collection,
              self);
        }
#endif
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

#ifdef ENABLE_JINGLE_FILE_TRANSFER
void
gabble_file_transfer_channel_gtalk_file_collection_write_blocked (
    GabbleFileTransferChannel *self, gboolean blocked)
{
  if (self->priv->transport != NULL)
    gibber_transport_block_receiving (self->priv->transport, blocked);
}
#endif

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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
  else if (self->priv->gtalk_file_collection != NULL)
    gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
        self, FALSE);
#endif
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           GabbleFileTransferChannel *self)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  gboolean requested = tp_base_channel_is_requested (base);

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

#ifdef ENABLE_JINGLE_FILE_TRANSFER
  if (self->priv->gtalk_file_collection != NULL)
    gtalk_file_collection_block_reading (self->priv->gtalk_file_collection,
        self, FALSE);
#endif

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
  TpBaseChannel *base = TP_BASE_CHANNEL (self);

  DEBUG ("Client connected to local socket");

  self->priv->transport = g_object_ref (transport);
  gabble_signal_connect_weak (transport, "disconnected",
    G_CALLBACK (transport_disconnected_cb), G_OBJECT (self));
  gabble_signal_connect_weak (transport, "buffer-empty",
    G_CALLBACK (transport_buffer_empty_cb), G_OBJECT (self));

  if (!tp_base_channel_is_requested (base))
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
      g_array_unref (array);
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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
                                  GTalkFileCollection *gtalk_file_collection,
#else
                                  gpointer gtalk_file_collection_dummy,
#endif
                                  const gchar *file_collection,
                                  const gchar *uri,
                                  const gchar *service_name,
                                  const GHashTable *metadata)

{
#ifndef ENABLE_JINGLE_FILE_TRANSFER
  g_assert (gtalk_file_collection_dummy == NULL);
#endif

  return g_object_new (GABBLE_TYPE_FILE_TRANSFER_CHANNEL,
      "connection", conn,
      "handle", handle,
      "initiator-handle", initiator_handle,
      "requested", (initiator_handle != handle),
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
#ifdef ENABLE_JINGLE_FILE_TRANSFER
      "gtalk-file-collection", gtalk_file_collection,
#endif
      "uri", uri,
      "service-name", service_name,
      "metadata", metadata,
      NULL);
}
