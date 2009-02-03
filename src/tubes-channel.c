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

#include "config.h"
#include "tubes-channel.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "bytestream-factory.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "presence.h"
#include "private-tubes-factory.h"
#include "tube-iface.h"
#include "tube-dbus.h"
#include "tube-stream.h"
#include "tube-dbus.h"
#include "util.h"

static void channel_iface_init (gpointer, gpointer);
static void tubes_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleTubesChannel, gabble_tubes_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TUBES, tubes_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_external_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

static const gchar *gabble_tubes_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    /* If more interfaces are added, either keep Group as the first, or change
     * the implementations of gabble_tubes_channel_get_interfaces () and
     * gabble_tubes_channel_get_property () too */
    NULL
};


enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_REQUESTED,
  PROP_CONNECTION,
  PROP_INTERFACES,
  PROP_MUC,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  LAST_PROPERTY,
};

/* private structure */

struct _GabbleTubesChannelPrivate
{
  GabbleConnection *conn;
  char *object_path;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandle self_handle;
  TpHandle initiator;
  gboolean requested;

  GHashTable *tubes;

  gulong pre_presence_signal;
  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_TUBES_CHANNEL_GET_PRIVATE(obj) ((obj)->priv)

static gboolean update_tubes_presence (GabbleTubesChannel *self);

static void pre_presence_cb (GabbleMucChannel *muc, LmMessage *msg,
    GabbleTubesChannel *self);

static void
gabble_tubes_channel_init (GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBES_CHANNEL, GabbleTubesChannelPrivate);

  self->priv = priv;

  priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);
}

static GObject *
gabble_tubes_channel_constructor (GType type,
                                  guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleTubesChannel *self;
  GabbleTubesChannelPrivate *priv;
  DBusGConnection *bus;
  TpHandleRepoIface *handle_repo, *contact_repo;

  DEBUG ("Called");

  obj = G_OBJECT_CLASS (gabble_tubes_channel_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_TUBES_CHANNEL (obj);
  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, priv->handle_type);

  tp_handle_ref (handle_repo, priv->handle);

  if (priv->initiator != 0)
    tp_handle_ref (contact_repo, priv->initiator);

  switch (priv->handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      g_assert (self->muc == NULL);
      g_assert (priv->initiator != 0);
      priv->self_handle = ((TpBaseConnection *) (priv->conn))->self_handle;
      break;
    case TP_HANDLE_TYPE_ROOM:
      g_assert (self->muc != NULL);

      priv->pre_presence_signal = g_signal_connect (self->muc, "pre-presence",
          G_CALLBACK (pre_presence_cb), self);

      priv->self_handle = self->muc->group.self_handle;
      tp_external_group_mixin_init (obj, (GObject *) self->muc);
      break;
    default:
      g_return_val_if_reached (NULL);
    }

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  DEBUG ("Registering at '%s'", priv->object_path);

  return obj;
}

static void
gabble_tubes_channel_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabbleTubesChannel *chan = GABBLE_TUBES_CHANNEL (object);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (chan);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

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
      case PROP_TARGET_ID:
          {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (
                base_conn, priv->handle_type);

            g_value_set_string (value,
                tp_handle_inspect (repo, priv->handle));
          }
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_INTERFACES:
        if (chan->muc)
          {
            /* MUC tubes */
            g_value_set_boxed (value, gabble_tubes_channel_interfaces);
          }
        else
          {
            /* 1-1 tubes - omit the Group interface */
            g_value_set_boxed (value, gabble_tubes_channel_interfaces + 1);
          }
        break;
      case PROP_MUC:
        g_value_set_object (value, chan->muc);
        break;
      case PROP_INITIATOR_HANDLE:
        g_value_set_uint (value, priv->initiator);
        break;
      case PROP_INITIATOR_ID:
        if (priv->initiator == 0)
          {
            g_value_set_static_string (value, "");
          }
        else
          {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (
                base_conn, TP_HANDLE_TYPE_CONTACT);

            g_value_set_string (value,
                tp_handle_inspect (repo, priv->initiator));
          }
        break;
      case PROP_REQUESTED:
        g_value_set_boolean (value, priv->requested);
        break;
      case PROP_CHANNEL_DESTROYED:
        g_value_set_boolean (value, priv->closed);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_take_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                TP_IFACE_CHANNEL, "Requested",
                TP_IFACE_CHANNEL, "Interfaces",
                NULL));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_tubes_channel_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
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
      case PROP_CHANNEL_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changeable on this channel, so we do nothing */
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
      case PROP_MUC:
        chan->muc = g_value_get_object (value);
        break;
      case PROP_INITIATOR_HANDLE:
        priv->initiator = g_value_get_uint (value);
        break;
      case PROP_REQUESTED:
        priv->requested = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
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

  g_value_init (&tmp, TP_STRUCT_TYPE_DBUS_TUBE_MEMBER);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (TP_STRUCT_TYPE_DBUS_TUBE_MEMBER));
  dbus_g_type_struct_set (&tmp,
      0, contact,
      1, new_name,
      G_MAXUINT);
  g_ptr_array_add (added, g_value_get_boxed (&tmp));

  tp_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  for (i = 0; i < added->len; i++)
    g_boxed_free (TP_STRUCT_TYPE_DBUS_TUBE_MEMBER, added->pdata[i]);
  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

static void
d_bus_names_changed_removed (GabbleTubesChannel *self,
                             guint tube_id,
                             TpHandle contact)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GPtrArray *added;
  GArray *removed;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    return;

  added = g_ptr_array_new ();
  removed = g_array_new (FALSE, FALSE, sizeof (guint));

  g_array_append_val (removed, contact);

  tp_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

static void
add_name_in_dbus_names (GabbleTubesChannel *self,
                        guint tube_id,
                        TpHandle handle,
                        const gchar *dbus_name)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeDBus *tube;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    return;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  if (gabble_tube_dbus_add_name (tube, handle, dbus_name))
    {
      /* Emit the DBusNamesChanged signal */
      d_bus_names_changed_added (self, tube_id, handle, dbus_name);
    }
}

static void
add_yourself_in_dbus_names (GabbleTubesChannel *self,
                            guint tube_id)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeDBus *tube;
  gchar *dbus_name;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    return;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  g_object_get (tube,
      "dbus-name", &dbus_name,
      NULL);

  add_name_in_dbus_names (self, tube_id, priv->self_handle, dbus_name);

  g_free (dbus_name);
}

static void
tube_closed_cb (GabbleTubeIface *tube,
                gpointer user_data)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (user_data);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  guint tube_id;

  if (priv->closed)
    return;

  g_object_get (tube, "id", &tube_id, NULL);
  if (!g_hash_table_remove (priv->tubes, GUINT_TO_POINTER (tube_id)))
    {
      DEBUG ("Can't find tube having this id: %d", tube_id);
    }
  DEBUG ("tube %d removed", tube_id);

  /* Emit the DBusNamesChanged signal if muc tube */
  d_bus_names_changed_removed (self, tube_id, priv->self_handle);

  update_tubes_presence (self);

  tp_svc_channel_type_tubes_emit_tube_closed (self, tube_id);

  tp_svc_channel_emit_closed (tube);

  /* Ideally, this should be done in the factory directly but the private
   * tubes factory and the muc factory are not aware of tube channels.
   * This design is a legacy of the old tube API and we can't really change it
   * without big refactoring. */
  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      tp_channel_manager_emit_channel_closed_for_object (
          priv->conn->private_tubes_factory, TP_EXPORTABLE_CHANNEL (tube));
    }
  else
    {
      tp_channel_manager_emit_channel_closed_for_object (
          priv->conn->muc_factory, TP_EXPORTABLE_CHANNEL (tube));
    }

}

static void
tube_opened_cb (GabbleTubeIface *tube,
                gpointer user_data)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (user_data);
  guint tube_id;

  g_object_get (tube, "id", &tube_id, NULL);

  tp_svc_channel_type_tubes_emit_tube_state_changed (self, tube_id,
      TP_TUBE_STATE_OPEN);
}

static void
tube_offered_cb (GabbleTubeIface *tube,
                 gpointer user_data)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (user_data);
  guint tube_id;
  TpHandle initiator;
  TpTubeType type;
  gchar *service;
  GHashTable *parameters;
  TpTubeState state;

  g_object_get (tube,
      "id", &tube_id,
      "initiator-handle", &initiator,
      "type", &type,
      "service", &service,
      "parameters", &parameters,
      "state", &state,
      NULL);

  /* tube has been offered and so can be announced using the old API */
  tp_svc_channel_type_tubes_emit_new_tube (self,
      tube_id,
      initiator,
      type,
      service,
      parameters,
      state);

  update_tubes_presence (self);

  if (type == TP_TUBE_TYPE_DBUS)
    {
      add_yourself_in_dbus_names (self, tube_id);
    }

  g_free (service);
  g_hash_table_destroy (parameters);
}

static GabbleTubeIface *
create_new_tube (GabbleTubesChannel *self,
                 TpTubeType type,
                 TpHandle initiator,
                 const gchar *service,
                 GHashTable *parameters,
                 const gchar *stream_id,
                 guint tube_id,
                 GabbleBytestreamIface *bytestream)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeIface *tube;
  TpTubeState state;

  switch (type)
    {
    case TP_TUBE_TYPE_DBUS:
      tube = GABBLE_TUBE_IFACE (gabble_tube_dbus_new (priv->conn,
          priv->handle, priv->handle_type, priv->self_handle, initiator,
          service, parameters, stream_id, tube_id, bytestream));
      break;
    case TP_TUBE_TYPE_STREAM:
      tube = GABBLE_TUBE_IFACE (gabble_tube_stream_new (priv->conn,
          priv->handle, priv->handle_type, priv->self_handle, initiator,
          service, parameters, tube_id));
      break;
    default:
      g_return_val_if_reached (NULL);
    }

  DEBUG ("create tube %u", tube_id);
  g_hash_table_insert (priv->tubes, GUINT_TO_POINTER (tube_id), tube);

  g_object_get (tube, "state", &state, NULL);

  /* The old API doesn't know the "not offered" state, so we have to wait that
   * the tube is offered before announcing it. */
  if (state != GABBLE_TUBE_CHANNEL_STATE_NOT_OFFERED)
    {
      tp_svc_channel_type_tubes_emit_new_tube (self,
          tube_id,
          initiator,
          type,
          service,
          parameters,
          state);
    }

  g_signal_connect (tube, "tube-opened", G_CALLBACK (tube_opened_cb), self);
  g_signal_connect (tube, "tube-closed", G_CALLBACK (tube_closed_cb), self);
  g_signal_connect (tube, "tube-offered", G_CALLBACK (tube_offered_cb), self);

  return tube;
}

static gboolean
extract_tube_information (GabbleTubesChannel *self,
                          LmMessageNode *tube_node,
                          TpTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters,
                          guint *tube_id)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (type != NULL)
    {
      const gchar *_type;

      _type = lm_message_node_get_attribute (tube_node, "type");

      if (!tp_strdiff (_type, "stream"))
        {
          *type = TP_TUBE_TYPE_STREAM;
        }
      else if (!tp_strdiff (_type, "dbus"))
        {
          *type = TP_TUBE_TYPE_DBUS;
        }
      else
        {
          DEBUG ("Unknown tube type: %s", _type);
          return FALSE;
        }
    }

  if (initiator_handle != NULL)
    {
      const gchar *initiator;

      initiator = lm_message_node_get_attribute (tube_node, "initiator");

      if (initiator != NULL)
        {
          *initiator_handle = tp_handle_ensure (contact_repo, initiator,
              GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

          if (*initiator_handle == 0)
            {
              DEBUG ("invalid initiator JID %s", initiator);
              return FALSE;
            }
        }
      else
        {
          *initiator_handle = 0;
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
      *parameters = lm_message_node_extract_properties (node, "parameter");
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      gchar *endptr;
      unsigned long tmp;

      str = lm_message_node_get_attribute (tube_node, "id");
      if (str == NULL)
        {
          DEBUG ("no tube id in SI request");
          return FALSE;
        }

      tmp = strtoul (str, &endptr, 10);
      if (!endptr || *endptr || tmp > G_MAXUINT32)
        {
          DEBUG ("tube id is not numeric or > 2**32: %s", str);
          return FALSE;
        }
      *tube_id = (guint) tmp;
    }

  return TRUE;
}

struct _add_in_old_dbus_tubes_data
{
  GHashTable *old_dbus_tubes;
  TpHandle contact;
};

static void
add_in_old_dbus_tubes (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  GabbleTubeIface *tube = GABBLE_TUBE_IFACE (value);
  struct _add_in_old_dbus_tubes_data *data =
    (struct _add_in_old_dbus_tubes_data *) user_data;
  TpTubeType type;

  g_object_get (tube, "type", &type, NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    return;

  if (gabble_tube_dbus_handle_in_names (GABBLE_TUBE_DBUS (tube),
        data->contact))
    {
      /* contact was in this tube */
      g_hash_table_insert (data->old_dbus_tubes, GUINT_TO_POINTER (tube_id),
          tube);
    }
}

struct
_emit_d_bus_names_changed_foreach_data
{
  GabbleTubesChannel *self;
  TpHandle contact;
};

struct _ForeachData
{
  TpExportableChannelFunc foreach;
  gpointer user_data;
};

static void
foreach_slave (gpointer key,
               gpointer value,
               gpointer user_data)
{
  GabbleTubeIface *tube = GABBLE_TUBE_IFACE (value);
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpTubeType type;

  g_object_get (tube, "type", &type, NULL);
  if (type == TP_TUBE_TYPE_STREAM)
    data->foreach (TP_EXPORTABLE_CHANNEL (tube), data->user_data);
}

void gabble_tubes_channel_foreach (GabbleTubesChannel *self,
    TpExportableChannelFunc foreach, gpointer user_data)
{
  struct _ForeachData data;
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->tubes, foreach_slave, &data);
}

static void
emit_d_bus_names_changed_foreach (gpointer key,
                                  gpointer value,
                                  gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  GabbleTubeDBus *tube = GABBLE_TUBE_DBUS (value);
  struct _emit_d_bus_names_changed_foreach_data *data =
    (struct _emit_d_bus_names_changed_foreach_data *) user_data;

  /* Remove from the D-Bus names mapping */
  if (gabble_tube_dbus_remove_name (tube, data->contact))
    {
      /* Emit the DBusNamesChanged signal */
      d_bus_names_changed_removed (data->self, tube_id, data->contact);
    }
}

static void
contact_left_muc (GabbleTubesChannel *self,
                  TpHandle contact)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
#ifdef ENABLE_DEBUG
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
#endif
  GHashTable *old_dbus_tubes;
  struct _add_in_old_dbus_tubes_data add_data;
  struct _emit_d_bus_names_changed_foreach_data emit_data;

  DEBUG ("%s left muc and so left all its tube", tp_handle_inspect (
        contact_repo, contact));

  /* Fill old_dbus_tubes with D-BUS tubes previoulsy announced by
   * the contact */
  old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
  add_data.old_dbus_tubes = old_dbus_tubes;
  add_data.contact = contact;
  g_hash_table_foreach (priv->tubes, add_in_old_dbus_tubes, &add_data);

  /* contact left the muc so he left all its tubes */
  emit_data.contact = contact;
  emit_data.self = self;
  g_hash_table_foreach (old_dbus_tubes, emit_d_bus_names_changed_foreach,
      &emit_data);

  g_hash_table_destroy (old_dbus_tubes);
}

/* Called when we receive a presence from a contact who is
 * in the muc associated with this tubes channel */
void
gabble_tubes_channel_presence_updated (GabbleTubesChannel *self,
                                       TpHandle contact,
                                       LmMessage *presence)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  LmMessageNode *tubes_node, *tube_node;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *presence_type;
  GHashTable *old_dbus_tubes;
  struct _add_in_old_dbus_tubes_data add_data;
  struct _emit_d_bus_names_changed_foreach_data emit_data;

  if (contact == priv->self_handle)
    /* We don't need to inspect our own presence */
    return;

  /* We are interested by this presence only if it contains tube information
   * or indicates someone left the muc */
  presence_type = lm_message_node_get_attribute (presence->node, "type");
  if (!tp_strdiff (presence_type, "unavailable"))
    {
      contact_left_muc (self, contact);
      return;
    }

  tubes_node = lm_message_node_get_child_with_namespace (presence->node,
      "tubes", NS_TUBES);

  if (tubes_node == NULL)
    return;

  /* Fill old_dbus_tubes with D-BUS tubes previoulsy announced by
   * the contact */
  old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
  add_data.old_dbus_tubes = old_dbus_tubes;
  add_data.contact = contact;
  g_hash_table_foreach (priv->tubes, add_in_old_dbus_tubes, &add_data);

  for (tube_node = tubes_node->children; tube_node != NULL;
      tube_node = tube_node->next)
    {
      const gchar *stream_id;
      GabbleTubeIface *tube;
      guint tube_id;
      TpTubeType type;

      stream_id = lm_message_node_get_attribute (tube_node, "stream-id");

      if (!extract_tube_information (self, tube_node, NULL,
          NULL, NULL, NULL, &tube_id))
        {
          DEBUG ("Bad tube ID, skipping to next child of <tubes>");
          continue;
        }

      tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

      if (tube == NULL)
        {
          /* We don't know yet this tube */
          const gchar *service;
          TpHandle initiator_handle;
          GHashTable *parameters;

          if (extract_tube_information (self, tube_node, &type,
                &initiator_handle, &service, &parameters, NULL))
            {
              switch (type)
                {
                  case TP_TUBE_TYPE_DBUS:
                    {
                      if (initiator_handle == 0)
                        {
                          DEBUG ("D-Bus tube initiator missing");
                          /* skip to the next child of <tubes> */
                          continue;
                        }
                    }
                    break;
                  case TP_TUBE_TYPE_STREAM:
                    {
                      if (initiator_handle != 0)
                        /* ignore it */
                        tp_handle_unref (contact_repo, initiator_handle);

                      initiator_handle = contact;
                      tp_handle_ref (contact_repo, initiator_handle);
                    }
                    break;
                  default:
                    {
                      g_return_if_reached ();
                    }
                }

              tube = create_new_tube (self, type, initiator_handle,
                  service, parameters, stream_id, tube_id, NULL);

              tp_channel_manager_emit_new_channel (priv->conn->muc_factory,
                  TP_EXPORTABLE_CHANNEL (tube), NULL);

              /* the tube has reffed its initiator, no need to keep a ref */
              tp_handle_unref (contact_repo, initiator_handle);
              g_hash_table_destroy (parameters);
            }
        }
      else
        {
          /* The contact is in the tube.
           * Remove it from old_dbus_tubes if needed */
          g_hash_table_remove (old_dbus_tubes, GUINT_TO_POINTER (tube_id));
        }

      if (tube == NULL)
        /* skip to the next child of <tubes> */
        continue;

      g_object_get (tube, "type", &type, NULL);

      if (type == TP_TUBE_TYPE_DBUS)
        {
          /* Update mapping of handle -> D-Bus name. */
          if (!gabble_tube_dbus_handle_in_names (GABBLE_TUBE_DBUS (tube),
                contact))
            {
              /* Contact just joined the tube */
              const gchar *new_name;

              new_name = lm_message_node_get_attribute (tube_node,
                  "dbus-name");

              if (!new_name)
                {
                  DEBUG ("Contact %u isn't announcing their D-Bus name",
                         contact);
                  /* skip to the next child of <tubes> */
                  continue;
                }

              add_name_in_dbus_names (self, tube_id, contact, new_name);
            }
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
  GabbleTubeIface *tube = (GabbleTubeIface *) value;
  guint tube_id = GPOINTER_TO_UINT (key);
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  GabbleTubeChannelState state;
  TpTubeType type;
  GPtrArray *array = (GPtrArray *) user_data;
  GValue entry = {0,};

  g_object_get (tube,
                "state", &state,
                NULL);

  /* The old interface has no way to represent unoffered tubes, so they
   * shouldn't appear in the result of ListTubes()
   */
  if (state == GABBLE_TUBE_CHANNEL_STATE_NOT_OFFERED)
    return;

  g_object_get (tube,
                "type", &type,
                "initiator-handle", &initiator,
                "service", &service,
                "parameters", &parameters,
                NULL);

  g_value_init (&entry, TP_STRUCT_TYPE_TUBE_INFO);
  g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (TP_STRUCT_TYPE_TUBE_INFO));
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
  type = TP_TUBE_TYPE_STREAM;
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
    g_boxed_free (TP_STRUCT_TYPE_TUBE_INFO, ret->pdata[i]);

  g_ptr_array_free (ret, TRUE);
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
  GabbleTubeIface *tube = (GabbleTubeIface *) value;
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (
      data->self);
  TpTubeState state;
  LmMessageNode *tube_node;
  TpTubeType type;
  TpHandle initiator;

  if (tube == NULL)
    return;

  g_object_get (tube,
      "state", &state,
      "type", &type,
      "initiator-handle", &initiator,
       NULL);

  if (state != TP_TUBE_STATE_OPEN)
    return;

  if (type == TP_TUBE_TYPE_STREAM && initiator != priv->self_handle)
    /* We only announce stream tubes we initiated */
    return;

  tube_node = lm_message_node_add_child (data->tubes_node, "tube", NULL);
  gabble_tube_iface_publish_in_node (tube, (TpBaseConnection *) priv->conn,
      tube_node);
}

static void
pre_presence_cb (GabbleMucChannel *muc,
                 LmMessage *msg,
                 GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  struct _i_hate_g_hash_table_foreach data;
  LmMessageNode *node;

  /* Augment the muc presence with tubes information */
  node = lm_message_node_add_child (msg->node, "tubes", NULL);
  lm_message_node_set_attribute (node, "xmlns", NS_TUBES);
  data.self = self;
  data.tubes_node = node;

  g_hash_table_foreach (priv->tubes, publish_tubes_in_node, &data);
}

static gboolean
update_tubes_presence (GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->handle_type != TP_HANDLE_TYPE_ROOM)
    return FALSE;

  return gabble_muc_channel_send_presence (self->muc, NULL);
}

/* Called when we receive a SI request,
 * via gabble_tubes_factory_handle_si_tube_request
 */
void
gabble_tubes_channel_tube_si_offered (GabbleTubesChannel *self,
                                      GabbleBytestreamIface *bytestream,
                                      LmMessage *msg)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  const gchar *service, *stream_id;
  GHashTable *parameters;
  TpTubeType type;
  LmMessageNode *si_node, *tube_node;
  guint tube_id;
  GabbleTubeIface *tube;

  /* Caller is expected to have checked that we have a SI node with
   * a stream ID, the TUBES profile and a <tube> element
   */
  g_return_if_fail (lm_message_get_type (msg) == LM_MESSAGE_TYPE_IQ);
  g_return_if_fail (lm_message_get_sub_type (msg) == LM_MESSAGE_SUB_TYPE_SET);
  si_node = lm_message_node_get_child_with_namespace (msg->node, "si",
      NS_SI);
  g_return_if_fail (si_node != NULL);
  stream_id = lm_message_node_get_attribute (si_node, "id");
  g_return_if_fail (stream_id != NULL);
  tube_node = lm_message_node_get_child_with_namespace (si_node, "tube",
      NS_TUBES);
  g_return_if_fail (tube_node != NULL);

  if (!extract_tube_information (self, tube_node, NULL, NULL,
              NULL, NULL, &tube_id))
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "<tube> has no id attribute" };

      NODE_DEBUG (tube_node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube != NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "tube ID already in use" };

      NODE_DEBUG (tube_node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  /* New tube */
  if (!extract_tube_information (self, tube_node, &type, NULL,
              &service, &parameters, NULL))
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "can't extract <tube> information from SI request" };

      NODE_DEBUG (tube_node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      g_hash_table_destroy (parameters);
      return;
    }

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_FORBIDDEN,
          "Only D-Bus tubes are allowed to be created using SI" };

      DEBUG ("%s", e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  tube = create_new_tube (self, type, priv->handle, service,
      parameters, stream_id, tube_id, (GabbleBytestreamIface *) bytestream);

  tp_channel_manager_emit_new_channel (priv->conn->private_tubes_factory,
      TP_EXPORTABLE_CHANNEL (tube), NULL);

  g_hash_table_destroy (parameters);
}

/* Called when we receive a SI request,
 * via either gabble_muc_factory_handle_si_stream_request or
 * gabble_tubes_factory_handle_si_stream_request
 */
void
gabble_tubes_channel_bytestream_offered (GabbleTubesChannel *self,
                                         GabbleBytestreamIface *bytestream,
                                         LmMessage *msg)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  const gchar *stream_id, *tmp;
  gchar *endptr;
  LmMessageNode *si_node, *stream_node;
  guint tube_id;
  unsigned long tube_id_tmp;
  GabbleTubeIface *tube;

  /* Caller is expected to have checked that we have a stream or muc-stream
   * node with a stream ID and the TUBES profile
   */
  g_return_if_fail (lm_message_get_type (msg) == LM_MESSAGE_TYPE_IQ);
  g_return_if_fail (lm_message_get_sub_type (msg) == LM_MESSAGE_SUB_TYPE_SET);

  si_node = lm_message_node_get_child_with_namespace (msg->node, "si",
      NS_SI);
  g_return_if_fail (si_node != NULL);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    stream_node = lm_message_node_get_child_with_namespace (si_node,
        "stream", NS_TUBES);
  else
    stream_node = lm_message_node_get_child_with_namespace (si_node,
        "muc-stream", NS_TUBES);
  g_return_if_fail (stream_node != NULL);

  stream_id = lm_message_node_get_attribute (si_node, "id");
  g_return_if_fail (stream_id != NULL);

  tmp = lm_message_node_get_attribute (stream_node, "tube");
  if (tmp == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "<stream> or <muc-stream> has no tube attribute" };

      NODE_DEBUG (stream_node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }
  tube_id_tmp = strtoul (tmp, &endptr, 10);
  if (!endptr || *endptr || tube_id_tmp > G_MAXUINT32)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "<stream> or <muc-stream> tube attribute not numeric or > 2**32" };

      DEBUG ("tube id is not numeric or > 2**32: %s", tmp);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }
  tube_id = (guint) tube_id_tmp;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "<stream> or <muc-stream> tube attribute points to a nonexistent "
          "tube" };

      DEBUG ("tube %u doesn't exist", tube_id);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  DEBUG ("received new bytestream request for existing tube: %u", tube_id);

  gabble_tube_iface_add_bytestream (tube, bytestream);
}


static void
send_tube_close_msg (GabbleTubesChannel *self,
                     guint tube_id)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  LmMessage *msg;
  const gchar *jid;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  gchar *id_str;

  jid = tp_handle_inspect (contact_repo, priv->handle);
  id_str = g_strdup_printf ("%u", tube_id);

  /* Send the close message */
  msg = lm_message_build (jid, LM_MESSAGE_TYPE_MESSAGE,
      '(', "close", "",
        '@', "xmlns", NS_TUBES,
        '@', "tube", id_str,
      ')',
      '(', "amp", "",
        '@', "xmlns", NS_AMP,
        '(', "rule", "",
          '@', "condition", "deliver-at",
          '@', "value", "stored",
          '@', "action", "error",
        ')',
        '(', "rule", "",
          '@', "condition", "match-resource",
          '@', "value", "exact",
          '@', "action", "error",
        ')',
      ')',
      NULL);
  g_free (id_str);

  _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);
}

static void
tube_msg_offered (GabbleTubesChannel *self,
                  LmMessage *msg)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  const gchar *service;
  GHashTable *parameters;
  TpTubeType type;
  LmMessageNode *tube_node;
  guint tube_id;
  GabbleTubeIface *tube;

  g_return_if_fail (lm_message_get_type (msg) == LM_MESSAGE_TYPE_MESSAGE);
  tube_node = lm_message_node_get_child_with_namespace (msg->node, "tube",
      NS_TUBES);
  g_return_if_fail (tube_node != NULL);

  if (!extract_tube_information (self, tube_node, NULL, NULL,
              NULL, NULL, &tube_id))
    {
      DEBUG ("<tube> has no id attribute");
      /* We can't send a close message as reply so just ignore it */
      return;
    }

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube != NULL)
    {
      DEBUG ("tube ID already in use. Do not open the offered tube and close "
          "the existing tube id %u", tube_id);
      gabble_tube_iface_close (tube, FALSE);
      return;
    }

  /* New tube */
  if (!extract_tube_information (self, tube_node, &type, NULL,
              &service, &parameters, NULL))
    {
      DEBUG ("can't extract <tube> information from message");
      send_tube_close_msg (self, tube_id);
      return;
    }

  if (type != TP_TUBE_TYPE_STREAM)
    {
      DEBUG ("Only stream tubes are allowed to be created using messages");
      send_tube_close_msg (self, tube_id);
      return;
    }

  tube = create_new_tube (self, type, priv->handle, service,
      parameters, NULL, tube_id, NULL);

  tp_channel_manager_emit_new_channel (priv->conn->private_tubes_factory,
      TP_EXPORTABLE_CHANNEL (tube), NULL);

  g_hash_table_destroy (parameters);
}

static void
tube_msg_close (GabbleTubesChannel *self,
                LmMessage *msg)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  LmMessageNode *close_node;
  guint tube_id;
  const gchar *tmp;
  gchar *endptr;
  GabbleTubeIface *tube;
  TpTubeType type;

  close_node = lm_message_node_get_child_with_namespace (msg->node, "close",
      NS_TUBES);
  g_assert (close != NULL);

  tmp = lm_message_node_get_attribute (close_node, "tube");
  if (tmp == NULL)
    {
      DEBUG ("no tube id in close message");
      return;
    }

  tube_id = (guint) strtoul (tmp, &endptr, 10);
  if (!endptr || *endptr || tube_id > G_MAXUINT32)
    {
      DEBUG ("tube id is not numeric or > 2**32: %s", tmp);
      return;
    }

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    {
      DEBUG ("<close> tube attribute points to a nonexistent tube");
      return;
    }

  g_object_get (tube, "type", &type, NULL);
  if (type != TP_TUBE_TYPE_STREAM)
    {
      DEBUG ("Only stream tubes can be closed using a close message");
      return;
    }

  DEBUG ("tube %u was closed by remote peer", tube_id);
  gabble_tube_iface_close (tube, TRUE);
}

void
gabble_tubes_channel_tube_msg (GabbleTubesChannel *self,
                               LmMessage *msg)
{
  LmMessageNode *node;

  node = lm_message_node_get_child_with_namespace (msg->node, "tube",
      NS_TUBES);
  if (node != NULL)
    {
      tube_msg_offered (self, msg);
      return;
    }

  node = lm_message_node_get_child_with_namespace (msg->node, "close",
      NS_TUBES);
  if (node != NULL)
    {
      tube_msg_close (self, msg);
      return;
    }
}

static guint
generate_tube_id (void)
{
  /* We don't generate IDs in the top half of the range, to be nice to
   * older Gabble versions. */
  return g_random_int_range (0, G_MAXINT);
}

GabbleTubeIface *gabble_tubes_channel_tube_request (GabbleTubesChannel *self,
    gpointer request_token, GHashTable *request_properties,
    gboolean require_new)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeIface *tube;
  const gchar *channel_type;
  const gchar *service;
  GHashTable *parameters = NULL;
  guint tube_id;
  gchar *stream_id;
  TpTubeType type;

  tube_id = generate_tube_id ();

  channel_type = tp_asv_get_string (request_properties,
            TP_IFACE_CHANNEL ".ChannelType");

  if (! tp_strdiff (channel_type, GABBLE_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      type = TP_TUBE_TYPE_STREAM;
      service = tp_asv_get_string (request_properties,
                GABBLE_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service");

    }
  else if (! tp_strdiff (channel_type, GABBLE_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      type = TP_TUBE_TYPE_DBUS;
      service = tp_asv_get_string (request_properties,
                GABBLE_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName");
    }
  else
    /* This assertion is safe: this function's caller only calls it in one of
     * the above cases.
     * FIXME: but it would be better to pass an enum member or something maybe.
     */
    g_assert_not_reached ();

  parameters = tp_asv_get_boxed (request_properties,
               GABBLE_IFACE_CHANNEL_INTERFACE_TUBE ".Parameters",
               TP_HASH_TYPE_STRING_VARIANT_MAP);
  if (parameters == NULL)
    {
      /* If it is not included in the request, the connection manager MUST
       * consider the property to be empty. */
      parameters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
          (GDestroyNotify) tp_g_value_slice_free);
    }

  /* if the service property is missing, the requestotron rejects the request
   */
  g_assert (service != NULL);

  DEBUG ("Request a tube channel with type='%s' and service='%s'",
      channel_type, service);

  stream_id = gabble_bytestream_factory_generate_stream_id ();
  tube = create_new_tube (self, type, priv->self_handle, service,
      parameters, stream_id, tube_id, NULL);
  g_free (stream_id);

  return tube;
}

/**
 * gabble_tubes_channel_offer_d_bus_tube
 *
 * Implements D-Bus method OfferDBusTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_offer_d_bus_tube (TpSvcChannelTypeTubes *iface,
                                       const gchar *service,
                                       GHashTable *parameters,
                                       DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  GabbleTubeIface *tube;
  gchar *stream_id;
  GError *error = NULL;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection *) priv->conn;

  stream_id = gabble_bytestream_factory_generate_stream_id ();
  tube_id = generate_tube_id ();

  tube = create_new_tube (self, TP_TUBE_TYPE_DBUS, priv->self_handle,
      service, parameters, (const gchar *) stream_id, tube_id, NULL);

  if (!gabble_tube_dbus_offer (GABBLE_TUBE_DBUS (tube), &error))
    {
      gabble_tube_iface_close (tube, TRUE);
      dbus_g_method_return_error (context, error);

      g_error_free (error);
      g_free (stream_id);
      return;
    }

  tp_svc_channel_type_tubes_return_from_offer_d_bus_tube (context, tube_id);

  g_free (stream_id);
}

static void
stream_unix_tube_new_connection_cb (GabbleTubeIface *tube,
                                    guint contact,
                                    gpointer user_data)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (user_data);
  guint tube_id;
  TpTubeType type;

  g_object_get (tube,
      "id", &tube_id,
      "type", &type,
      NULL);

  g_assert (type == TP_TUBE_TYPE_STREAM);

  tp_svc_channel_type_tubes_emit_stream_tube_new_connection (self,
      tube_id, contact);
}

/**
 * gabble_tubes_channel_offer_stream_tube
 *
 * Implements D-Bus method OfferStreamTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_offer_stream_tube (TpSvcChannelTypeTubes *iface,
                                        const gchar *service,
                                        GHashTable *parameters,
                                        guint address_type,
                                        const GValue *address,
                                        guint access_control,
                                        const GValue *access_control_param,
                                        DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  GabbleTubeIface *tube;
  gchar *stream_id;
  GError *error = NULL;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection *) priv->conn;

  if (!gabble_tube_stream_check_params (address_type, address,
        access_control, access_control_param, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  stream_id = gabble_bytestream_factory_generate_stream_id ();
  tube_id = generate_tube_id ();

  tube = create_new_tube (self, TP_TUBE_TYPE_STREAM, priv->self_handle,
      service, parameters, (const gchar *) stream_id, tube_id, NULL);

  g_object_set (tube,
      "address-type", address_type,
      "address", address,
      "access-control", access_control,
      "access-control-param", access_control_param,
      NULL);

  /* Tube was created using the old API so is already offered */
  if (!gabble_tube_stream_offer (GABBLE_TUBE_STREAM (tube), &error))
    {
      gabble_tube_iface_close (tube, TRUE);

      dbus_g_method_return_error (context, error);

      g_error_free (error);
      g_free (stream_id);
      return;
    }

  g_signal_connect (tube, "tube-new-connection",
      G_CALLBACK (stream_unix_tube_new_connection_cb), self);

  /* announce the new tube channel we just created (new tube API) */
  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      tp_channel_manager_emit_new_channel (priv->conn->private_tubes_factory,
          TP_EXPORTABLE_CHANNEL (tube), NULL);
    }
  else
    {
      tp_channel_manager_emit_new_channel (priv->conn->muc_factory,
          TP_EXPORTABLE_CHANNEL (tube), NULL);
    }

  tp_svc_channel_type_tubes_return_from_offer_stream_tube (context,
      tube_id);

  g_free (stream_id);
}

/**
 * gabble_tubes_channel_accept_d_bus_tube
 *
 * Implements D-Bus method AcceptDBusTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_accept_d_bus_tube (TpSvcChannelTypeTubes *iface,
                                        guint id,
                                        DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  GabbleTubeIface *tube;
  TpTubeState state;
  TpTubeType type;
  gchar *addr;
  GError *error = NULL;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  if (state != TP_TUBE_STATE_LOCAL_PENDING)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the local pending state" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  if (!gabble_tube_iface_accept (tube, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  update_tubes_presence (self);

  add_yourself_in_dbus_names (self, id);

  g_object_get (tube, "dbus-address", &addr, NULL);
  tp_svc_channel_type_tubes_return_from_accept_d_bus_tube (context, addr);
  g_free (addr);
}

/**
 * gabble_tubes_channel_accept_stream_tube
 *
 * Implements D-Bus method AcceptStreamTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_accept_stream_tube (TpSvcChannelTypeTubes *iface,
                                         guint id,
                                         guint address_type,
                                         guint access_control,
                                         const GValue *access_control_param,
                                         DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  GabbleTubeIface *tube;
  TpTubeState state;
  TpTubeType type;
  GValue *address;
  GError *error = NULL;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_STREAM)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a stream tube" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  /* most parameters sanity checks are done in gabble_tube_stream_accept,
   * but at least check that they fit the properties requirements */
  if (address_type != TP_SOCKET_ADDRESS_TYPE_UNIX &&
      address_type != TP_SOCKET_ADDRESS_TYPE_IPV4 &&
      address_type != TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Address type not implemented" };

      dbus_g_method_return_error (context, &e);
      return;
    }
  g_object_set (tube,
      "address-type", address_type,
      "access-control", access_control,
      "access-control-param", access_control_param,
      NULL);

  if (!gabble_tube_iface_accept (tube, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  update_tubes_presence (self);

  g_object_get (tube, "address", &address, NULL);

  tp_svc_channel_type_tubes_return_from_accept_stream_tube (context,
      address);
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
  GabbleTubeIface *tube;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  gabble_tube_iface_close (tube, FALSE);

  tp_svc_channel_type_tubes_return_from_close_tube (context);
}

/**
 * gabble_tubes_channel_get_d_bus_tube_address
 *
 * Implements D-Bus method GetDBusTubeAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_get_d_bus_tube_address (TpSvcChannelTypeTubes *iface,
                                             guint id,
                                             DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  GabbleTubeIface *tube;
  gchar *addr;
  TpTubeType type;
  TpTubeState state;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-address", &addr, NULL);
  tp_svc_channel_type_tubes_return_from_get_d_bus_tube_address (context,
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

  g_value_init (&tmp, TP_STRUCT_TYPE_DBUS_TUBE_MEMBER);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (TP_STRUCT_TYPE_DBUS_TUBE_MEMBER));
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
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeIface *tube;
  GHashTable *names;
  GPtrArray *ret;
  TpTubeType type;
  TpTubeState state;
  guint i;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_DBUS ||
      priv->handle_type != TP_HANDLE_TYPE_ROOM)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a Muc D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != TP_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-names", &names, NULL);
  g_assert (names);

  ret = g_ptr_array_sized_new (g_hash_table_size (names));
  g_hash_table_foreach (names, get_d_bus_names_foreach, ret);

  tp_svc_channel_type_tubes_return_from_get_d_bus_names (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (TP_STRUCT_TYPE_DBUS_TUBE_MEMBER, ret->pdata[i]);
  g_hash_table_unref (names);
  g_ptr_array_free (ret, TRUE);
}

/**
 * gabble_tubes_channel_get_stream_tube_socket_address
 *
 * Implements D-Bus method GetStreamTubeSocketAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_get_stream_tube_socket_address (TpSvcChannelTypeTubes *iface,
                                                     guint id,
                                                     DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv  = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeIface *tube;
  TpTubeType type;
  TpTubeState state;
  TpSocketAddressType address_type;
  GValue *address;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_STREAM)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a Stream tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != TP_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "address-type", &address_type,
      "address", &address,
      NULL);

  tp_svc_channel_type_tubes_return_from_get_stream_tube_socket_address (
      context, address_type, address);
}

/**
 * gabble_tubes_channel_get_available_stream_tube_types
 *
 * Implements D-Bus method GetAvailableStreamTubeTypes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_get_available_stream_tube_types (TpSvcChannelTypeTubes *iface,
                                                      DBusGMethodInvocation *context)
{
  GHashTable *ret;

  ret = gabble_tube_stream_get_supported_socket_types ();

  tp_svc_channel_type_tubes_return_from_get_available_stream_tube_types (
      context, ret);

  g_hash_table_destroy (ret);
}

static void
emit_tube_closed_signal (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  guint id = GPOINTER_TO_UINT (key);
  GabbleTubesChannel *self = (GabbleTubesChannel *) user_data;

  tp_svc_channel_type_tubes_emit_tube_closed (self, id);
}

void
gabble_tubes_channel_close (GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  DEBUG ("called on %p", self);

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->closed)
    {
      return;
    }

  priv->closed = TRUE;

  g_hash_table_foreach (priv->tubes, emit_tube_closed_signal, self);
  g_hash_table_destroy (priv->tubes);

  priv->tubes = NULL;

  tp_svc_channel_emit_closed (self);
}

/**
 * gabble_tubes_channel_close_async:
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_tubes_channel_close_async (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  gabble_tubes_channel_close (self);
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
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);

  if (self->muc)
    {
      tp_svc_channel_return_from_get_interfaces (context,
          gabble_tubes_channel_interfaces);
    }
  else
    {
      /* omit the Group interface */
      tp_svc_channel_return_from_get_interfaces (context,
          gabble_tubes_channel_interfaces + 1);
    }
}

static void gabble_tubes_channel_dispose (GObject *object);
static void gabble_tubes_channel_finalize (GObject *object);

static void
gabble_tubes_channel_class_init (
    GabbleTubesChannelClass *gabble_tubes_channel_class)
{
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
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { NULL }
  };
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

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this Tubes channel object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_object (
      "muc",
      "GabbleMucChannel object",
      "Gabble text MUC channel corresponding to this Tubes channel object, "
      "if the handle type is ROOM.",
      GABBLE_TYPE_MUC_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MUC, param_spec);

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "The string obtained by inspecting the target handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

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

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  gabble_tubes_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleTubesChannelClass, dbus_props_class));

  tp_external_group_mixin_init_dbus_properties (object_class);
}

void
gabble_tubes_channel_dispose (GObject *object)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (object);
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, priv->handle_type);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, priv->handle);

  if (self->muc != NULL)
    {
      g_signal_handler_disconnect (self->muc, priv->pre_presence_signal);

      tp_external_group_mixin_finalize (object);
    }
  gabble_tubes_channel_close (self);

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
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  g_free (priv->object_path);

  if (priv->initiator != 0)
    tp_handle_unref (contact_handles, priv->initiator);

  G_OBJECT_CLASS (gabble_tubes_channel_parent_class)->finalize (object);
}

static void
tubes_iface_init (gpointer g_iface,
                  gpointer iface_data)
{
  TpSvcChannelTypeTubesClass *klass = (TpSvcChannelTypeTubesClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_tubes_implement_##x (\
    klass, gabble_tubes_channel_##x)
  IMPLEMENT(get_available_tube_types);
  IMPLEMENT(list_tubes);
  IMPLEMENT(close_tube);
  IMPLEMENT(offer_d_bus_tube);
  IMPLEMENT(accept_d_bus_tube);
  IMPLEMENT(get_d_bus_tube_address);
  IMPLEMENT(get_d_bus_names);
  IMPLEMENT(offer_stream_tube);
  IMPLEMENT(accept_stream_tube);
  IMPLEMENT(get_stream_tube_socket_address);
  IMPLEMENT(get_available_stream_tube_types);
#undef IMPLEMENT
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_tubes_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}
