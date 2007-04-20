/*
 * tubes-channel.c - Source for GabbleTubesChannel
 * Copyright (C) 2007 Collabora Ltd.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "debug.h"
#include "gabble-connection.h"
#include "presence.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "util.h"
#include "base64.h"
#include "tube-dbus.h"
#include "bytestream-factory.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-properties-interface.h>

#include "tubes-channel.h"

#define TP_CHANNEL_TUBE_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
        G_TYPE_UINT, \
        G_TYPE_UINT, \
        G_TYPE_UINT, \
        G_TYPE_STRING, \
        dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), \
        G_TYPE_UINT, \
        G_TYPE_INVALID))

#define DBUS_NAME_PAIR_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

static void channel_iface_init (gpointer, gpointer);
static void tubes_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleTubesChannel, gabble_tubes_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TUBES, tubes_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_SELF_HANDLE,
  LAST_PROPERTY,
};


/* private structure */
typedef struct _GabbleTubesChannelPrivate GabbleTubesChannelPrivate;

struct _GabbleTubesChannelPrivate
{
  GabbleConnection *conn;
  char *object_path;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandle self_handle;

  GHashTable *tubes;
  GHashTable *stream_id_to_tube_id;
  guint next_tube_id;

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_TUBES_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleTubesChannelPrivate *) obj->priv)

static void update_tubes_presence (GabbleTubesChannel *self);

static void
free_gvalue (GValue *value)
{
  g_value_unset (value);
  g_slice_free (GValue, value);
}

static void
gabble_tubes_channel_init (GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBES_CHANNEL, GabbleTubesChannelPrivate);

  self->priv = priv;

  priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);

  priv->stream_id_to_tube_id = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);

  priv->next_tube_id = 1;

  priv->dispose_has_run = FALSE;
  priv->closed = FALSE;
}

static GObject *
gabble_tubes_channel_constructor (GType type,
                                  guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleTubesChannelPrivate *priv;
  DBusGConnection *bus;

  DEBUG ("Called");

  obj = G_OBJECT_CLASS (gabble_tubes_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (GABBLE_TUBES_CHANNEL (obj));

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  DEBUG ("Registering at '%s'", priv->object_path);

  return obj;
}

static void
gabble_tubes_channel_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleTubesChannel *chan = GABBLE_TUBES_CHANNEL (object);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (chan);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TUBES);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, priv->handle_type);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_SELF_HANDLE:
        g_value_set_uint (value, priv->self_handle);
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_tubes_channel_set_property (GObject     *object,
                                   guint        property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleTubesChannel *chan = GABBLE_TUBES_CHANNEL (object);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (chan);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        DEBUG ("Setting object_path: %s", priv->object_path);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_HANDLE_TYPE:
        priv->handle_type = g_value_get_uint (value);
        break;
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_SELF_HANDLE:
        priv->self_handle = g_value_get_uint (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GHashTable *
extract_parameters (LmMessageNode *params_node)
{
  /* XXX use the code defined in the OLPC branch */
  GHashTable *parameters;

  parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) free_gvalue);

  if (params_node)
    {
      LmMessageNode *node;
      for (node = params_node->children; node; node = node->next)
        {
          const gchar *name;
          const gchar *type;
          const gchar *value;
          GValue *gvalue;

          if (tp_strdiff (node->name, "parameter"))
            continue;

          name = lm_message_node_get_attribute (node, "name");

          if (!name)
            continue;

          type = lm_message_node_get_attribute (node, "type");
          value = lm_message_node_get_value (node);

          if (type == NULL || 0 == strcmp (type, "bytes"))
            {
              GArray *arr;
              GString *decoded;

              decoded = base64_decode (value);

              if (!decoded)
                continue;

              arr = g_array_new (FALSE, FALSE, sizeof (guchar));
              g_array_append_vals (arr, decoded->str, decoded->len);
              gvalue = g_slice_new0 (GValue);
              g_value_init (gvalue, DBUS_TYPE_G_UCHAR_ARRAY);
              g_value_set_boxed (gvalue, arr);
              g_hash_table_insert (parameters, g_strdup (name), gvalue);
            }
          else if (0 == strcmp (type, "str"))
            {
              gvalue = g_slice_new0 (GValue);
              g_value_init (gvalue, G_TYPE_STRING);
              g_value_set_string (gvalue, value);
              g_hash_table_insert (parameters, g_strdup (name), gvalue);
            }
          else if (0 == strcmp (type, "int"))
            {
              gvalue = g_slice_new0 (GValue);
              g_value_init (gvalue, G_TYPE_INT);
              g_value_set_int (gvalue, atoi (value));
              g_hash_table_insert (parameters, g_strdup (name), gvalue);
            }
          else if (0 == strcmp (type, "uint"))
            {
              gvalue = g_slice_new0 (GValue);
              g_value_init (gvalue, G_TYPE_UINT);
              g_value_set_int (gvalue, atoi (value));
              g_hash_table_insert (parameters, g_strdup (name), gvalue);
            }
        }
    }

  return parameters;
}

static void
add_yourself_in_dbus_names (GabbleTubesChannel *self,
                            GabbleTubeDBus *tube)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GHashTable *names;
  gchar *name;

  g_object_get (tube,
      "dbus-name", &name,
      "dbus-names", &names,
      NULL);
  g_hash_table_insert (names, GUINT_TO_POINTER (priv->self_handle), name);
  g_hash_table_unref (names);
  tp_handle_ref (contact_repo, priv->self_handle);
}

static guint
create_new_tube (GabbleTubesChannel *self,
                 TpTubeType type,
                 TpHandle initiator,
                 const gchar* service,
                 GHashTable *parameters,
                 TpTubeState state,
                 const gchar *stream_id,
                 GabbleBytestreamIBB *bytestream)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeDBus *tube;
  guint tube_id;
  GType gtype;

  switch (type)
    {
    case TP_TUBE_TYPE_DBUS:
      gtype = GABBLE_TYPE_TUBE_DBUS;
      break;
    default:
      g_assert_not_reached ();
    }

  tube_id = priv->next_tube_id++;

  tube = g_object_new (gtype,
                       "connection", priv->conn,
                       "initiator", initiator,
                       "service", service,
                       "parameters", parameters,
                       NULL);

  if (bytestream != NULL)
    {
      g_object_set (tube, "bytestream", bytestream, NULL);
    }

  g_object_set (G_OBJECT (tube), "state", state, NULL);

  g_hash_table_insert (priv->tubes, GUINT_TO_POINTER (tube_id), tube);
  g_hash_table_insert (priv->stream_id_to_tube_id, g_strdup (stream_id),
      GUINT_TO_POINTER (tube_id));

  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    {
      update_tubes_presence (self);
    }

  tp_svc_channel_type_tubes_emit_new_tube (self,
      tube_id,
      initiator,
      type,
      service,
      parameters,
      state);

  if (type == TP_TUBE_TYPE_DBUS &&
      state != TP_TUBE_STATE_LOCAL_PENDING)
    {
      add_yourself_in_dbus_names (self, tube);
    }

  return tube_id;
}

static gboolean
extract_tube_information (GabbleTubesChannel *self,
                          LmMessageNode *tube_node,
                          TpTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (type != NULL)
    {
      const gchar *_type;

      _type = lm_message_node_get_attribute (tube_node, "type");

      if (!tp_strdiff (_type, "dbus"))
        *type = TP_TUBE_TYPE_DBUS;
      else
        {
          DEBUG ("Unknow tube type: %s", _type);
          return FALSE;
        }
    }

  if (initiator_handle != NULL)
    {
      const gchar *initiator;

      initiator = lm_message_node_get_attribute (tube_node, "initiator");
      *initiator_handle = tp_handle_lookup (contact_repo, initiator, NULL,
          NULL);

      if (!tp_handle_is_valid (contact_repo, *initiator_handle, NULL))
        {
          DEBUG ("invalid initiator handle");
          return FALSE;
        }
    }

  if (service != NULL)
    {
      *service = lm_message_node_get_attribute (tube_node, "service");
    }

  if (parameters != NULL)
    {
      LmMessageNode *node;

      node = lm_message_node_get_child (tube_node, "parameters");
      *parameters = extract_parameters (node);
    }

  return TRUE;
}

static void
d_bus_names_changed_added (GabbleTubesChannel *self,
                           guint tube_id,
                           TpHandle contact,
                           const gchar *new_name)
{
  GPtrArray *added = g_ptr_array_sized_new (1);
  GArray *removed = g_array_new (FALSE, FALSE, sizeof (guint));
  GValue tmp = {0,};
  guint i;

  g_value_init (&tmp, DBUS_NAME_PAIR_TYPE);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (DBUS_NAME_PAIR_TYPE));
  dbus_g_type_struct_set (&tmp,
      0, contact,
      1, new_name,
      G_MAXUINT);
  g_ptr_array_add (added, g_value_get_boxed (&tmp));

  tp_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  for (i = 0; i < added->len; i++)
    g_boxed_free (DBUS_NAME_PAIR_TYPE, added->pdata[i]);
  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

static void
d_bus_names_changed_removed (GabbleTubesChannel *self,
                             guint tube_id,
                             TpHandle contact)
{
  GPtrArray *added = g_ptr_array_new ();
  GArray *removed = g_array_new (FALSE, FALSE, sizeof (guint));

  g_array_append_val (removed, contact);

  tp_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

struct _add_in_old_tubes_data
{
  GHashTable *old_dbus_tubes;
  TpHandle contact;
};

static void
add_in_old_tubes (gpointer key,
                  gpointer value,
                  gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  GabbleTubeDBus *tube = GABBLE_TUBE_DBUS (value);
  struct _add_in_old_tubes_data *data =
    (struct _add_in_old_tubes_data *) user_data;
  TpTubeType type;
  GHashTable *names;

  g_object_get (tube, "type", &type, NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    return;

  g_object_get (tube, "dbus-names", &names, NULL);
  g_assert (names);

  if (g_hash_table_lookup (names, GUINT_TO_POINTER (data->contact)))
    {
      /* contact was in this tube */
      g_hash_table_insert (data->old_dbus_tubes, GUINT_TO_POINTER (tube_id),
          tube);
    }

  g_hash_table_unref (names);
}

struct
_emit_d_bus_names_changed_foreach_data
{
  GabbleTubesChannel *self;
  TpHandle contact;
};

static void
emit_d_bus_names_changed_foreach (gpointer key,
                                  gpointer value,
                                  gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  GabbleTubeDBus *tube = GABBLE_TUBE_DBUS (value);
  struct _emit_d_bus_names_changed_foreach_data *data =
    (struct _emit_d_bus_names_changed_foreach_data *) user_data;
  GHashTable *names;
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (
      data->self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  /* Remove from the D-Bus names mapping */
  g_object_get (tube, "dbus-names", &names, NULL);
  g_hash_table_remove (names, GUINT_TO_POINTER (data->contact));
  g_hash_table_unref (names);

  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_removed (data->self, tube_id, data->contact);

  tp_handle_unref (contact_repo, data->contact);
}

void
gabble_tubes_channel_presence_updated (GabbleTubesChannel *self,
                                       TpHandle contact,
                                       LmMessageNode *tubes_node)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  LmMessageNode *tube_node;
  GHashTable *old_dbus_tubes;
  struct _add_in_old_tubes_data add_data;
  struct _emit_d_bus_names_changed_foreach_data emit_data;

  if (contact == priv->self_handle)
    /* We don't need to inspect our own presence */
    return;

  /* Fill old_dbus_tubes with D-BUS tubes previoulsy announced by
   * the contact */
  old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
  add_data.old_dbus_tubes = old_dbus_tubes;
  add_data.contact = contact;
  g_hash_table_foreach (priv->tubes, add_in_old_tubes, &add_data);

  for (tube_node = tubes_node->children; tube_node != NULL;
      tube_node = tube_node->next)
    {
      const gchar *stream_id;
      GabbleTubeDBus *tube;
      guint tube_id;
      TpTubeType type;

      stream_id = lm_message_node_get_attribute (tube_node, "stream_id");

      if (stream_id == NULL)
        continue;

      tube_id = GPOINTER_TO_UINT (g_hash_table_lookup (
            priv->stream_id_to_tube_id, stream_id));
      tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

      if (tube == NULL)
        {
          /* We don't know yet this tube */
          const gchar *service;
          TpTubeType type;
          TpHandle initiator_handle;
          GHashTable *parameters;

          if (extract_tube_information (self, tube_node, &type,
                &initiator_handle, &service, &parameters))
            {
              GabbleBytestreamIBB *bytestream;

              // XXX we should have a way to detect the type of stream
              // used by the tube and use it
              bytestream = gabble_bytestream_factory_create_ibb (
                  priv->conn->bytestream_factory,
                  priv->handle,
                  priv->handle_type,
                  stream_id,
                  NULL,
                  NULL,
                  TRUE);

              tube_id = create_new_tube (self, type, initiator_handle,
                  service, parameters, TP_TUBE_STATE_LOCAL_PENDING, stream_id,
                  bytestream);
              tube = g_hash_table_lookup (priv->tubes,
                  GUINT_TO_POINTER (tube_id));
            }
        }
      else
        {
          /* The contact is in the tube.
           * Remove it from old_dbus_tubes if needed */
          g_hash_table_remove (old_dbus_tubes, GUINT_TO_POINTER (tube_id));
        }

      if (tube == NULL)
        continue;

      g_object_get (tube, "type", &type, NULL);

      if (type == TP_TUBE_TYPE_DBUS)
        {
          /* Update mapping of handle -> D-Bus name. */

          GHashTable *names;
          gchar *name;

          g_object_get (tube, "dbus-names", &names, NULL);
          g_assert (names);
          name = g_hash_table_lookup (names, GUINT_TO_POINTER (contact));

          if (!name)
            {
              /* Contact just joined the tube */
              const gchar *new_name = lm_message_node_get_attribute (tube_node,
                  "dbus-name");
              TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
                ((TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

              if (!new_name)
                continue;

              g_hash_table_insert (names, GUINT_TO_POINTER (contact),
                  g_strdup (new_name));
              tp_handle_ref (contact_repo, contact);

              /* Emit the DBusNamesChanged signal */
              d_bus_names_changed_added (self, tube_id, contact, new_name);
            }

          g_hash_table_unref (names);
        }
    }

  /* Tubes remaining in old_dbus_tubes was left by the contact */
  emit_data.contact = contact;
  emit_data.self = self;
  g_hash_table_foreach (old_dbus_tubes, emit_d_bus_names_changed_foreach,
      &emit_data);

  g_hash_table_destroy (old_dbus_tubes);
}

static void
copy_tube_in_ptr_array (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  GabbleTubeDBus *tube = (GabbleTubeDBus *) value;
  guint tube_id = GPOINTER_TO_UINT(key);
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  TpTubeState state;
  TpTubeType type;
  GPtrArray *array = (GPtrArray *) user_data;
  GValue entry = {0,};

  g_object_get (tube,
                "type", &type,
                "initiator", &initiator,
                "service", &service,
                "parameters", &parameters,
                "state", &state,
                NULL);

  g_value_init (&entry, TP_CHANNEL_TUBE_TYPE);
  g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (TP_CHANNEL_TUBE_TYPE));
  dbus_g_type_struct_set (&entry,
          0, tube_id,
          1, initiator,
          2, type,
          3, service,
          4, parameters,
          5, state,
          G_MAXUINT);

  g_ptr_array_add (array, g_value_get_boxed (&entry));
  g_free (service);
  g_hash_table_unref (parameters);
}

static GPtrArray *
make_tubes_ptr_array (GabbleTubesChannel *self,
                      GHashTable *tubes)
{
  GPtrArray *ret;

  ret = g_ptr_array_sized_new (g_hash_table_size (tubes));

  g_hash_table_foreach (tubes, copy_tube_in_ptr_array, ret);

  return ret;
}

/**
 * gabble_tubes_channel_get_available_tube_types
 *
 * Implements D-Bus method GetAvailableTubeTypes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_get_available_tube_types (TpSvcChannelTypeTubes *iface,
                                               DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GArray *ret;
  TpTubeType type;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  ret = g_array_sized_new (FALSE, FALSE, sizeof (TpTubeType), 1);
  type = TP_TUBE_TYPE_DBUS;
  g_array_append_val (ret, type);

  tp_svc_channel_type_tubes_return_from_get_available_tube_types (context,
      ret);

  g_array_free (ret, TRUE);
}

/**
 * gabble_tubes_channel_list_tubes
 *
 * Implements D-Bus method ListTubes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_list_tubes (TpSvcChannelTypeTubes *iface,
                                 DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  GPtrArray *ret;
  guint i;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  ret = make_tubes_ptr_array (self, priv->tubes);
  tp_svc_channel_type_tubes_return_from_list_tubes (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (TP_CHANNEL_TUBE_TYPE, ret->pdata[i]);

  g_ptr_array_free (ret, TRUE);
}

static void
copy_parameter (gpointer key,
                gpointer value,
                gpointer user_data)
{
  const gchar *prop = key;
  GValue *gvalue = value;
  GHashTable *parameters = user_data;
  GValue *gvalue_copied;

  gvalue_copied = g_slice_new0 (GValue);
  g_value_init (gvalue_copied, G_VALUE_TYPE (gvalue));
  g_value_copy (gvalue, gvalue_copied);

  g_hash_table_insert (parameters, g_strdup (prop), gvalue_copied);
}

static void
set_parameter (gpointer key,
               gpointer value,
               gpointer user_data)
{
  /* XXX use the code defined in the OLPC branch */
  GValue *gvalue = value;
  LmMessageNode *parameters_node = (LmMessageNode*) user_data;
  LmMessageNode *parameter;
  const gchar *type = NULL;

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      type = "str";
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      type = "bytes";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      type = "int";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      type = "uint";
    }
  else
    {
      /* a type we don't know how to handle: ignore it */
      return;
    }

  parameter = lm_message_node_add_child (parameters_node,
      "parameter", NULL);

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      lm_message_node_set_value (parameter,
        g_value_get_string (gvalue));
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      GArray *arr;
      gchar *str;

      arr = g_value_get_boxed (gvalue);
      str = base64_encode (arr->len, arr->data);
      lm_message_node_set_value (parameter, str);
      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_int (gvalue));
      lm_message_node_set_value (parameter, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_uint (gvalue));
      lm_message_node_set_value (parameter, str);

      g_free (str);
    }
  else
    {
      g_debug ("property with unknown type \"%s\"", g_type_name
          (G_VALUE_TYPE (gvalue)));
    }

  lm_message_node_set_attributes (parameter,
      "name", key,
      "type", type,
      NULL);
}

static void
publish_tube_in_node (LmMessageNode *node,
                      GabbleTubeDBus *tube,
                      const gchar *stream_id)
{
  LmMessageNode *parameters_node;
  GHashTable *parameters;
  TpTubeType type;
  gchar *service;

  g_object_get (G_OBJECT (tube),
      "service", &service,
      "parameters", &parameters,
      "type", &type,
      NULL);

  lm_message_node_set_attributes (node,
      "stream_id", stream_id,
      "service", service,
      NULL);

  switch (type)
    {
      case TP_TUBE_TYPE_DBUS:
        lm_message_node_set_attribute (node, "type", "dbus");
        break;
      default:
        g_assert_not_reached ();
    }

  if (type == TP_TUBE_TYPE_DBUS)
    {
      gchar *name;

      g_object_get (G_OBJECT (tube), "dbus-name", &name, NULL);
      lm_message_node_set_attribute (node, "dbus-name", name);
      g_free (name);
    }

  parameters_node = lm_message_node_add_child (node, "parameters",
      NULL);
  g_hash_table_foreach (parameters, set_parameter, parameters_node);

  g_free (service);
  g_hash_table_unref (parameters);
}

struct _i_hate_g_hash_table_foreach
{
  GabbleTubesChannel *self;
  LmMessageNode *tubes_node;
};

static void
publish_tubes_in_node (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (value);
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  TpTubeState state;
  GabbleTubesChannelPrivate *priv =
      GABBLE_TUBES_CHANNEL_GET_PRIVATE (data->self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle initiator_handle;
  LmMessageNode *tube_node;
  const gchar *initiator;
  TpTubeType type;
  gchar *stream_id;
  GabbleTubeDBus *tube = g_hash_table_lookup (priv->tubes,
      GUINT_TO_POINTER (tube_id));

  g_object_get (tube,
                "state", &state,
                NULL);

  if (state != TP_TUBE_STATE_OPEN)
    return;

  stream_id = gabble_tube_dbus_get_stream_id (GABBLE_TUBE_DBUS (tube));

  tube_node = lm_message_node_add_child (data->tubes_node, "tube", NULL);
  publish_tube_in_node (tube_node, tube, stream_id);

  g_object_get (tube,
        "type", &type,
        "initiator", &initiator_handle,
        NULL);
  initiator = tp_handle_inspect (contact_repo, initiator_handle);
  lm_message_node_set_attribute (tube_node, "initiator", initiator);

  g_free (stream_id);
}

static void
update_tubes_presence (GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection*) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_ROOM);
  LmMessage *msg;
  LmMessageNode *node;
  const gchar *main_jid, *jid;
  gchar *username, *to;
  struct _i_hate_g_hash_table_foreach data;

  /* build the message */
  jid = tp_handle_inspect (room_repo, priv->handle);

  main_jid = tp_handle_inspect (contact_repo, conn->self_handle);

  gabble_decode_jid (main_jid, &username, NULL, NULL);

  to = g_strdup_printf ("%s/%s", jid, username);

  msg = lm_message_new (to, LM_MESSAGE_TYPE_PRESENCE);

  node = lm_message_node_add_child (msg->node, "x", NULL);
  lm_message_node_set_attribute (node, "xmlns", NS_MUC);

  node = lm_message_node_add_child (msg->node, "tubes", NULL);
  lm_message_node_set_attribute (node, "xmlns", NS_TUBES);
  data.self = self;
  data.tubes_node = node;

  g_hash_table_foreach (priv->stream_id_to_tube_id, publish_tubes_in_node,
      &data);

  /* Send it */
  _gabble_connection_send (priv->conn, msg, NULL);

  g_free (username);
  g_free (to);
  lm_message_unref (msg);
}

static void
bytestream_negotiate_cb (GabbleBytestreamIBB *bytestream,
                         const gchar *stream_id,
                         LmMessage *msg,
                         gpointer user_data)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (user_data);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeDBus *tube;
  guint tube_id;

  tube_id = GPOINTER_TO_UINT (g_hash_table_lookup (priv->stream_id_to_tube_id,
      stream_id));
  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

  if (tube == NULL)
    {
      DEBUG ("tube unknow");
      return;
    }

  if (bytestream != NULL)
    {
      /* Tube was accepted by remote user */
      g_object_set (tube,
          "bytestream", bytestream,
          "state", TP_TUBE_STATE_OPEN,
          NULL);

      tp_svc_channel_type_tubes_emit_tube_state_changed (self, tube_id,
          TP_TUBE_STATE_OPEN);
    }

  else
    {
      /* Tube was declined by remote user. Close it */
      g_hash_table_remove (priv->tubes, GUINT_TO_POINTER (tube_id));
      g_hash_table_remove (priv->stream_id_to_tube_id, stream_id);

      tp_svc_channel_type_tubes_emit_tube_closed (self, tube_id);
    }
}

void
gabble_tubes_channel_tube_offered (GabbleTubesChannel *self,
                                   GabbleBytestreamIBB *bytestream,
                                   LmMessage *msg)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  const gchar *service, *stream_id;
  GHashTable *parameters;
  TpTubeType type;
  LmMessageNode *node;

  node = lm_message_node_get_child_with_namespace (msg->node, "tube",
      NS_SI_TUBES);

  if (node == NULL)
    {
      // XXX decline/destroy bytestream
      NODE_DEBUG (msg->node, "got a SI request without a tube field");
      return;
    }

  stream_id = lm_message_node_get_attribute (node, "stream_id");

  if (stream_id == NULL)
    {
      // XXX decline/destroy bytestream
      NODE_DEBUG (msg->node, "got a SI request without stream ID");
      return;
    }

  if (g_hash_table_lookup (priv->stream_id_to_tube_id, stream_id) != NULL)
    {
      // XXX decline/destroy bytestream
      DEBUG ("we already have a tube using this stream id: %s", stream_id);
      return;
    }

  if (!extract_tube_information (self, node, &type, NULL,
              &service, &parameters))
    {
      // XXX decline/destroy bytestream
      return;
    }

  create_new_tube (self, type, priv->handle, service,
      parameters, TP_TUBE_STATE_LOCAL_PENDING, stream_id, bytestream);
}

/**
 * gabble_tubes_channel_offer_tube
 *
 * Implements D-Bus method OfferTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_offer_tube (TpSvcChannelTypeTubes *iface,
                                 guint type,
                                 const gchar *service,
                                 GHashTable *parameters,
                                 DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  TpBaseConnection *base;
  GabbleBytestreamIBB *bytestream = NULL;
  guint tube_id;
  GHashTable *parameters_copied;
  TpTubeState state;
  gchar *stream_id;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection*) priv->conn;

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError *error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid type: %d", type);

      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  parameters_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) free_gvalue);
  g_hash_table_foreach (parameters, copy_parameter, parameters_copied);

  stream_id = gabble_bytestream_factory_generate_stream_id ();

  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    {
      /* We don't need SI for muc tubes so the bytestream is
       * already open */

      bytestream = gabble_bytestream_factory_create_ibb (
          priv->conn->bytestream_factory,
          priv->handle,
          priv->handle_type,
          stream_id,
          NULL,
          NULL,
          TRUE);

      state = TP_TUBE_STATE_OPEN;
    }
  else
    {
      state = TP_TUBE_STATE_REMOTE_PENDING;
    }

  tube_id = create_new_tube (self, type, base->self_handle, service,
      parameters_copied, state, (const gchar*) stream_id, bytestream);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Stream initiation */
      LmMessageNode *node;
      GabbleTubeDBus *tube;

      tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

      node = lm_message_node_new ("tube", NULL);
      lm_message_node_set_attribute (node, "xmlns", NS_SI_TUBES);
      publish_tube_in_node (node, tube, stream_id);

      gabble_bytestream_factory_negotiate_stream (
          priv->conn->bytestream_factory,
          priv->handle,
          NS_SI_TUBES,
          stream_id,
          node,
          bytestream_negotiate_cb,
          self);

      lm_message_node_unref (node);
    }

  tp_svc_channel_type_tubes_return_from_offer_tube (context, tube_id);

  g_free (stream_id);
}

/**
 * gabble_tubes_channel_accept_tube
 *
 * Implements D-Bus method AcceptTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_accept_tube (TpSvcChannelTypeTubes *iface,
                                  guint id,
                                  DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  GabbleTubeDBus *tube;
  TpTubeState state;
  TpTubeType type;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);

      return;
    }

  g_object_get (tube, "state", &state, NULL);
  if (state != TP_TUBE_STATE_LOCAL_PENDING)
    {
      /* XXX raise an error if the tube was not in the local pending state ? */
      tp_svc_channel_type_tubes_return_from_accept_tube (context);
      return;
    }

  g_object_set (tube, "state", TP_TUBE_STATE_OPEN, NULL);

  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    {
      update_tubes_presence (self);
    }

  tp_svc_channel_type_tubes_emit_tube_state_changed (iface, id,
      TP_TUBE_STATE_OPEN);

  g_object_get (tube, "type", &type, NULL);
  if (type == TP_TUBE_TYPE_DBUS)
    {
      gchar *name;

      g_object_get (tube, "dbus-name", &name, NULL);

      add_yourself_in_dbus_names (self, tube);
      /* Emit the DBusNamesChanged signal */
      d_bus_names_changed_added (self, id, priv->self_handle, name);

      g_free (name);
    }

  tp_svc_channel_type_tubes_return_from_accept_tube (context);
}


static void
close_tube (GabbleTubesChannel *self,
            GabbleTubeDBus *tube,
            guint tube_id)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  gchar *stream_id;

  if (tube == NULL)
    return;

  stream_id = gabble_tube_dbus_get_stream_id (tube);

  gabble_tube_dbus_close (tube);

  g_hash_table_remove (priv->tubes, GUINT_TO_POINTER (tube_id));

  if (stream_id != NULL)
    {
      if (!g_hash_table_remove (priv->stream_id_to_tube_id, stream_id))
        {
          DEBUG ("Can't find tube id using this stream id: %s", stream_id);
        }

      g_free (stream_id);
    }

  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_removed (self, tube_id, priv->self_handle);

  tp_svc_channel_type_tubes_emit_tube_closed (self, tube_id);
}

/**
 * gabble_tubes_channel_close_tube
 *
 * Implements D-Bus method CloseTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_close_tube (TpSvcChannelTypeTubes *iface,
                                 guint id,
                                 DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  GabbleTubeDBus *tube;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  close_tube (self, tube, id);

  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    {
      update_tubes_presence (self);
    }

  tp_svc_channel_type_tubes_return_from_close_tube (context);
}


/**
 * gabble_tubes_channel_get_d_bus_server_address
 *
 * Implements D-Bus method GetDBusServerAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_get_d_bus_server_address (TpSvcChannelTypeTubes *iface,
                                               guint id,
                                               DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  GabbleTubeDBus *tube;
  gchar *addr;
  guint type;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "type", &type, NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-address", &addr, NULL);
  tp_svc_channel_type_tubes_return_from_get_d_bus_server_address (context,
      addr);
  g_free (addr);
}


static void
get_d_bus_names_foreach (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GPtrArray *ret = user_data;
  GValue tmp = {0,};

  g_value_init (&tmp, DBUS_NAME_PAIR_TYPE);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (DBUS_NAME_PAIR_TYPE));
  dbus_g_type_struct_set (&tmp,
      0, key,
      1, value,
      G_MAXUINT);
  g_ptr_array_add (ret, g_value_get_boxed (&tmp));
}


/**
 * gabble_tubes_channel_get_d_bus_names
 *
 * Implements D-Bus method GetDBusNames
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_get_d_bus_names (TpSvcChannelTypeTubes *iface,
                                      guint id,
                                      DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv  = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GObject *tube;
  GHashTable *names;
  GPtrArray *ret;
  TpTubeType type;
  guint i;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "type", &type, NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-names", &names, NULL);
  g_assert (names);

  ret = g_ptr_array_sized_new (g_hash_table_size (names));
  g_hash_table_foreach (names, get_d_bus_names_foreach, ret);

  tp_svc_channel_type_tubes_return_from_get_d_bus_names (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (DBUS_NAME_PAIR_TYPE, ret->pdata[i]);
  g_hash_table_unref (names);
  g_ptr_array_free (ret, TRUE);
}

static void
emit_tube_closed_signal (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  guint id = GPOINTER_TO_UINT (key);
  GabbleTubesChannel *self = (GabbleTubesChannel*) user_data;

  tp_svc_channel_type_tubes_emit_tube_closed (self, id);
}

static void
close_tubes_channel (GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  DEBUG ("called on %p", self);

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->closed)
    {
      return;
    }

  g_assert (g_hash_table_size (priv->tubes) ==
      g_hash_table_size (priv->stream_id_to_tube_id));

  g_hash_table_foreach (priv->tubes, emit_tube_closed_signal, self);
  g_hash_table_destroy (priv->tubes);
  g_hash_table_destroy (priv->stream_id_to_tube_id);

  priv->tubes = NULL;
  priv->stream_id_to_tube_id = NULL;
  priv->closed = TRUE;

  tp_svc_channel_emit_closed (self);
}


/**
 * gabble_tubes_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tubes_channel_close (TpSvcChannel *iface,
                            DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  close_tubes_channel (self);
  tp_svc_channel_return_from_close (context);
}


/**
 * gabble_tubes_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tubes_channel_get_channel_type (TpSvcChannel *iface,
                                       DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TUBES);
}


/**
 * gabble_tubes_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tubes_channel_get_handle (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));
  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, priv->handle_type,
      priv->handle);
}


/**
 * gabble_tubes_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tubes_channel_get_interfaces (TpSvcChannel *iface,
                                     DBusGMethodInvocation *context)
{
  const char *interfaces[] = {
      NULL };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}

static void gabble_tubes_channel_dispose (GObject *object);
static void gabble_tubes_channel_finalize (GObject *object);

static void
gabble_tubes_channel_class_init (
    GabbleTubesChannelClass *gabble_tubes_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_tubes_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_tubes_channel_class,
      sizeof (GabbleTubesChannelPrivate));

  object_class->constructor = gabble_tubes_channel_constructor;

  object_class->get_property = gabble_tubes_channel_get_property;
  object_class->set_property = gabble_tubes_channel_set_property;

  object_class->dispose = gabble_tubes_channel_dispose;
  object_class->finalize = gabble_tubes_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this Tubes channel object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  /* XXX: Is this crack? It's a pain to look up the self handle on the muc, so
   * we have the factory do it for us; this also means that the factory can
   * check whether we're in the muc or not.
   */
  param_spec = g_param_spec_uint (
      "self-handle",
      "Self handle",
      "The handle to use for ourself. This can be different from the "
      "connection's self handle if our handle is a room handle.",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SELF_HANDLE, param_spec);
}

void
gabble_tubes_channel_dispose (GObject *object)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (object);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  close_tubes_channel (self);

  g_assert (priv->closed);
  g_assert (priv->tubes == NULL);

  if (G_OBJECT_CLASS (gabble_tubes_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_tubes_channel_parent_class)->dispose (object);
}

void
gabble_tubes_channel_finalize (GObject *object)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (object);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  g_free (priv->object_path);

  G_OBJECT_CLASS (gabble_tubes_channel_parent_class)->finalize (object);
}

static void
tubes_iface_init (gpointer g_iface,
                  gpointer iface_data)
{
  TpSvcChannelTypeTubesClass *klass = (TpSvcChannelTypeTubesClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_tubes_implement_##x (\
    klass, gabble_tubes_channel_##x)
  IMPLEMENT(get_available_tube_types);
  IMPLEMENT(list_tubes);
  IMPLEMENT(offer_tube);
  IMPLEMENT(accept_tube);
  IMPLEMENT(close_tube);
  IMPLEMENT(get_d_bus_server_address);
  IMPLEMENT(get_d_bus_names);
#undef IMPLEMENT
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_tubes_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}
