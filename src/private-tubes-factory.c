/*
 * private-tubes-factory.c - Source for GabblePrivateTubesFactory
 * Copyright (C) 2006 Collabora Ltd.
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
#include "private-tubes-factory.h"

#include <string.h>
#include <glib-object.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "extensions/extensions.h"

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "bytestream-factory.h"
#include "gabble/caps-channel-manager.h"
#include "connection.h"
#include "debug.h"
#include "muc-channel.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "tube-dbus.h"
#include "tube-stream.h"
#include "util.h"

static GabbleTubeIface * new_channel_from_stanza (GabblePrivateTubesFactory *self,
    WockyStanza *stanza, WockyNode *tube_node, guint64 tube_id,
    GabbleBytestreamIface *bytestream);

static gboolean private_tubes_factory_tube_close_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data);
static gboolean private_tubes_factory_msg_tube_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data);

static void channel_manager_iface_init (gpointer, gpointer);
static void caps_channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabblePrivateTubesFactory,
    gabble_private_tubes_factory,
    G_TYPE_OBJECT,
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

struct _GabblePrivateTubesFactoryPrivate
{
  GabbleConnection *conn;
  gulong status_changed_id;
  guint msg_tube_cb;
  guint msg_close_cb;

  /* guint tube ID => (owned) (GabbleTubeIface *) */
  GHashTable *tubes;

  gboolean dispose_has_run;
};

static const gchar * const tubes_channel_fixed_properties[] = {
    TP_PROP_CHANNEL_CHANNEL_TYPE,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    NULL
};

gboolean
gabble_private_tubes_factory_extract_tube_information (
    TpHandleRepoIface *contact_repo,
    WockyNode *tube_node,
    TubeType *type,
    TpHandle *initiator_handle,
    const gchar **service,
    GHashTable **parameters,
    guint64 *tube_id)
{
  if (type != NULL)
    {
      const gchar *_type;

      _type = wocky_node_get_attribute (tube_node, "type");

      if (!tp_strdiff (_type, "stream"))
        {
          *type = TUBE_TYPE_STREAM;
        }
      else if (!tp_strdiff (_type, "dbus"))
        {
          *type = TUBE_TYPE_DBUS;
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

      initiator = wocky_node_get_attribute (tube_node, "initiator");

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
      *service = wocky_node_get_attribute (tube_node, "service");
    }

  if (parameters != NULL)
    {
      WockyNode *node;

      node = wocky_node_get_child (tube_node, "parameters");
      *parameters = lm_message_node_extract_properties (node, "parameter");
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      guint64 tmp;

      str = wocky_node_get_attribute (tube_node, "id");
      if (str == NULL)
        {
          DEBUG ("no tube id in SI request");
          return FALSE;
        }

      tmp = g_ascii_strtoull (str, NULL, 10);
      if (tmp == 0 || tmp > G_MAXUINT32)
        {
          DEBUG ("tube id is non-numeric or out of range: %s", str);
          return FALSE;
        }
      *tube_id = tmp;
    }

  return TRUE;
}

static void
gabble_private_tubes_factory_init (GabblePrivateTubesFactory *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_PRIVATE_TUBES_FACTORY, GabblePrivateTubesFactoryPrivate);

  self->priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);

  self->priv->conn = NULL;
  self->priv->dispose_has_run = FALSE;
}


static void gabble_private_tubes_factory_close_all (
    GabblePrivateTubesFactory *fac);

static void
porter_available_cb (
    GabbleConnection *connection,
    WockyPorter *porter,
    gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (user_data);

  self->priv->msg_tube_cb = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_MAX,
      private_tubes_factory_msg_tube_cb, self,
      '(', "tube", ':', NS_TUBES,
      ')', NULL);

  self->priv->msg_close_cb = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_MAX,
      private_tubes_factory_tube_close_cb, self,
      '(', "close", ':', NS_TUBES,
      ')', NULL);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabblePrivateTubesFactory *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_private_tubes_factory_close_all (self);
      break;
    }
}


static GObject *
gabble_private_tubes_factory_constructor (GType type,
                                          guint n_props,
                                          GObjectConstructParam *props)
{
  GObject *obj;
  GabblePrivateTubesFactory *self;

  obj = G_OBJECT_CLASS (gabble_private_tubes_factory_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_PRIVATE_TUBES_FACTORY (obj);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);
  tp_g_signal_connect_object (self->priv->conn, "porter-available",
      (GCallback) porter_available_cb, obj, 0);

  return obj;
}


static void
gabble_private_tubes_factory_dispose (GObject *object)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (object);

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  self->priv->dispose_has_run = TRUE;

  gabble_private_tubes_factory_close_all (self);
  g_assert (self->priv->tubes == NULL);

  if (G_OBJECT_CLASS (gabble_private_tubes_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_private_tubes_factory_parent_class)->dispose (
        object);
}

static void
gabble_private_tubes_factory_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->priv->conn);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_private_tubes_factory_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        self->priv->conn = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_private_tubes_factory_class_init (
    GabblePrivateTubesFactoryClass *gabble_private_tubes_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gabble_private_tubes_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_private_tubes_factory_class,
      sizeof (GabblePrivateTubesFactoryPrivate));

  object_class->constructor = gabble_private_tubes_factory_constructor;
  object_class->dispose = gabble_private_tubes_factory_dispose;

  object_class->get_property = gabble_private_tubes_factory_get_property;
  object_class->set_property = gabble_private_tubes_factory_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this Tubes channel manager object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static void
gabble_private_tubes_factory_close_all (GabblePrivateTubesFactory *self)
{
  DEBUG ("closing 1-1 tubes channels");

  if (self->priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (self->priv->conn,
          self->priv->status_changed_id);
      self->priv->status_changed_id = 0;
    }

  if (self->priv->msg_tube_cb != 0)
    {
      WockyPorter *porter = wocky_session_get_porter (
          self->priv->conn->session);

      wocky_porter_unregister_handler (porter, self->priv->msg_tube_cb);
      self->priv->msg_tube_cb = 0;
      wocky_porter_unregister_handler (porter, self->priv->msg_close_cb);
      self->priv->msg_close_cb = 0;
    }

  tp_clear_pointer (&self->priv->tubes, g_hash_table_unref);
}

static void
add_service_to_array (const gchar *service,
                      GPtrArray *arr,
                      TubeType type,
                      TpHandle handle)
{
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;
  gchar *tube_allowed_properties[] =
    {
        TP_PROP_CHANNEL_TARGET_HANDLE,
        TP_PROP_CHANNEL_TARGET_ID,
        NULL
    };

  g_assert (type == TUBE_TYPE_STREAM || type == TUBE_TYPE_DBUS);

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  if (type == TUBE_TYPE_STREAM)
    g_value_set_static_string (channel_type_value,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  else
    g_value_set_static_string (channel_type_value,
        TP_IFACE_CHANNEL_TYPE_DBUS_TUBE);
  g_hash_table_insert (fixed_properties, TP_PROP_CHANNEL_CHANNEL_TYPE,
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, target_handle_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_string (target_handle_type_value, service);
  if (type == TUBE_TYPE_STREAM)
    g_hash_table_insert (fixed_properties,
        TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE,
        target_handle_type_value);
  else
    g_hash_table_insert (fixed_properties,
        TP_PROP_CHANNEL_TYPE_DBUS_TUBE_SERVICE_NAME,
        target_handle_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, tube_allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

static void
add_generic_tube_caps (GPtrArray *arr)
{
  GValue monster1 = {0,}, monster2 = {0,};
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;

  /* StreamTube */
  g_value_init (&monster1, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster1,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);

  g_hash_table_insert (fixed_properties, TP_PROP_CHANNEL_CHANNEL_TYPE,
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, target_handle_type_value);

  dbus_g_type_struct_set (&monster1,
      0, fixed_properties,
      1, gabble_tube_stream_channel_get_allowed_properties (),
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);
  g_ptr_array_add (arr, g_value_get_boxed (&monster1));

  /* DBusTube */
  g_value_init (&monster2, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster2,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_DBUS_TUBE);

  g_hash_table_insert (fixed_properties, TP_PROP_CHANNEL_CHANNEL_TYPE,
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, target_handle_type_value);

  dbus_g_type_struct_set (&monster2,
      0, fixed_properties,
      1, gabble_tube_dbus_channel_get_allowed_properties (),
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);
  g_ptr_array_add (arr, g_value_get_boxed (&monster2));
}

#define STREAM_CAP_PREFIX (NS_TUBES "/stream#")
#define DBUS_CAP_PREFIX (NS_TUBES "/dbus#")

typedef struct {
    gboolean supports_tubes;
    GPtrArray *arr;
    TpHandle handle;
} GetContactCapsClosure;

static void
get_contact_caps_foreach (gpointer data,
    gpointer user_data)
{
  const gchar *ns = data;
  GetContactCapsClosure *closure = user_data;

  if (!g_str_has_prefix (ns, NS_TUBES))
    return;

  closure->supports_tubes = TRUE;

  if (g_str_has_prefix (ns, STREAM_CAP_PREFIX))
    add_service_to_array (ns + strlen (STREAM_CAP_PREFIX), closure->arr,
        TUBE_TYPE_STREAM, closure->handle);
  else if (g_str_has_prefix (ns, DBUS_CAP_PREFIX))
    add_service_to_array (ns + strlen (DBUS_CAP_PREFIX), closure->arr,
        TUBE_TYPE_DBUS, closure->handle);
}

static void
gabble_private_tubes_factory_get_contact_caps (
    GabbleCapsChannelManager *manager,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);
  GetContactCapsClosure closure = { FALSE, arr, handle };
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);

  /* Always claim that we support tubes. */
  closure.supports_tubes = (handle ==
      tp_base_connection_get_self_handle (base_conn));

  gabble_capability_set_foreach (caps, get_contact_caps_foreach, &closure);

  if (closure.supports_tubes)
    add_generic_tube_caps (arr);
}

static void
gabble_private_tubes_factory_add_cap (GabbleCapsChannelManager *manager,
    const gchar *client_name,
    GHashTable *cap,
    GabbleCapabilitySet *cap_set)
{
  const gchar *channel_type, *service;
  gchar *ns = NULL;

  /* capabilities mean being able to RECEIVE said kinds of tubes. hence,
   * skip Requested=true (locally initiated) channel classes */
  if (tp_asv_get_boolean (cap, TP_PROP_CHANNEL_REQUESTED, FALSE))
    return;

  channel_type = tp_asv_get_string (cap,
            TP_PROP_CHANNEL_CHANNEL_TYPE);

  /* this channel is not for this factory */
  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    return;

  if (tp_asv_get_uint32 (cap,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL) != TP_HANDLE_TYPE_CONTACT)
    return;

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      service = tp_asv_get_string (cap,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);

      if (service != NULL)
        ns = g_strconcat (STREAM_CAP_PREFIX, service, NULL);
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      service = tp_asv_get_string (cap,
          TP_PROP_CHANNEL_TYPE_DBUS_TUBE_SERVICE_NAME);

      if (service != NULL)
        ns = g_strconcat (DBUS_CAP_PREFIX, service, NULL);
    }

  if (ns != NULL)
    {
      DEBUG ("%s: adding capability %s", client_name, ns);
      gabble_capability_set_add (cap_set, ns);
      g_free (ns);
    }
}

static void
gabble_private_tubes_factory_represent_client (
    GabbleCapsChannelManager *manager,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens,
    GabbleCapabilitySet *cap_set,
    GPtrArray *data_forms)
{
  guint i;

  for (i = 0; i < filters->len; i++)
    {
      gabble_private_tubes_factory_add_cap (manager, client_name,
          g_ptr_array_index (filters, i), cap_set);
    }
}

struct _ForeachData
{
  TpExportableChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key,
                gpointer value,
                gpointer user_data)
{
  struct _ForeachData *data = user_data;
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

  data->foreach (chan, data->user_data);
}

static void
gabble_private_tubes_factory_foreach_channel (TpChannelManager *manager,
    TpExportableChannelFunc foreach,
    gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (self->priv->tubes, _foreach_slave, &data);
}

void
gabble_private_tubes_factory_handle_si_tube_request (
    GabblePrivateTubesFactory *self,
    GabbleBytestreamIface *bytestream,
    TpHandle handle,
    const gchar *stream_id,
    WockyStanza *msg)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  WockyNode *si_node, *tube_node;
  WockyStanzaType stanza_type;
  WockyStanzaSubType sub_type;
  guint64 tube_id;
  GabbleTubeIface *tube;

  DEBUG ("contact#%u stream %s", handle, stream_id);
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  wocky_stanza_get_type_info (msg, &stanza_type, &sub_type);
  g_return_if_fail (stanza_type == WOCKY_STANZA_TYPE_IQ);
  g_return_if_fail (sub_type == WOCKY_STANZA_SUB_TYPE_SET);
  si_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "si", NS_SI);
  g_return_if_fail (si_node != NULL);
  tube_node = wocky_node_get_child_ns (si_node, "tube",
      NS_TUBES);
  g_return_if_fail (tube_node != NULL);

  if (!gabble_private_tubes_factory_extract_tube_information (
          contact_repo, tube_node, NULL, NULL,
          NULL, NULL, &tube_id))
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<tube> has no id attribute" };

      NODE_DEBUG (tube_node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  tube = g_hash_table_lookup (self->priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube != NULL)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "tube ID already in use" };

      NODE_DEBUG (tube_node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  /* New tube */
  new_channel_from_stanza (self, msg, tube_node,
      tube_id, bytestream);
}

void
gabble_private_tubes_factory_handle_si_stream_request (
    GabblePrivateTubesFactory *self,
    GabbleBytestreamIface *bytestream,
    TpHandle handle,
    const gchar *stream_id,
    WockyStanza *msg)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *tmp;
  guint64 tube_id;
  WockyNode *si_node, *stream_node;
  GabbleTubeIface *tube;
  WockyStanzaType stanza_type;
  WockyStanzaSubType sub_type;

  DEBUG ("contact#%u stream %s", handle, stream_id);
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  wocky_stanza_get_type_info (msg, &stanza_type, &sub_type);
  g_return_if_fail (stanza_type == WOCKY_STANZA_TYPE_IQ);
  g_return_if_fail (sub_type == WOCKY_STANZA_SUB_TYPE_SET);

  si_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "si", NS_SI);
  g_return_if_fail (si_node != NULL);

  stream_node = wocky_node_get_child_ns (si_node,
      "stream", NS_TUBES);
  g_return_if_fail (stream_node != NULL);

  tmp = wocky_node_get_attribute (stream_node, "tube");
  if (tmp == NULL)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<stream> has no tube attribute" };

      NODE_DEBUG (stream_node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }
  tube_id = g_ascii_strtoull (tmp, NULL, 10);
  if (tube_id == 0 || tube_id > G_MAXUINT32)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<stream> tube ID attribute non-numeric or out of range" };

      DEBUG ("tube id is non-numeric or out of range: %s", tmp);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  tube = g_hash_table_lookup (self->priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<stream> tube attribute points to a nonexistent "
          "tube" };

      DEBUG ("tube %" G_GUINT64_FORMAT " doesn't exist", tube_id);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  DEBUG ("received new bytestream request for existing tube: %" G_GUINT64_FORMAT,
      tube_id);

  gabble_tube_iface_add_bytestream (tube, bytestream);
}

static gboolean
tube_msg_checks (GabblePrivateTubesFactory *self,
    WockyStanza *msg,
    WockyNode *node,
    TpHandle *out_handle,
    guint64 *out_tube_id)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *from, *tmp;
  TpHandle handle;
  guint64 tube_id;

  from = wocky_node_get_attribute (
      wocky_stanza_get_top_node (msg), "from");
  if (from == NULL)
    {
      STANZA_DEBUG (msg, "got a message without a from field");
      return FALSE;
    }

  handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (handle == 0)
    {
      DEBUG ("Invalid from field");
      return FALSE;
    }

  tmp = wocky_node_get_attribute (node, "id");
  if (tmp == NULL)
    {
      DEBUG ("failed to get the tube ID");
      return FALSE;
    }

  tube_id = g_ascii_strtoull (tmp, NULL, 10);
  if (tube_id == 0 || tube_id > G_MAXUINT32)
    {
      DEBUG ("tube ID is non-numeric or out of range: %s", tmp);
      return FALSE;
    }

  if (out_tube_id != NULL)
    *out_tube_id = tube_id;

  if (out_handle != NULL)
    *out_handle = handle;

  return TRUE;
}

static gboolean
private_tubes_factory_msg_tube_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (user_data);
  WockyNode *node;
  guint64 tube_id;
  GabbleTubeIface *channel;
  TpHandle handle;

  node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "tube", NS_TUBES);
  g_return_val_if_fail (node != NULL, FALSE);

  if (!tube_msg_checks (self, msg, node, &handle, &tube_id))
    return FALSE;

  channel = g_hash_table_lookup (self->priv->tubes, GUINT_TO_POINTER (tube_id));

  if (channel != NULL)
    {
      TpHandle tube_handle = 0;

      g_object_get (channel,
          "handle", &tube_handle,
          NULL);

      DEBUG ("tube ID already in use; do not open the offered tube and close "
          "the existing tube if it's to the same contact");

      /* only close the existing channel if it's the same contact
       * otherwise contacts could force close unrelated tubes. */
      if (handle == tube_handle)
        gabble_tube_iface_close (channel, FALSE);

      return TRUE;
    }

  new_channel_from_stanza (self, msg, node, tube_id, NULL);

  return TRUE;
}

static gboolean
private_tubes_factory_tube_close_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (user_data);
  WockyNode *node;
  guint64 tube_id;
  GabbleTubeIface *channel;
  TubeType type;

  node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "close", NS_TUBES);
  g_return_val_if_fail (node != NULL, FALSE);

  if (!tube_msg_checks (self, msg, node, NULL, &tube_id))
    return FALSE;

  channel = g_hash_table_lookup (self->priv->tubes, GUINT_TO_POINTER (tube_id));

  if (channel == NULL)
    {
      DEBUG ("<close> tube attribute points to a nonexistent tube");
      return TRUE;
    }

  g_object_get (channel, "type", &type, NULL);
  if (type != TUBE_TYPE_STREAM)
    {
      DEBUG ("Only stream tubes can be closed using a close message");
      return TRUE;
    }

  DEBUG ("tube %" G_GUINT64_FORMAT " was closed by remote peer", tube_id);
  gabble_tube_iface_close (channel, TRUE);

  return TRUE;
}

static GabbleTubeIface *
gabble_private_tubes_factory_lookup (GabblePrivateTubesFactory *self,
    const gchar *type,
    TpHandle handle,
    const gchar *service)
{
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, self->priv->tubes);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      GabbleTubeIface *tube = value;
      gboolean match = FALSE;

      gchar *channel_type, *channel_service;
      TpHandle channel_handle;

      g_object_get (tube,
          "channel-type", &channel_type,
          "handle", &channel_handle,
          "service", &channel_service,
          NULL);

      if (!tp_strdiff (type, channel_type)
          && handle == channel_handle
          && !tp_strdiff (service, channel_service))
        match = TRUE;

      g_free (channel_type);
      g_free (channel_service);

      if (match)
        return tube;
    }

  return NULL;
}

static void
channel_closed_cb (GabbleTubeIface *tube,
    GabblePrivateTubesFactory *self)
{
  guint64 id;

  g_object_get (tube,
      "id", &id,
      NULL);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (tube));

  if (self->priv->tubes != NULL)
    g_hash_table_remove (self->priv->tubes, GUINT_TO_POINTER (id));
}

static guint64
generate_tube_id (GabblePrivateTubesFactory *self)
{
  guint out;

  /* probably totally overkill */
  do
    {
      out = g_random_int_range (1, G_MAXINT32);
    }
  while (g_hash_table_lookup (self->priv->tubes,
          GUINT_TO_POINTER (out)) != NULL);

  return out;
}

/* Returns: (transfer none): new tube channel. the channel manager holds
 * the ref to this channel, so don't unref it! */
static GabbleTubeIface *
new_channel_from_request (GabblePrivateTubesFactory *self,
    GHashTable *request)
{
  GabbleTubeIface *tube;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);

  gchar *stream_id;

  TpHandle handle;
  const gchar *ctype, *service;
  TpHandleType handle_type;
  GHashTable *parameters;
  guint64 tube_id;

  ctype = tp_asv_get_string (request, TP_PROP_CHANNEL_CHANNEL_TYPE);
  handle = tp_asv_get_uint32 (request, TP_PROP_CHANNEL_TARGET_HANDLE,
      NULL);
  handle_type = tp_asv_get_uint32 (request,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);

  tube_id = generate_tube_id (self);

  /* requested tubes have an empty parameters dict */
  parameters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  if (!tp_strdiff (ctype, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      service = tp_asv_get_string (request,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);

      tube = GABBLE_TUBE_IFACE (gabble_tube_stream_new (self->priv->conn,
              handle, handle_type,
              tp_base_connection_get_self_handle (base_conn),
              tp_base_connection_get_self_handle (base_conn),
              service, parameters, tube_id, NULL, TRUE));
    }
  else if (!tp_strdiff (ctype, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      service = tp_asv_get_string (request,
          TP_PROP_CHANNEL_TYPE_DBUS_TUBE_SERVICE_NAME);

      stream_id = gabble_bytestream_factory_generate_stream_id ();

      tube = GABBLE_TUBE_IFACE (gabble_tube_dbus_new (self->priv->conn,
              handle, handle_type,
              tp_base_connection_get_self_handle (base_conn),
              tp_base_connection_get_self_handle (base_conn),
              service, parameters, stream_id, tube_id, NULL, NULL, TRUE));

      g_free (stream_id);
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  tp_base_channel_register ((TpBaseChannel *) tube);

  g_signal_connect (tube, "closed",
      G_CALLBACK (channel_closed_cb), self);

  g_hash_table_insert (self->priv->tubes, GUINT_TO_POINTER (tube_id),
      tube);

  g_hash_table_unref (parameters);

  return tube;
}

static void
send_tube_close_msg (GabblePrivateTubesFactory *self,
    const gchar *jid,
    guint64 tube_id)
{
  WockyPorter *porter;
  WockyStanza *msg;
  gchar *id_str;

  id_str = g_strdup_printf ("%" G_GUINT64_FORMAT, tube_id);

  porter = gabble_connection_dup_porter (self->priv->conn);

  /* Send the close message */
  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      NULL, jid,
      '(', "close",
        ':', NS_TUBES,
        '@', "tube", id_str,
      ')',
      GABBLE_AMP_DO_NOT_STORE_SPEC,
      NULL);
  g_free (id_str);

  wocky_porter_send (porter, msg);

  g_object_unref (porter);
  g_object_unref (msg);
}

/* Returns: (transfer none): new tube channel. the channel manager holds
 * the ref to this channel, so don't unref it! */
static GabbleTubeIface *
new_channel_from_stanza (GabblePrivateTubesFactory *self,
    WockyStanza *stanza,
    WockyNode *tube_node,
    guint64 tube_id,
    GabbleBytestreamIface *bytestream)
{
  GabbleTubeIface *tube;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);

  TubeType type;
  TpHandle handle;
  const gchar *service;
  GHashTable *parameters;

  /* the validity of this has already been checked by wocky */
  handle = tp_handle_ensure (contact_repo,
      wocky_stanza_get_from (stanza), NULL, NULL);
  g_return_val_if_fail (handle != 0, NULL);

  if (!gabble_private_tubes_factory_extract_tube_information (
          contact_repo, tube_node, &type, NULL,
          &service, &parameters, NULL))
    {
      DEBUG ("can't extract <tube> information from message");
      send_tube_close_msg (self, wocky_stanza_get_from (stanza), tube_id);
      return NULL;
    }

  if (bytestream == NULL && type != TUBE_TYPE_STREAM)
    {
      DEBUG ("Only stream tubes are allowed to be created using messages");
      send_tube_close_msg (self, wocky_stanza_get_from (stanza), tube_id);
      return NULL;
    }
  else if (bytestream != NULL && type != TUBE_TYPE_DBUS)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_FORBIDDEN,
          "Only D-Bus tubes are allowed to be created using SI" };

      DEBUG ("%s", e.message);
      gabble_bytestream_iface_close (bytestream, &e);
      return NULL;
    }

  if (type == TUBE_TYPE_STREAM)
    {
      tube = GABBLE_TUBE_IFACE (gabble_tube_stream_new (self->priv->conn,
              handle, TP_HANDLE_TYPE_CONTACT,
              tp_base_connection_get_self_handle (base_conn),
              handle, service, parameters, tube_id, NULL, FALSE));
    }
  else
    {
      WockyNode *si_node;
      const gchar *stream_id;

      si_node = wocky_node_get_child_ns (
          wocky_stanza_get_top_node (stanza), "si", NS_SI);
      g_return_val_if_fail (si_node != NULL, NULL);

      stream_id = wocky_node_get_attribute (si_node, "id");
      g_return_val_if_fail (stream_id != NULL, NULL);

      tube = GABBLE_TUBE_IFACE (gabble_tube_dbus_new (self->priv->conn,
              handle, TP_HANDLE_TYPE_CONTACT,
              tp_base_connection_get_self_handle (base_conn),
              handle, service, parameters,
              stream_id, tube_id, bytestream, NULL, FALSE));
    }

  tp_base_channel_register ((TpBaseChannel *) tube);

  g_signal_connect (tube, "closed",
      G_CALLBACK (channel_closed_cb), self);

  g_hash_table_insert (self->priv->tubes, GUINT_TO_POINTER (tube_id),
      tube);

  g_hash_table_unref (parameters);

  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (tube), NULL);

  return tube;
}

GabblePrivateTubesFactory *
gabble_private_tubes_factory_new (GabbleConnection *conn)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  return g_object_new (
      GABBLE_TYPE_PRIVATE_TUBES_FACTORY,
      "connection", conn,
      NULL);
}

static void
gabble_private_tubes_factory_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table;
  GValue *value;

  /* 1-1 Channel.Type.StreamTube */
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  g_hash_table_insert (table, TP_PROP_CHANNEL_CHANNEL_TYPE,
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
      value);

  func (type, table, gabble_tube_stream_channel_get_allowed_properties (),
      user_data);

  g_hash_table_unref (table);

  /* 1-1 Channel.Type.DBusTube */
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE);
  g_hash_table_insert (table, TP_PROP_CHANNEL_CHANNEL_TYPE,
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
      value);

  func (type, table, gabble_tube_dbus_channel_get_allowed_properties (),
      user_data);

  g_hash_table_unref (table);
}


static gboolean
gabble_private_tubes_factory_requestotron (GabblePrivateTubesFactory *self,
                                           gpointer request_token,
                                           GHashTable *request_properties,
                                           gboolean require_new)
{
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->conn;
  TpHandle handle;
  GError *error = NULL;
  const gchar *channel_type;
  GabbleTubeIface *channel;
  const gchar *service = NULL;

  if (tp_asv_get_uint32 (request_properties,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  channel_type = tp_asv_get_string (request_properties,
            TP_PROP_CHANNEL_CHANNEL_TYPE);

  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    return FALSE;

  if (! tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              gabble_tube_stream_channel_get_allowed_properties (),
              &error))
        goto error;

      /* "Service" is a mandatory, not-fixed property */
      service = tp_asv_get_string (request_properties,
                TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);
      if (service == NULL)
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
              "Request does not contain the mandatory property '%s'",
              TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);
          goto error;
        }
    }
  else if (! tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      GError *err = NULL;

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              gabble_tube_dbus_channel_get_allowed_properties (),
              &error))
        goto error;

      /* "ServiceName" is a mandatory, not-fixed property */
      service = tp_asv_get_string (request_properties,
                TP_PROP_CHANNEL_TYPE_DBUS_TUBE_SERVICE_NAME);
      if (service == NULL)
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
              "Request does not contain the mandatory property '%s'",
              TP_PROP_CHANNEL_TYPE_DBUS_TUBE_SERVICE_NAME);
          goto error;
        }

      if (!tp_dbus_check_valid_bus_name (service, TP_DBUS_NAME_TYPE_WELL_KNOWN,
            &err))
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "Invalid ServiceName: %s", err->message);
          g_error_free (err);
          goto error;
        }
    }

  /* validity already checked by TpBaseConnection */
  handle = tp_asv_get_uint32 (request_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE, NULL);
  g_assert (handle != 0);

  /* Don't support opening a channel to our self handle */
  if (handle == tp_base_connection_get_self_handle (base_conn))
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Can't open a channel to your self handle");
      goto error;
    }

  channel = gabble_private_tubes_factory_lookup (self, channel_type,
      handle, service);

  if (channel == NULL)
    {
      GSList *request_tokens = NULL;

      channel = new_channel_from_request (self, request_properties);

      if (request_token != NULL)
        request_tokens = g_slist_prepend (NULL, request_token);

      tp_channel_manager_emit_new_channel (self,
          TP_EXPORTABLE_CHANNEL (channel), request_tokens);

      g_slist_free (request_tokens);
    }
  else
    {
      if (require_new)
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
              "A channel to #%u (service: %s) is already open",
              handle, service);
          goto error;
        }

      tp_channel_manager_emit_request_already_satisfied (self,
          request_token, TP_EXPORTABLE_CHANNEL (channel));
    }

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
gabble_private_tubes_factory_create_channel (TpChannelManager *manager,
                                             gpointer request_token,
                                             GHashTable *request_properties)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);

  return gabble_private_tubes_factory_requestotron (self, request_token,
      request_properties, TRUE);
}


static gboolean
gabble_private_tubes_factory_request_channel (TpChannelManager *manager,
                                              gpointer request_token,
                                              GHashTable *request_properties)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);

  return gabble_private_tubes_factory_requestotron (self, request_token,
      request_properties, FALSE);
}


static gboolean
gabble_private_tubes_factory_ensure_channel (TpChannelManager *manager,
                                             gpointer request_token,
                                             GHashTable *request_properties)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (manager);

  return gabble_private_tubes_factory_requestotron (self, request_token,
      request_properties, FALSE);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_private_tubes_factory_foreach_channel;
  iface->type_foreach_channel_class =
      gabble_private_tubes_factory_type_foreach_channel_class;
  iface->create_channel = gabble_private_tubes_factory_create_channel;
  iface->request_channel = gabble_private_tubes_factory_request_channel;
  iface->ensure_channel = gabble_private_tubes_factory_ensure_channel;
}

static void
caps_channel_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  GabbleCapsChannelManagerInterface *iface = g_iface;

  iface->get_contact_caps = gabble_private_tubes_factory_get_contact_caps;
  iface->represent_client = gabble_private_tubes_factory_represent_client;
}
