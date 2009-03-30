/*
 * ft-manager.c - Source for GabbleFtManager
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

#define _XOPEN_SOURCE /* glibc2 needs this */
#include <time.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "caps-channel-manager.h"
#include "connection.h"
#include "ft-manager.h"
#include "error.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

#include "file-transfer-channel.h"

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_FT
#include "debug.h"

static void
channel_manager_iface_init (gpointer, gpointer);

static void gabble_ft_manager_channel_created (GabbleFtManager *mgr,
    GabbleFileTransferChannel *chan, gpointer request_token);

G_DEFINE_TYPE_WITH_CODE (GabbleFtManager, gabble_ft_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

/* private structure */
struct _GabbleFtManagerPrivate
{
  gboolean dispose_has_run;
  GabbleConnection *connection;
  GList *channels;
};

static void
gabble_ft_manager_init (GabbleFtManager *obj)
{
  obj->priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      GABBLE_TYPE_FT_MANAGER, GabbleFtManagerPrivate);
  obj->priv->connection = NULL;

  /* allocate any data required by the object here */
  obj->priv->channels = NULL;
}

static void gabble_ft_manager_dispose (GObject *object);
static void gabble_ft_manager_finalize (GObject *object);

static void
gabble_ft_manager_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_ft_manager_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        self->priv->connection = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_ft_manager_class_init (GabbleFtManagerClass *gabble_ft_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_ft_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_ft_manager_class,
                            sizeof (GabbleFtManagerPrivate));

  object_class->get_property = gabble_ft_manager_get_property;
  object_class->set_property = gabble_ft_manager_set_property;

  object_class->dispose = gabble_ft_manager_dispose;
  object_class->finalize = gabble_ft_manager_finalize;

  param_spec = g_param_spec_object ("connection",
      "GabbleConnection object",
      "Gabble Connection that owns the connection for this FT manager",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

void
gabble_ft_manager_dispose (GObject *object)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (object);
  GList *l;

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  for (l = self->priv->channels; l != NULL; l = g_list_next (l))
    {
      g_signal_handlers_disconnect_matched (l->data,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      g_object_unref (l->data);
    }

  if (self->priv->channels)
    g_list_free (self->priv->channels);

  if (G_OBJECT_CLASS (gabble_ft_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_ft_manager_parent_class)->dispose (object);
}

void
gabble_ft_manager_finalize (GObject *object)
{
  /*GabbleFtManager *self = GABBLE_FT_MANAGER (object);*/

  G_OBJECT_CLASS (gabble_ft_manager_parent_class)->finalize (object);
}

/* Channel Manager interface */

struct foreach_data {
  TpExportableChannelFunc func;
  gpointer data;
};

static void
gabble_ft_manager_iface_foreach_one (gpointer value,
                                     gpointer data)
{
  TpExportableChannel *chan;
  struct foreach_data *f = (struct foreach_data *) data;

  chan = TP_EXPORTABLE_CHANNEL (value);

  f->func (chan, f->data);
}

static void
gabble_ft_manager_foreach_channel (TpChannelManager *iface,
                                  TpExportableChannelFunc func,
                                  gpointer data)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (iface);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_list_foreach (self->priv->channels,
      (GFunc) gabble_ft_manager_iface_foreach_one, &f);
}

static void
file_channel_closed (GabbleFtManager *self,
                     GabbleFileTransferChannel *chan)
{
  TpHandle handle;

  if (self->priv->channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %d", handle);
      self->priv->channels = g_list_remove (self->priv->channels, chan);
      g_object_unref (chan);
    }
}

static void
file_channel_closed_cb (GabbleFileTransferChannel *chan,
                        gpointer user_data)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (user_data);

  file_channel_closed (self, chan);
}

static void
gabble_ft_manager_channel_created (GabbleFtManager *self,
                                   GabbleFileTransferChannel *chan,
                                   gpointer request_token)
{
  GSList *requests = NULL;

  g_signal_connect (chan, "closed", G_CALLBACK (file_channel_closed_cb), self);

  self->priv->channels = g_list_append (self->priv->channels, chan);

  if (request_token != NULL)
    requests = g_slist_prepend (requests, request_token);

  tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
      requests);

  g_slist_free (requests);
}

static gboolean
gabble_ft_manager_handle_request (TpChannelManager *manager,
                                 gpointer request_token,
                                 GHashTable *request_properties)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (manager);
  GabbleFileTransferChannel *chan;
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (
      self->priv->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  const gchar *content_type, *filename, *content_hash, *description;
  guint64 size, date, initial_offset;
  TpFileHashType content_hash_type;
  GError *error = NULL;
  gboolean valid;

  DEBUG ("File transfer request");

  /* We only support file transfer channels */
  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER))
    return FALSE;

  /* And only contact handles */
  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  /* Must be a valid contact handle */
  if (!tp_handle_is_valid (contact_repo, handle, &error))
    goto error;

  /* Don't support opening a channel to our self handle */
  if (handle == base_connection->self_handle)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Can't open a file transfer channel to yourself");
      goto error;
    }

  content_type = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentType");
  if (content_type == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "ContentType property is mandatory");
      goto error;
    }

  filename = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename");
  if (filename == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Filename property is mandatory");
      goto error;
    }

  size = tp_asv_get_uint64 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size", NULL);
  if (size == 0)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Size property is mandatory");
      goto error;
    }

  content_hash_type = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHashType", &valid);
  if (!valid)
    {
      /* Assume File_Hash_Type_None */
      content_hash_type = TP_FILE_HASH_TYPE_NONE;
    }
  else
    {
      if (content_hash_type >= NUM_TP_FILE_HASH_TYPES)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "%u is not a valid ContentHashType", content_hash_type);
          goto error;
        }
    }

  if (content_hash_type != TP_FILE_HASH_TYPE_NONE)
    {
      content_hash = tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHash");
      if (content_hash == NULL)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "ContentHash property is mandatory if ContentHashType is "
              "not None");
          goto error;
        }
    }
  else
    {
      content_hash = NULL;
    }

  description = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Description");

  date = tp_asv_get_uint64 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Date", NULL);

  initial_offset = tp_asv_get_uint64 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".InitialOffset", NULL);

  DEBUG ("Requested outgoing channel with contact: %s",
      tp_handle_inspect (contact_repo, handle));

  chan = gabble_file_transfer_channel_new (self->priv->connection,
      handle, base_connection->self_handle, TP_FILE_TRANSFER_STATE_PENDING,
      content_type, filename, size, content_hash_type, content_hash,
      description, date, initial_offset, NULL);

  if (!gabble_file_transfer_channel_offer_file (chan, &error))
    {
      /* Pretend the chan was closed so it's removed from the channels
       * list and unreffed. */
      file_channel_closed (self, chan);
      goto error;
    }

  gabble_ft_manager_channel_created (self, chan, request_token);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

/* Keep in sync with values set in gabble_ft_manager_foreach_channel_class */
static const gchar * const file_transfer_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const file_transfer_channel_allowed_properties[] =
{
   TP_IFACE_CHANNEL ".TargetHandle",
   TP_IFACE_CHANNEL ".TargetID",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentType",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHashType",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHash",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Description",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Date",
   NULL
};

static void
gabble_ft_manager_foreach_channel_class (TpChannelManager *manager,
                                         TpChannelManagerChannelClassFunc func,
                                         gpointer user_data)
{
  GHashTable *table;
  GValue *value;

  table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType" , value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType", value);

  func (manager, table, file_transfer_channel_allowed_properties,
      user_data);

  g_hash_table_destroy (table);
}

void gabble_ft_manager_handle_si_request (GabbleFtManager *self,
                                          GabbleBytestreamIface *bytestream,
                                          TpHandle handle,
                                          const gchar *stream_id,
                                          LmMessage *msg)
{
  LmMessageNode *si_node, *file_node, *desc_node;
  const gchar *filename, *size_str, *content_type, *content_hash, *description;
  const gchar *date_str;
  guint64 size;
  guint64 date = 0;
  TpFileHashType content_hash_type;
  GabbleFileTransferChannel *chan;

  si_node = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  g_assert (si_node != NULL);

  file_node = lm_message_node_get_child_with_namespace (si_node, "file",
      NS_FILE_TRANSFER);
  if (file_node == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "Invalid file transfer SI request: no <file>" };

      DEBUG ("%s", e.message);
      gabble_bytestream_iface_close (bytestream, &e);
    }

  filename = lm_message_node_get_attribute (file_node, "name");
  if (filename == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "Invalid file transfer SI request: missing file name" };

      DEBUG ("%s", e.message);
      gabble_bytestream_iface_close (bytestream, &e);
    }

  size_str = lm_message_node_get_attribute (file_node, "size");
  if (size_str == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "Invalid file transfer SI request: missing file size" };

      DEBUG ("%s", e.message);
      gabble_bytestream_iface_close (bytestream, &e);
    }
  size = g_ascii_strtoull (size_str, NULL, 0);

  content_type = lm_message_node_get_attribute (file_node, "mime-type");
  if (content_type == NULL)
    {
      content_type = "application/octet-stream";
    }

  content_hash = lm_message_node_get_attribute (file_node, "hash");
  if (content_hash != NULL)
    content_hash_type = TP_FILE_HASH_TYPE_MD5;
  else
    content_hash_type = TP_FILE_HASH_TYPE_NONE;

  desc_node = lm_message_node_get_child (file_node, "desc");
  if (desc_node != NULL)
    description = desc_node->value;
  else
    description = NULL;

  date_str = lm_message_node_get_attribute (file_node, "date");
  if (date_str != NULL)
    {
      struct tm tm;

      /* FIXME: this assume the timezone is always UTC */
      if (strptime (date_str, "%FT%H:%M:%SZ", &tm) != NULL)
        date = (guint64) mktime (&tm);
    }

  /* TODO: initial offset */
  chan = gabble_file_transfer_channel_new (self->priv->connection,
      handle, handle, TP_FILE_TRANSFER_STATE_PENDING,
      content_type, filename, size, content_hash_type, content_hash,
      description, date, 0, bytestream);

  gabble_ft_manager_channel_created (self, chan, NULL);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_ft_manager_foreach_channel;
  iface->foreach_channel_class = gabble_ft_manager_foreach_channel_class;
  iface->request_channel = gabble_ft_manager_handle_request;
  iface->create_channel = gabble_ft_manager_handle_request;
  iface->ensure_channel = gabble_ft_manager_handle_request;
}

/* public functions */
GabbleFtManager *
gabble_ft_manager_new (GabbleConnection *connection)
{
  g_assert (connection != NULL);

  return g_object_new (GABBLE_TYPE_FT_MANAGER,
      "connection", connection,
      NULL);
}
