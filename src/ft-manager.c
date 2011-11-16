/*
 * ft-manager.c - Source for GabbleFtManager
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

#define _BSD_SOURCE
#define _XOPEN_SOURCE /* glibc2 needs this */
#include <time.h>
#include <dbus/dbus-glib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#include "jingle-session.h"
#include "jingle-share.h"
#include "gabble/caps-channel-manager.h"
#include "connection.h"
#include "ft-manager.h"
#include "ft-channel.h"
#include "error.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "util.h"

#include <wocky/wocky-data-form.h>

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_FT
#include "debug.h"

static void
channel_manager_iface_init (gpointer, gpointer);

static void gabble_ft_manager_channel_created (GabbleFtManager *mgr,
    GabbleFileTransferChannel *chan, gpointer request_token);


static void caps_channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleFtManager, gabble_ft_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      caps_channel_manager_iface_init));

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
  /* path of the temporary directory used to store UNIX sockets */
  gchar *tmp_dir;
  gulong status_changed_id;
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

static void gabble_ft_manager_constructed (GObject *object);
static void gabble_ft_manager_dispose (GObject *object);
static void gabble_ft_manager_finalize (GObject *object);
static void connection_status_changed_cb (GabbleConnection *conn,
                                          guint status,
                                          guint reason,
                                          GabbleFtManager *self);

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

  object_class->constructed = gabble_ft_manager_constructed;
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


static void
gabble_ft_manager_constructed (GObject *object)
{
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (gabble_ft_manager_parent_class)->constructed;
  GabbleFtManager *self = GABBLE_FT_MANAGER (object);

  if (chain_up != NULL)
    chain_up (object);

  self->priv->status_changed_id = g_signal_connect (self->priv->connection,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}

static void
ft_manager_close_all (GabbleFtManager *self)
{
  GList *l;

  while ((l = self->priv->channels) != NULL)
    {
      gabble_file_transfer_channel_do_close (l->data);
      /* Channels should have closed and disappeared from the list */
      g_assert (l != self->priv->channels);
    }
}


void
gabble_ft_manager_dispose (GObject *object)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (object);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  g_assert (self->priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_ft_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_ft_manager_parent_class)->dispose (object);
}

void
gabble_ft_manager_finalize (GObject *object)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (object);

  if (self->priv->tmp_dir != NULL)
    {
      if (g_rmdir (self->priv->tmp_dir) != 0)
        {
          DEBUG ("rmdir failed: %s", g_strerror (errno));
        }
    }
  g_free (self->priv->tmp_dir);

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
file_channel_closed_cb (GabbleFileTransferChannel *chan,
                        gpointer user_data)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (user_data);

  if (self->priv->channels != NULL)
    {
      gchar *path, *id;

      g_object_get (chan,
          "target-id", &id,
          "object-path", &path,
          NULL);

      DEBUG ("Removing channel %s with %s", path, id);
      self->priv->channels = g_list_remove (self->priv->channels, chan);
      g_object_unref (chan);
      g_free (id);
      g_free (path);
    }
}

static void
gabble_ft_manager_channels_created (GabbleFtManager *self, GList *channels)
{
  GList *i;
  GHashTable *new_channels = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);

  for (i = channels; i ; i = i->next)
    {
      GabbleFileTransferChannel *chan = i->data;

      gabble_signal_connect_weak (chan, "closed",
          G_CALLBACK (file_channel_closed_cb), G_OBJECT (self));

      self->priv->channels = g_list_append (self->priv->channels, chan);
      /* The channels can't satisfy a request because this will always be called
         when we receive an incoming jingle-share session */
      g_hash_table_insert (new_channels, chan, NULL);
    }

  tp_channel_manager_emit_new_channels (self, new_channels);

  g_hash_table_unref (new_channels);
}

static void
gabble_ft_manager_channel_created (GabbleFtManager *self,
                                   GabbleFileTransferChannel *chan,
                                   gpointer request_token)
{
  GSList *requests = NULL;

  gabble_signal_connect_weak (chan, "closed",
      G_CALLBACK (file_channel_closed_cb), G_OBJECT (self));

  self->priv->channels = g_list_append (self->priv->channels, chan);

  if (request_token != NULL)
    requests = g_slist_prepend (requests, request_token);

  tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
      requests);

  g_slist_free (requests);
}


static void
new_jingle_session_cb (GabbleJingleFactory *jf,
    GabbleJingleSession *sess,
    gpointer data)
{
  GabbleFtManager *self = GABBLE_FT_MANAGER (data);
  GTalkFileCollection *gtalk_fc = NULL;
  GabbleJingleContent *content = NULL;
  GabbleJingleShareManifest *manifest = NULL;
  GList *channels = NULL;
  GList *cs, *i;

  if (gabble_jingle_session_get_content_type (sess) ==
      GABBLE_TYPE_JINGLE_SHARE)
    {
      cs = gabble_jingle_session_get_contents (sess);

      if (cs != NULL)
        {
          content = GABBLE_JINGLE_CONTENT (cs->data);
          g_list_free (cs);
        }

      if (content == NULL)
          return;

      gtalk_fc = gtalk_file_collection_new_from_session (jf, sess);

      if (gtalk_fc)
        {
          gchar *token = NULL;

          g_object_get (gtalk_fc,
              "token", &token,
              NULL);

          manifest = gabble_jingle_share_get_manifest (
              GABBLE_JINGLE_SHARE (content));
          for (i = manifest->entries; i; i = i->next)
            {
              GabbleJingleShareManifestEntry *entry = i->data;
              GabbleFileTransferChannel *channel = NULL;
              gchar *filename = NULL;

              filename = g_strdup_printf ("%s%s",
                  entry->name, entry->folder? ".tar":"");
              channel = gabble_file_transfer_channel_new (self->priv->connection,
                  sess->peer, sess->peer, TP_FILE_TRANSFER_STATE_PENDING,
                  NULL, filename, entry->size, TP_FILE_HASH_TYPE_NONE, NULL,
                  NULL, 0, 0, FALSE, NULL, gtalk_fc, token, NULL, NULL, NULL);
              g_free (filename);

              gtalk_file_collection_add_channel (gtalk_fc, channel);
              channels = g_list_prepend (channels, channel);
            }

          if (channels != NULL)
            gabble_ft_manager_channels_created (self, channels);

          g_list_free (channels);

          /* Channels will hold the reference to the gtalk file collection,
             so we can drop ours already. If no channels were created,
             then we need to destroy it anyways */
          g_object_unref (gtalk_fc);
        }
    }
}


static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleFtManager *self)
{

  switch (status)
    {
      case TP_CONNECTION_STATUS_CONNECTING:
        g_signal_connect (self->priv->connection->jingle_factory,
            "new-session",
            G_CALLBACK (new_jingle_session_cb), self);
        break;

      case TP_CONNECTION_STATUS_DISCONNECTED:
        ft_manager_close_all (self);
        if (self->priv->status_changed_id != 0)
          {
            g_signal_handler_disconnect (self->priv->connection,
                self->priv->status_changed_id);
            self->priv->status_changed_id = 0;
          }
        break;
    }
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
  const gchar *file_uri, *service_name;
  guint64 size, date, initial_offset;
  TpFileHashType content_hash_type;
  const GHashTable *metadata;
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

  file_uri = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_URI);

  service_name = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME);

  metadata = tp_asv_get_boxed (request_properties,
      TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_METADATA,
      TP_HASH_TYPE_METADATA);

  if (metadata != NULL && g_hash_table_lookup ((GHashTable *) metadata, "FORM_TYPE"))
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Metadata cannot contain an item with key 'FORM_TYPE'");
      goto error;
    }

  DEBUG ("Requested outgoing channel with contact: %s",
      tp_handle_inspect (contact_repo, handle));

  chan = gabble_file_transfer_channel_new (self->priv->connection,
      handle, base_connection->self_handle, TP_FILE_TRANSFER_STATE_PENDING,
      content_type, filename, size, content_hash_type, content_hash,
      description, date, initial_offset, TRUE, NULL, NULL, NULL, file_uri,
      service_name, metadata);

  if (!gabble_file_transfer_channel_offer_file (chan, &error))
    {
      g_object_unref (chan);
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

/* Keep in sync with values set in gabble_ft_manager_type_foreach_channel_class
 */
static const gchar * const file_transfer_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

   /* ContentHashType has to be first so we can easily skip it when needed */
#define STANDARD_PROPERTIES \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_CONTENT_HASH_TYPE, \
   TP_PROP_CHANNEL_TARGET_HANDLE, \
   TP_PROP_CHANNEL_TARGET_ID, \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_CONTENT_TYPE, \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_FILENAME, \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_SIZE, \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_CONTENT_HASH, \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_DESCRIPTION, \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_DATE, \
   TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_URI

static const gchar * const file_transfer_channel_allowed_properties[] =
{
  STANDARD_PROPERTIES,
  NULL
};

static const gchar * const file_transfer_channel_allowed_properties_with_metadata_prop[] =
{
  STANDARD_PROPERTIES,
  TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_METADATA,
  NULL
};

static const gchar * const file_transfer_channel_allowed_properties_with_both_metadata_props[] =
{
  STANDARD_PROPERTIES,
  TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME,
  TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_METADATA,
  NULL
};

static void
gabble_ft_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table;

  /* general FT class */
  table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType" ,
      tp_g_value_slice_new_string (TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER));

  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      tp_g_value_slice_new_uint (TP_HANDLE_TYPE_CONTACT));

  func (type, table, file_transfer_channel_allowed_properties_with_both_metadata_props,
      user_data);

  /* MD5 HashType class */
  g_hash_table_insert (table,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHashType",
      tp_g_value_slice_new_uint (TP_FILE_HASH_TYPE_MD5));

  /* skip ContentHashType in allowed properties */
  func (type, table, file_transfer_channel_allowed_properties_with_both_metadata_props + 1,
      user_data);

  g_hash_table_unref (table);
}

static LmMessageNode *
hyvaa_vappua (
    LmMessageNode *si_node,
    const gchar **filename,
    const gchar **size_str,
    GError **error)
{
#define die_if_null(var, msg) \
  if ((var) == NULL) \
    { \
      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, msg); \
      return NULL; \
    }

  LmMessageNode *file_node = lm_message_node_get_child_with_namespace (si_node,
      "file", NS_FILE_TRANSFER);

  die_if_null (file_node, "Invalid file transfer SI request: no <file>")
  die_if_null (*filename = lm_message_node_get_attribute (file_node, "name"),
      "Invalid file transfer SI request: missing file name")
  die_if_null (*size_str = lm_message_node_get_attribute (file_node, "size"),
      "Invalid file transfer SI request: missing file size")

  return file_node;
#undef die_if_null
}

static WockyDataForm *
find_data_form (WockyNode *file,
    const gchar *form_type)
{
  WockyNodeIter iter;
  WockyNode *x;

  wocky_node_iter_init (&iter, file, "x", NS_X_DATA);
  while (wocky_node_iter_next (&iter, &x))
    {
      GError *error = NULL;
      WockyDataForm *form = wocky_data_form_new_from_node (x, &error);
      WockyDataFormField *field;

      if (form == NULL)
        {
          DEBUG ("Failed to parse data form: %s", error->message);
          g_clear_error (&error);
          continue;
        }

      field = g_hash_table_lookup (form->fields, "FORM_TYPE");

      if (field == NULL)
        {
          DEBUG ("Data form doesn't have FORM_TYPE field!");
          g_object_unref (form);
          continue;
        }

      /* found it! */
      if (!tp_strdiff (field->raw_value_contents[0], form_type))
        return form;

      g_object_unref (form);
    }

  return NULL;
}

static gchar *
extract_service_name (WockyNode *file)
{
  WockyDataForm *form = find_data_form (file, NS_TP_FT_METADATA_SERVICE);
  WockyDataFormField *field;
  gchar *service_name = NULL;

  if (form == NULL)
    return NULL;

  field = g_hash_table_lookup (form->fields, "ServiceName");

  if (field == NULL)
    {
      DEBUG ("ServiceName propery not present in data form; odd...");
      goto out;
    }

  if (field->raw_value_contents == NULL
      || field->raw_value_contents[0] == NULL)
    {
      DEBUG ("ServiceName property doesn't have a real value; odd...");
    }
  else
    {
      service_name = g_strdup (field->raw_value_contents[0]);
    }

out:
  g_object_unref (form);
  return service_name;
}

static GHashTable *
extract_metadata (WockyNode *file)
{
  WockyDataForm *form = find_data_form (file, NS_TP_FT_METADATA);
  GHashTable *metadata;
  GHashTableIter iter;
  gpointer key, value;

  if (form == NULL)
    return NULL;

  metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_strfreev);

  g_hash_table_iter_init (&iter, form->fields);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *var = key;
      WockyDataFormField *field = value;

      if (!tp_strdiff (var, "FORM_TYPE"))
        continue;

      g_hash_table_insert (metadata,
          g_strdup (var),
          g_strdupv (field->raw_value_contents));
    }

  g_object_unref (form);
  return metadata;
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
  gchar *service_name;
  GHashTable *metadata;
  guint64 size;
  guint64 date = 0;
  TpFileHashType content_hash_type;
  GabbleFileTransferChannel *chan;
  gboolean resume_supported;
  GError *error = NULL;

  si_node = lm_message_node_get_child_with_namespace (
      wocky_stanza_get_top_node (msg), "si", NS_SI);
  g_assert (si_node != NULL);

  file_node = hyvaa_vappua (si_node, &filename, &size_str, &error);

  if (file_node == NULL)
    {
      DEBUG ("%s", error->message);
      gabble_bytestream_iface_close (bytestream, error);
      g_clear_error (&error);
      return;
    }

  size = g_ascii_strtoull (size_str, NULL, 0);

  content_type = lm_message_node_get_attribute (file_node, "mime-type");
  if (content_type == NULL)
    content_type = "application/octet-stream";

  /* The hash is always an MD5-sum, if present. */
  content_hash = lm_message_node_get_attribute (file_node, "hash");
  if (content_hash != NULL)
    content_hash_type = TP_FILE_HASH_TYPE_MD5;
  else
    content_hash_type = TP_FILE_HASH_TYPE_NONE;

  desc_node = lm_message_node_get_child (file_node, "desc");
  if (desc_node != NULL)
    description = lm_message_node_get_value (desc_node);
  else
    description = NULL;

  date_str = lm_message_node_get_attribute (file_node, "date");
  if (date_str != NULL)
    {
      GTimeVal val;

      /* FIXME: this assume the timezone is always UTC */
      if (g_time_val_from_iso8601 (date_str, &val))
        date = val.tv_sec;
    }

  resume_supported = (lm_message_node_get_child (file_node, "range") != NULL);

  /* metadata */
  service_name = extract_service_name (file_node);
  metadata = extract_metadata (file_node);

  chan = gabble_file_transfer_channel_new (self->priv->connection,
      handle, handle, TP_FILE_TRANSFER_STATE_PENDING,
      content_type, filename, size, content_hash_type, content_hash,
      description, date, 0, resume_supported, bytestream, NULL, NULL, NULL,
      service_name, metadata);

  gabble_ft_manager_channel_created (self, chan, NULL);

  g_free (service_name);
  if (metadata != NULL)
    g_hash_table_unref (metadata);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_ft_manager_foreach_channel;
  iface->type_foreach_channel_class =
      gabble_ft_manager_type_foreach_channel_class;
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

#ifdef G_OS_UNIX
/* We assume that all Unixes have mkdtemp */
const gchar *
gabble_ft_manager_get_tmp_dir (GabbleFtManager *self)
{
  if (self->priv->tmp_dir != NULL)
    return self->priv->tmp_dir;

  self->priv->tmp_dir = g_strdup_printf ("%s/gabble-ft-XXXXXX",
      g_get_tmp_dir ());
  self->priv->tmp_dir = mkdtemp (self->priv->tmp_dir);
  if (self->priv->tmp_dir == NULL)
    DEBUG ("mkdtemp failed: %s\n", g_strerror (errno));

  return self->priv->tmp_dir;
}
#endif

static void
add_file_transfer_channel_class (GPtrArray *arr,
    gboolean include_metadata_properties,
    const gchar *service_name_str)
{
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;
  GValue *service_name_value;
  const gchar * const *allowed_properties;

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new_static_string (
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new_uint (TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".TargetHandleType",
      target_handle_type_value);

  if (service_name_str != NULL)
    {
      service_name_value = tp_g_value_slice_new_string (service_name_str);
      g_hash_table_insert (fixed_properties,
          TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME,
          service_name_value);
    }

  if (include_metadata_properties)
    {
      if (service_name_str == NULL)
        allowed_properties = file_transfer_channel_allowed_properties_with_both_metadata_props;
      else
        allowed_properties = file_transfer_channel_allowed_properties_with_metadata_prop;
    }
  else
    {
      allowed_properties = file_transfer_channel_allowed_properties;
    }

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

static void
get_contact_caps_foreach (gpointer data,
    gpointer user_data)
{
  const gchar *ns = data;
  GPtrArray *arr = user_data;

  if (!g_str_has_prefix (ns, NS_TP_FT_METADATA "#"))
    return;

  add_file_transfer_channel_class (arr, TRUE,
      ns + strlen (NS_TP_FT_METADATA "#"));
}

static void
gabble_ft_manager_get_contact_caps (
    GabbleCapsChannelManager *manager G_GNUC_UNUSED,
    TpHandle handle G_GNUC_UNUSED,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  if (gabble_capability_set_has (caps, NS_FILE_TRANSFER) ||
      gabble_capability_set_has (caps, NS_GOOGLE_FEAT_SHARE))
    {
      add_file_transfer_channel_class (arr,
          gabble_capability_set_has (caps, NS_TP_FT_METADATA), NULL);
    }

  gabble_capability_set_foreach (caps, get_contact_caps_foreach, arr);
}

static void
gabble_ft_manager_represent_client (
    GabbleCapsChannelManager *manager G_GNUC_UNUSED,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens G_GNUC_UNUSED,
    GabbleCapabilitySet *cap_set,
    GPtrArray *data_forms G_GNUC_UNUSED)
{
  guint i;

  for (i = 0; i < filters->len; i++)
    {
      GHashTable *channel_class = g_ptr_array_index (filters, i);
      const gchar *service_name;
      gchar *ns;

      if (tp_strdiff (tp_asv_get_string (channel_class,
              TP_IFACE_CHANNEL ".ChannelType"),
            TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER))
        continue;

      if (tp_asv_get_uint32 (channel_class,
            TP_IFACE_CHANNEL ".TargetHandleType", NULL)
          != TP_HANDLE_TYPE_CONTACT)
        continue;

      DEBUG ("client %s supports file transfer", client_name);
      gabble_capability_set_add (cap_set, NS_FILE_TRANSFER);
      gabble_capability_set_add (cap_set, NS_GOOGLE_FEAT_SHARE);
      gabble_capability_set_add (cap_set, NS_TP_FT_METADATA);

      /* now look at service names */

      /* capabilities mean being able to RECEIVE said kinds of
       * FTs. hence, skip Requested=true (locally initiated) channel
       * classes */
      if (tp_asv_get_boolean (channel_class,
              TP_PROP_CHANNEL_REQUESTED, FALSE))
        continue;

      service_name = tp_asv_get_string (channel_class,
          TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME);

      if (service_name == NULL)
        continue;

      ns = g_strconcat (NS_TP_FT_METADATA "#", service_name, NULL);

      DEBUG ("%s: adding capability %s", client_name, ns);
      gabble_capability_set_add (cap_set, ns);
      g_free (ns);
    }
}

static void
caps_channel_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  GabbleCapsChannelManagerInterface *iface = g_iface;

  iface->get_contact_caps = gabble_ft_manager_get_contact_caps;
  iface->represent_client = gabble_ft_manager_represent_client;
}
