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

#include "tubes-channel.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "debug.h"
#include "gabble-connection.h"
#include "presence.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "util.h"
#include "tube-iface.h"
#include "tube-stream.h"
#include "bytestream-factory.h"

#ifdef HAVE_DBUS_TUBE
#include "tube-dbus.h"
#endif

#include <telepathy-glib/errors.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-unstable.h>

#define GABBLE_CHANNEL_TUBE_TYPE \
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
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_external_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_MUC,
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

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_TUBES_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleTubesChannelPrivate *) obj->priv)

static gboolean update_tubes_presence (GabbleTubesChannel *self);

static void
gabble_tubes_channel_init (GabbleTubesChannel *self)
{
  GabbleTubesChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBES_CHANNEL, GabbleTubesChannelPrivate);

  self->priv = priv;

  priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);

  self->muc = NULL;

  priv->dispose_has_run = FALSE;
  priv->closed = FALSE;
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
  TpHandleRepoIface *handle_repo;

  DEBUG ("Called");

  obj = G_OBJECT_CLASS (gabble_tubes_channel_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_TUBES_CHANNEL (obj);
  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, priv->handle_type);

  tp_handle_ref (handle_repo, priv->handle);

  switch (priv->handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      g_assert (self->muc == NULL);
      priv->self_handle = ((TpBaseConnection *) (priv->conn))->self_handle;
      break;
    case TP_HANDLE_TYPE_ROOM:
      g_assert (self->muc != NULL);
      priv->self_handle = self->muc->group.self_handle;
      tp_external_group_mixin_init (obj, (GObject *) self->muc);
      break;
    default:
      g_assert_not_reached ();
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
      case PROP_MUC:
        g_value_set_object (value, chan->muc);
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
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

#ifdef HAVE_DBUS_TUBE
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

static void
add_name_in_dbus_names (GabbleTubesChannel *self,
                        guint tube_id,
                        TpHandle handle,
                        const gchar *dbus_name)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubeDBus *tube;
  GHashTable *names;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  g_object_get (tube,
      "dbus-names", &names,
      NULL);

  g_hash_table_insert (names, GUINT_TO_POINTER (handle), g_strdup (dbus_name));
  tp_handle_ref (contact_repo, handle);

  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_added (self, tube_id, handle, dbus_name);

  g_hash_table_unref (names);
}

static void
add_yourself_in_dbus_names (GabbleTubesChannel *self,
                            guint tube_id)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeDBus *tube;
  gchar *dbus_name;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  g_object_get (tube,
      "dbus-name", &dbus_name,
      NULL);

  add_name_in_dbus_names (self, tube_id, priv->self_handle, dbus_name);

  g_free (dbus_name);
}
#endif

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

#ifdef HAVE_DBUS_TUBE
  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_removed (self, tube_id, priv->self_handle);
#endif

  update_tubes_presence (self);

  tp_svc_channel_type_tubes_emit_tube_closed (self, tube_id);
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
create_new_tube (GabbleTubesChannel *self,
                 TpTubeType type,
                 TpHandle initiator,
                 const gchar *service,
                 GHashTable *parameters,
                 const gchar *stream_id,
                 guint tube_id)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeIface *tube;
  TpTubeState state;

  switch (type)
    {
#ifdef HAVE_DBUS_TUBE
    case TP_TUBE_TYPE_DBUS:
      tube = GABBLE_TUBE_IFACE (gabble_tube_dbus_new (priv->conn,
          priv->handle, priv->handle_type, priv->self_handle, initiator,
          service, parameters, stream_id, tube_id));
      break;
#endif
    case TP_TUBE_TYPE_STREAM_UNIX:
      tube = GABBLE_TUBE_IFACE (gabble_tube_stream_new (priv->conn,
          priv->handle, priv->handle_type, priv->self_handle, initiator,
          service, parameters, tube_id));
      break;
    default:
      g_assert_not_reached ();
    }

  DEBUG ("create tube %u", tube_id);
  g_hash_table_insert (priv->tubes, GUINT_TO_POINTER (tube_id), tube);
  update_tubes_presence (self);

  g_object_get (tube, "state", &state, NULL);

  tp_svc_channel_type_tubes_emit_new_tube (self,
      tube_id,
      initiator,
      type,
      service,
      parameters,
      state);

#ifdef HAVE_DBUS_TUBE
  if (type == TP_TUBE_TYPE_DBUS &&
      state != TP_TUBE_STATE_LOCAL_PENDING)
    {
      add_yourself_in_dbus_names (self, tube_id);
    }
#endif

  g_signal_connect (tube, "opened", G_CALLBACK (tube_opened_cb), self);
  g_signal_connect (tube, "closed", G_CALLBACK (tube_closed_cb), self);
}

static gboolean
extract_tube_information (GabbleTubesChannel *self,
                          LmMessageNode *tube_node,
                          TpTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters,
                          gboolean *offering,
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
          *type = TP_TUBE_TYPE_STREAM_UNIX;
        }
#ifdef HAVE_DBUS_TUBE
      else if (!tp_strdiff (_type, "dbus"))
        {
          *type = TP_TUBE_TYPE_DBUS;
        }
#endif
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
      *initiator_handle = tp_handle_ensure (contact_repo, initiator,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

      if (*initiator_handle == 0)
        {
          DEBUG ("invalid initiator JID %s", initiator);
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
      *parameters = lm_message_node_extract_properties (node, "parameter");
    }

  if (offering != NULL)
    {
      const gchar *_offering;

      _offering = lm_message_node_get_attribute (tube_node, "offering");
      if (!tp_strdiff (_offering, "false"))
        *offering = FALSE;
      else
        *offering = TRUE;
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      gchar *endptr;
      long int tmp;

      str = lm_message_node_get_attribute (tube_node, "id");
      if (str == NULL)
        {
          DEBUG ("no tube id in SI request");
          return FALSE;
        }

      tmp = strtol (str, &endptr, 10);
      if (!endptr || *endptr)
        {
          DEBUG ("tube id is not numeric: %s", str);
          return FALSE;
        }
      *tube_id = (int) tmp;
    }

  return TRUE;
}

#ifdef HAVE_DBUS_TUBE
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

static void
contact_left_muc (GabbleTubesChannel *self,
                  TpHandle contact)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
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

#endif

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
#ifdef HAVE_DBUS_TUBE
  const gchar *presence_type;
  GHashTable *old_dbus_tubes;
  struct _add_in_old_dbus_tubes_data add_data;
  struct _emit_d_bus_names_changed_foreach_data emit_data;
#endif

  if (contact == priv->self_handle)
    /* We don't need to inspect our own presence */
    return;

  /* We are interested by this presence only if it contains tube information
   * or indicates someone left the muc */
#ifdef HAVE_DBUS_TUBE
  presence_type = lm_message_node_get_attribute (presence->node, "type");
  if (!tp_strdiff (presence_type, "unavailable"))
    {
      contact_left_muc (self, contact);
      return;
    }
#endif

  tubes_node = lm_message_node_get_child_with_namespace (presence->node,
      "tubes", NS_TUBES);

  if (tubes_node == NULL)
    return;

#ifdef HAVE_DBUS_TUBE
  /* Fill old_dbus_tubes with D-BUS tubes previoulsy announced by
   * the contact */
  old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
  add_data.old_dbus_tubes = old_dbus_tubes;
  add_data.contact = contact;
  g_hash_table_foreach (priv->tubes, add_in_old_dbus_tubes, &add_data);
#endif

  for (tube_node = tubes_node->children; tube_node != NULL;
      tube_node = tube_node->next)
    {
      const gchar *stream_id;
      GabbleTubeIface *tube;
      guint tube_id;
      TpTubeType type;

      stream_id = lm_message_node_get_attribute (tube_node, "stream-id");

      extract_tube_information (self, tube_node, NULL,
          NULL, NULL, NULL, NULL, &tube_id);
      tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

      if (tube == NULL)
        {
          /* We don't know yet this tube */
          const gchar *service;
          TpTubeType type;
          TpHandle initiator_handle;
          GHashTable *parameters;
          guint tube_id;

          if (extract_tube_information (self, tube_node, &type,
                &initiator_handle, &service, &parameters, NULL, &tube_id))
            {

#ifndef HAVE_DBUS_TUBE
              if (type == TP_TUBE_TYPE_DBUS)
                {
                  DEBUG ("Don't create the tube as D-Bus tube support"
                      "is not built");
                  continue;
                }
#endif

              create_new_tube (self, type, initiator_handle,
                  service, parameters, stream_id, tube_id);
              tube = g_hash_table_lookup (priv->tubes,
                  GUINT_TO_POINTER (tube_id));

              /* the tube has reffed its initiator, no need to keep a ref */
              tp_handle_unref (contact_repo, initiator_handle);
            }
        }
#ifdef HAVE_DBUS_TUBE
      else
        {
          /* The contact is in the tube.
           * Remove it from old_dbus_tubes if needed */
          g_hash_table_remove (old_dbus_tubes, GUINT_TO_POINTER (tube_id));
        }
#endif

      if (tube == NULL)
        continue;

      g_object_get (tube, "type", &type, NULL);

#ifdef HAVE_DBUS_TUBE
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
              const gchar *new_name;

              new_name = lm_message_node_get_attribute (tube_node,
                  "dbus-name");

              if (!new_name)
                {
                  DEBUG ("Contact %u isn't announcing their D-Bus name",
                         contact);
                  continue;
                }

              add_name_in_dbus_names (self, tube_id, contact, new_name);
            }

          g_hash_table_unref (names);
        }
#endif
    }

#ifdef HAVE_DBUS_TUBE
  /* Tubes remaining in old_dbus_tubes was left by the contact */
  emit_data.contact = contact;
  emit_data.self = self;
  g_hash_table_foreach (old_dbus_tubes, emit_d_bus_names_changed_foreach,
      &emit_data);

  g_hash_table_destroy (old_dbus_tubes);
#endif
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

  g_value_init (&entry, GABBLE_CHANNEL_TUBE_TYPE);
  g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (GABBLE_CHANNEL_TUBE_TYPE));
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
#ifdef HAVE_DBUS_TUBE
  type = TP_TUBE_TYPE_DBUS;
  g_array_append_val (ret, type);
#endif
  type = TP_TUBE_TYPE_STREAM_UNIX;
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
    g_boxed_free (GABBLE_CHANNEL_TUBE_TYPE, ret->pdata[i]);

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
publish_tube_in_node (GabbleTubesChannel *self,
                      LmMessageNode *node,
                      GabbleTubeIface *tube)
{
  LmMessageNode *parameters_node;
  GHashTable *parameters;
  TpTubeType type;
  gchar *service, *id_str;
  guint tube_id;
  GabbleTubesChannelPrivate *priv =
      GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle initiator_handle;
  const gchar *initiator;

  g_object_get (G_OBJECT (tube),
      "service", &service,
      "parameters", &parameters,
      "type", &type,
      "id", &tube_id,
      "initiator", &initiator_handle,
      NULL);

  id_str = g_strdup_printf ("%u", tube_id);
  initiator = tp_handle_inspect (contact_repo, initiator_handle);

  lm_message_node_set_attributes (node,
      "service", service,
      "id", id_str,
      "initiator", initiator,
      NULL);

  g_free (id_str);

  switch (type)
    {
      case TP_TUBE_TYPE_DBUS:
        lm_message_node_set_attribute (node, "type", "dbus");
        break;
      case TP_TUBE_TYPE_STREAM_UNIX:
        lm_message_node_set_attribute (node, "type", "stream");
        break;
      default:
        g_assert_not_reached ();
    }

  if (type == TP_TUBE_TYPE_DBUS)
    {
      gchar *name, *stream_id;

      g_object_get (G_OBJECT (tube),
          "dbus-name", &name,
          "stream-id", &stream_id,
          NULL);

      lm_message_node_set_attributes (node,
          "dbus-name", name,
          "stream-id", stream_id,
          NULL);

      g_free (name);
      g_free (stream_id);
    }

  parameters_node = lm_message_node_add_child (node, "parameters",
      NULL);
  lm_message_node_add_children_from_properties (parameters_node, parameters,
      "parameter");

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
  GabbleTubeIface *tube = (GabbleTubeIface *) value;
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  TpTubeState state;
  LmMessageNode *tube_node;

  if (tube == NULL)
    return;

  g_object_get (tube,
                "state", &state,
                NULL);

  if (state != TP_TUBE_STATE_OPEN)
    return;

  tube_node = lm_message_node_add_child (data->tubes_node, "tube", NULL);
  publish_tube_in_node (data->self, tube_node, tube);
}

static gboolean
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
  gboolean result;

  if (priv->handle_type != TP_HANDLE_TYPE_ROOM)
    return FALSE;

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

  g_hash_table_foreach (priv->tubes, publish_tubes_in_node, &data);

  /* Send it */
  result = _gabble_connection_send (priv->conn, msg, NULL);

  g_free (username);
  g_free (to);
  lm_message_unref (msg);
  return result;
}

struct _bytestream_negotiate_cb_data
{
  GabbleTubesChannel *self;
  GabbleTubeIface *tube;
};

static void
bytestream_negotiate_cb (GabbleBytestreamIBB *bytestream,
                         const gchar *stream_id,
                         LmMessage *msg,
                         gpointer user_data)
{
  struct _bytestream_negotiate_cb_data *data =
    (struct _bytestream_negotiate_cb_data *) user_data;
#ifdef HAVE_DBUS_TUBE
  GabbleTubesChannel *self = data->self;
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
#endif
  GabbleTubeIface *tube = data->tube;
  LmMessageNode *si, *tube_node;
  TpTubeType type;

  g_slice_free (struct _bytestream_negotiate_cb_data, data);

  if (bytestream == NULL)
    {
      /* Tube was declined by remote user. Close it */
      gabble_tube_iface_close (tube);
      return;
    }

  /* Tube was accepted by remote user */

  g_object_set (tube,
      "bytestream", bytestream,
      NULL);

  gabble_tube_iface_accept (tube);

  /* Extract tube type specific information from SI reply */
  si = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  if (si == NULL)
    return;

  g_object_get (tube,
      "type", &type,
      NULL);

  tube_node = lm_message_node_get_child_with_namespace (si, "tube",
      NS_SI_TUBES);
  if (tube_node == NULL)
    tube_node = lm_message_node_get_child_with_namespace (si, "tube",
        NS_SI_TUBES_OLD);
  if (tube_node == NULL)
    return;

#ifdef HAVE_DBUS_TUBE
  if (type == TP_TUBE_TYPE_DBUS)
    {
      LmMessageNode *dbus_name_node;
      const gchar *dbus_name;
      guint tube_id;

      g_object_get (tube, "id", &tube_id, NULL);
      dbus_name_node = lm_message_node_get_child (tube_node, "dbus-name");
      dbus_name = lm_message_node_get_value (dbus_name_node);
      add_name_in_dbus_names (self, tube_id, priv->handle, dbus_name);
    }
#endif
}

/* Called when we receive a SI request */
gboolean
gabble_tubes_channel_tube_offered (GabbleTubesChannel *self,
                                   GabbleBytestreamIBB *bytestream,
                                   LmMessage *msg)
{
  GabbleTubesChannelPrivate *priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  const gchar *service, *stream_id;
#ifdef HAVE_DBUS_TUBE
  const gchar *dbus_name = NULL;
#endif
  GHashTable *parameters;
  TpTubeType type;
  LmMessageNode *si_node, *tube_node;
  guint tube_id;
  GabbleTubeIface *tube;
  gboolean offering;

  si_node = lm_message_node_get_child_with_namespace (msg->node, "si",
      NS_SI);
  if (si_node == NULL)
    {

      NODE_DEBUG (msg->node, "got a SI request without SI markup");
      return FALSE;
    }

  stream_id = lm_message_node_get_attribute (si_node, "id");
  if (stream_id == NULL)
    {
      NODE_DEBUG (msg->node, "got a SI request without stream ID");
      return FALSE;
    }

  tube_node = lm_message_node_get_child_with_namespace (si_node, "tube",
      NS_SI_TUBES);
  if (tube_node == NULL)
    tube_node = lm_message_node_get_child_with_namespace (si_node, "tube",
        NS_SI_TUBES_OLD);
  if (tube_node == NULL)
    {
      NODE_DEBUG (msg->node, "got a SI request without tube markup");
      return FALSE;
    }

  if (!extract_tube_information (self, tube_node, NULL, NULL,
              NULL, NULL, &offering, &tube_id))
    {
      DEBUG ("can't extract tube information in SI request");
      return FALSE;
    }

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube != NULL)
    {
      if (offering)
        {
          DEBUG ("received SI request for an existing tube but with the "
              "offering flag set to true. Tube rejected so.");
          return FALSE;
        }

        DEBUG ("received new SI request for existing tube: %u",
            tube_id);

        gabble_tube_iface_add_bytestream (tube, bytestream);
        return TRUE;
    }

  if (!offering)
    {
      DEBUG ("tube %d not found so we reject this SI as it's a not a tube "
          "offer", tube_id);
      return FALSE;
    }

  /* New tube */
  if (!extract_tube_information (self, tube_node, &type, NULL,
              &service, &parameters, NULL, NULL))
    {
      DEBUG ("can't extract tube information in SI request");
      return FALSE;
    }

  /* Type specific informations*/
#ifdef HAVE_DBUS_TUBE
  if (type == TP_TUBE_TYPE_DBUS)
    {
      dbus_name = lm_message_node_get_attribute (tube_node, "dbus-name");
      if (dbus_name == NULL)
        {
          DEBUG ("D-Bus tube doesn't contain initiator D-Bus name");
          return FALSE;
        }
    }
#endif

  create_new_tube (self, type, priv->handle, service,
      parameters, stream_id, tube_id);
  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

  g_object_set (tube, "bytestream", bytestream, NULL);

  if (type == TP_TUBE_TYPE_DBUS)
    {
#ifdef HAVE_DBUS_TUBE
      add_name_in_dbus_names (self, tube_id, priv->handle, dbus_name);
#else
      DEBUG ("Don't create the tube as D-Bus tube support"
          "is not built");

      return FALSE;
#endif
    }

  return TRUE;
}

static gboolean
start_stream_initiation (GabbleTubesChannel *self,
                         GabbleTubeIface *tube,
                         const gchar *stream_id,
                         GError **error)
{
  GabbleTubesChannelPrivate *priv;
  LmMessageNode *tube_node, *si_node;
  LmMessage *msg;
  TpHandleRepoIface *contact_repo;
  GabblePresence *presence;
  const gchar *jid, *resource;
  gchar *full_jid;
  gboolean result;
  struct _bytestream_negotiate_cb_data *data;

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);

  contact_repo = tp_base_connection_get_handles (
     (TpBaseConnection*) priv->conn, TP_HANDLE_TYPE_CONTACT);

  jid = tp_handle_inspect (contact_repo, priv->handle);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->handle);
  if (presence == NULL)
    {
      DEBUG ("can't find contacts's presence");
      if (error != NULL)
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "can't find contact's presence");

      return FALSE;
    }

  resource = gabble_presence_pick_resource_by_caps (presence,
      PRESENCE_CAP_SI_TUBES);
  if (resource == NULL)
    {
      DEBUG ("contact doesn't have tubes capabilities");
      if (error != NULL)
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "contact doesn't have tubes capabilities");

      return FALSE;
    }

  full_jid = g_strdup_printf ("%s/%s", jid, resource);

  msg = gabble_bytestream_factory_make_stream_init_iq (full_jid,
      stream_id, NS_SI_TUBES_OLD);

  si_node = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  g_assert (si_node != NULL);

  tube_node = lm_message_node_add_child (si_node, "tube", NULL);
  lm_message_node_set_attribute (tube_node, "xmlns", NS_SI_TUBES_OLD);
  publish_tube_in_node (self, tube_node, tube);
  lm_message_node_set_attribute (tube_node, "offering", "true");

  data = g_slice_new (struct _bytestream_negotiate_cb_data);
  data->self = self;
  data->tube = tube;

  result = gabble_bytestream_factory_negotiate_stream (
    priv->conn->bytestream_factory,
    msg,
    stream_id,
    bytestream_negotiate_cb,
    data,
    error);

  if (!result)
    g_slice_free (struct _bytestream_negotiate_cb_data, data);

  lm_message_unref (msg);
  g_free (full_jid);

  return result;
}

static gint
generate_tube_id (void)
{
  return g_random_int_range (0, G_MAXINT);
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
#ifdef HAVE_DBUS_TUBE
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  GabbleTubeIface *tube;
  GHashTable *parameters_copied;
  gchar *stream_id;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection*) priv->conn;

  parameters_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_foreach (parameters, copy_parameter, parameters_copied);

  stream_id = gabble_bytestream_factory_generate_stream_id ();
  tube_id = generate_tube_id ();

  create_new_tube (self, TP_TUBE_TYPE_DBUS, priv->self_handle,
      service, parameters_copied, (const gchar*) stream_id, tube_id);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Stream initiation */
      GError *error = NULL;

      if (!start_stream_initiation (self, tube, stream_id, &error))
        {
          gabble_tube_iface_close (tube);

          dbus_g_method_return_error (context, error);

          g_error_free (error);
          g_free (stream_id);
          return;
        }

    }

  tp_svc_channel_type_tubes_return_from_offer_d_bus_tube (context, tube_id);

  g_free (stream_id);
#else
  GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
      "D-Bus tube support not built" };

  dbus_g_method_return_error (context, &error);
  return;
#endif
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

  g_assert (type == TP_TUBE_TYPE_STREAM_UNIX);

  tp_svc_channel_type_tubes_emit_stream_unix_socket_new_connection (self,
      tube_id, contact);
}
/**
 * gabble_tubes_channel_offer_stream_unix_tube
 *
 * Implements D-Bus method OfferStreamUnixTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_offer_stream_unix_tube (TpSvcChannelTypeTubes *iface,
                                             const gchar *service,
                                             const gchar *socket,
                                             GHashTable *parameters,
                                             DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  GabbleTubeIface *tube;
  GHashTable *parameters_copied;
  gchar *stream_id;
  struct stat stat_buff;

  g_assert (GABBLE_IS_TUBES_CHANNEL (self));

  priv = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection*) priv->conn;

  if (g_stat (socket, &stat_buff) == -1)
    {
      GError *error = NULL;

      DEBUG ("Error calling stat on socket: %s", g_strerror (errno));

      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "%s: %s",
          socket, g_strerror (errno));

      dbus_g_method_return_error (context, error);

      g_error_free (error);
      return;
    }

  if (!S_ISSOCK (stat_buff.st_mode))
    {
      GError *error = NULL;

      DEBUG ("%s is not a socket", socket);

      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "%s is not a socket", socket);

      dbus_g_method_return_error (context, error);

      g_error_free (error);
      return;
    }

  parameters_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_foreach (parameters, copy_parameter, parameters_copied);

  stream_id = gabble_bytestream_factory_generate_stream_id ();
  tube_id = generate_tube_id ();

  create_new_tube (self, TP_TUBE_TYPE_STREAM_UNIX, priv->self_handle,
      service, parameters_copied, (const gchar*) stream_id, tube_id);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

  g_object_set (tube, "socket", socket, NULL);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Stream initiation */
      GError *error = NULL;

      if (!start_stream_initiation (self, tube, stream_id, &error))
        {
          gabble_tube_iface_close (tube);

          dbus_g_method_return_error (context, error);

          g_error_free (error);
          g_free (stream_id);
          return;
        }
    }

  g_signal_connect (tube, "new-connection",
      G_CALLBACK (stream_unix_tube_new_connection_cb), self);

  tp_svc_channel_type_tubes_return_from_offer_d_bus_tube (context, tube_id);

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
  GabbleTubeIface *tube;
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

  gabble_tube_iface_accept (tube);

  update_tubes_presence (self);

  g_object_get (tube, "type", &type, NULL);

#ifdef HAVE_DBUS_TUBE
  if (type == TP_TUBE_TYPE_DBUS)
    {
      add_yourself_in_dbus_names (self, id);
    }
#endif

  tp_svc_channel_type_tubes_return_from_accept_tube (context);
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

  gabble_tube_iface_close (tube);

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
#ifdef HAVE_DBUS_TUBE
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

  if (state != TP_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-address", &addr, NULL);
  tp_svc_channel_type_tubes_return_from_get_d_bus_server_address (context,
      addr);
  g_free (addr);

#else /* ! HAVE_DBUS_TUBE */

  GError error = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "D-Bus tube support not built" };

  dbus_g_method_return_error (context, &error);
#endif
}

#ifdef HAVE_DBUS_TUBE
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
#endif

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
#ifdef HAVE_DBUS_TUBE
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

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

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
    g_boxed_free (DBUS_NAME_PAIR_TYPE, ret->pdata[i]);
  g_hash_table_unref (names);
  g_ptr_array_free (ret, TRUE);

#else /* HAVE_DBUS_TUBE */

  GError error = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "D-Bus tube support not built" };

  dbus_g_method_return_error (context, &error);
#endif
}

/**
 * gabble_tubes_channel_get_stream_unix_socket_address
 *
 * Implements D-Bus method GetStreamSocketAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
gabble_tubes_channel_get_stream_unix_socket_address (TpSvcChannelTypeTubes *iface,
                                                     guint id,
                                                     DBusGMethodInvocation *context)
{
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);
  GabbleTubesChannelPrivate *priv  = GABBLE_TUBES_CHANNEL_GET_PRIVATE (self);
  GabbleTubeIface *tube;
  TpTubeType type;
  TpTubeState state;
  gchar *socket;

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

  if (type != TP_TUBE_TYPE_STREAM_UNIX)
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
      "socket", &socket,
      NULL);

  tp_svc_channel_type_tubes_return_from_get_stream_unix_socket_address (
      context, socket);

  g_free (socket);
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
  const char *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      NULL };
  GabbleTubesChannel *self = GABBLE_TUBES_CHANNEL (iface);

  if (self->muc)
    {
      tp_svc_channel_return_from_get_interfaces (context, interfaces);
    }
  else
    {
      /* only show the NULL */
      tp_svc_channel_return_from_get_interfaces (context, interfaces + 1);
    }
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

  param_spec = g_param_spec_object (
      "muc",
      "GabbleMucChannel object",
      "Gabble text MUC channel corresponding to this Tubes channel object, "
      "if the handle type is ROOM.",
      GABBLE_TYPE_MUC_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MUC, param_spec);
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
  IMPLEMENT(offer_d_bus_tube);
  IMPLEMENT(offer_stream_unix_tube);
  IMPLEMENT(accept_tube);
  IMPLEMENT(close_tube);
  IMPLEMENT(get_d_bus_server_address);
  IMPLEMENT(get_d_bus_names);
  IMPLEMENT(get_stream_unix_socket_address);
#undef IMPLEMENT
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_tubes_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}
