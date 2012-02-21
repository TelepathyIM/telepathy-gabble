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
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

#include "extensions/extensions.h"

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "gabble/caps-channel-manager.h"
#include "connection.h"
#include "debug.h"
#include "muc-channel.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "tubes-channel.h"
#include "tube-dbus.h"
#include "tube-stream.h"
#include "util.h"

static GabbleTubesChannel *new_tubes_channel (GabblePrivateTubesFactory *fac,
    TpHandle handle, TpHandle initiator, gpointer request_token,
    gboolean send_new_channel_signal);

static void tubes_channel_closed_cb (GabbleTubesChannel *chan,
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

  GHashTable *tubes_channels;

  gboolean dispose_has_run;
};

#define GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE(obj) ((obj)->priv)

static const gchar * const tubes_channel_fixed_properties[] = {
    TP_PROP_CHANNEL_CHANNEL_TYPE,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    NULL
};

static const gchar * const old_tubes_channel_allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE,
    TP_PROP_CHANNEL_TARGET_ID,
    NULL
};

static void
gabble_private_tubes_factory_init (GabblePrivateTubesFactory *self)
{
  GabblePrivateTubesFactoryPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_PRIVATE_TUBES_FACTORY, GabblePrivateTubesFactoryPrivate);

  self->priv = priv;

  priv->tubes_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
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
  GabblePrivateTubesFactoryPrivate *priv = self->priv;

  priv->msg_tube_cb = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_MAX,
      private_tubes_factory_msg_tube_cb, self,
      '(', "tube", ':', NS_TUBES,
      ')', NULL);
  priv->msg_close_cb = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_MAX,
      private_tubes_factory_msg_tube_cb, self,
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
  GabblePrivateTubesFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_private_tubes_factory_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_PRIVATE_TUBES_FACTORY (obj);
  priv = GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);
  tp_g_signal_connect_object (priv->conn, "porter-available",
      (GCallback) porter_available_cb, obj, 0);

  return obj;
}


static void
gabble_private_tubes_factory_dispose (GObject *object)
{
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (object);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_private_tubes_factory_close_all (fac);
  g_assert (priv->tubes_channels == NULL);

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
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (object);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
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
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (object);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
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


/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when an Tubes channel is closed. Removes the references
 * that PrivateTubesFactory holds to them.
 */
static void
tubes_channel_closed_cb (GabbleTubesChannel *chan,
                         gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (user_data);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandle contact_handle;

  if (priv->tubes_channels == NULL)
    return;

  g_object_get (chan, "handle", &contact_handle, NULL);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (chan));

  DEBUG ("removing tubes channel with handle %d", contact_handle);

  g_hash_table_remove (priv->tubes_channels, GUINT_TO_POINTER (contact_handle));
}

/**
 * new_tubes_channel
 *
 * Creates the GabbleTubes object associated with the given parameters
 */
static GabbleTubesChannel *
new_tubes_channel (GabblePrivateTubesFactory *fac,
                   TpHandle handle,
                   TpHandle initiator,
                   gpointer request_token,
                   gboolean send_new_channel_signal)
{
  GabblePrivateTubesFactoryPrivate *priv;
  TpBaseConnection *conn;
  GabbleTubesChannel *chan;
  char *object_path;
  gboolean requested;

  g_assert (GABBLE_IS_PRIVATE_TUBES_FACTORY (fac));
  g_assert (handle != 0);
  g_assert (initiator != 0);

  priv = GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *) priv->conn;

  object_path = g_strdup_printf ("%s/SITubesChannel%u", conn->object_path,
      handle);

  requested = (request_token != NULL);

  chan = g_object_new (GABBLE_TYPE_TUBES_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "handle-type", TP_HANDLE_TYPE_CONTACT,
                       "initiator-handle", initiator,
                       "requested", requested,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", G_CALLBACK (tubes_channel_closed_cb), fac);

  g_hash_table_insert (priv->tubes_channels, GUINT_TO_POINTER (handle), chan);

  g_free (object_path);

  if (send_new_channel_signal)
    {
      GSList *request_tokens;
      if (request_token != NULL)
        request_tokens = g_slist_prepend (NULL, request_token);
      else
        request_tokens = NULL;

      tp_channel_manager_emit_new_channel (fac,
          TP_EXPORTABLE_CHANNEL (chan), request_tokens);

      g_slist_free (request_tokens);
    }

  return chan;
}

static void
gabble_private_tubes_factory_close_all (GabblePrivateTubesFactory *fac)
{
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);

  DEBUG ("closing 1-1 tubes channels");

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (priv->msg_tube_cb != 0)
    {
      WockyPorter *porter = wocky_session_get_porter (priv->conn->session);

      wocky_porter_unregister_handler (porter, priv->msg_tube_cb);
      priv->msg_tube_cb = 0;
      wocky_porter_unregister_handler (porter, priv->msg_close_cb);
      priv->msg_close_cb = 0;
    }

  /* Use a temporary variable (the macro does this) because we don't want
   * tubes_channel_closed_cb to remove the channel from the hash table a
   * second time */
  tp_clear_pointer (&priv->tubes_channels, g_hash_table_unref);
}

static void
add_service_to_array (const gchar *service,
                      GPtrArray *arr,
                      TpTubeType type,
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

  g_assert (type == TP_TUBE_TYPE_STREAM || type == TP_TUBE_TYPE_DBUS);

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  if (type == TP_TUBE_TYPE_STREAM)
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
  if (type == TP_TUBE_TYPE_STREAM)
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
        TP_TUBE_TYPE_STREAM, closure->handle);
  else if (g_str_has_prefix (ns, DBUS_CAP_PREFIX))
    add_service_to_array (ns + strlen (DBUS_CAP_PREFIX), closure->arr,
        TP_TUBE_TYPE_DBUS, closure->handle);
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

  /* Always claim that we support tubes. */
  closure.supports_tubes = (handle == self->priv->conn->parent.self_handle);

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
  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
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
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

  /* Add channels of type Channel.Type.Tubes */
  data->foreach (chan, data->user_data);

  /* Add channels of type Channel.Type.{Stream|DBus}Tube which live in the
   * GabbleTubesChannel object */
  gabble_tubes_channel_foreach (GABBLE_TUBES_CHANNEL (chan), data->foreach,
      data->user_data);
}

static void
gabble_private_tubes_factory_foreach_channel (TpChannelManager *manager,
    TpExportableChannelFunc foreach,
    gpointer user_data)
{
  GabblePrivateTubesFactory *fac = GABBLE_PRIVATE_TUBES_FACTORY (manager);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->tubes_channels, _foreach_slave, &data);
}

void
gabble_private_tubes_factory_handle_si_tube_request (
    GabblePrivateTubesFactory *self,
    GabbleBytestreamIface *bytestream,
    TpHandle handle,
    const gchar *stream_id,
    WockyStanza *msg)
{
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubesChannel *chan;

  DEBUG ("contact#%u stream %s", handle, stream_id);
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  chan = g_hash_table_lookup (priv->tubes_channels, GUINT_TO_POINTER (handle));
  if (chan == NULL)
    {
      chan = new_tubes_channel (self, handle, handle, NULL, TRUE);

      /* FIXME: Should we close the channel if the request is not properly
       * handled by the newly created channel ? */
    }

  gabble_tubes_channel_tube_si_offered (chan, bytestream, msg);
}

void
gabble_private_tubes_factory_handle_si_stream_request (
    GabblePrivateTubesFactory *self,
    GabbleBytestreamIface *bytestream,
    TpHandle handle,
    const gchar *stream_id,
    WockyStanza *msg)
{
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleTubesChannel *chan;

  DEBUG ("contact#%u stream %s", handle, stream_id);
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  chan = g_hash_table_lookup (priv->tubes_channels, GUINT_TO_POINTER (handle));
  if (chan == NULL)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "No tubes channel available for this contact" };

      DEBUG ("tubes channel with contact %d doesn't exist", handle);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  gabble_tubes_channel_bytestream_offered (chan, bytestream, msg);
}

static gboolean
private_tubes_factory_msg_tube_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data)
{
  GabblePrivateTubesFactory *self = GABBLE_PRIVATE_TUBES_FACTORY (user_data);
  GabblePrivateTubesFactoryPrivate *priv =
    GABBLE_PRIVATE_TUBES_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  WockyNode *tube_node, *close_node;
  GabbleTubesChannel *chan;
  const gchar *from;
  TpHandle handle;

  tube_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "tube", NS_TUBES);
  close_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "close", NS_TUBES);

  g_return_val_if_fail (tube_node != NULL || close_node != NULL, FALSE);

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

  /* Tube offer */
  chan = g_hash_table_lookup (priv->tubes_channels, GUINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      if (tube_node != NULL)
        {
          /* We create the tubes channel only if the message is a new tube
           * offer */
          chan = new_tubes_channel (self, handle, handle, NULL, TRUE);
        }
      else
        {
          DEBUG ("Ignore tube close message as there is no tubes channel"
             " to handle it");
          return TRUE;
        }
    }

  gabble_tubes_channel_tube_msg (chan, msg);

  return TRUE;
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

  /* 1-1 Channel.Type.Tubes */
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TUBES);
  g_hash_table_insert (table, TP_PROP_CHANNEL_CHANNEL_TYPE,
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
      value);

  func (type, table, old_tubes_channel_allowed_properties, user_data);

  g_hash_table_unref (table);

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
  GabbleTubesChannel *channel;

  if (tp_asv_get_uint32 (request_properties,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  channel_type = tp_asv_get_string (request_properties,
            TP_PROP_CHANNEL_CHANNEL_TYPE);

  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    return FALSE;

  if (! tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              old_tubes_channel_allowed_properties,
              &error))
        goto error;
    }
  else if (! tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      const gchar *service;

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
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Request does not contain the mandatory property '%s'",
              TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);
          goto error;
        }
    }
  else if (! tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      const gchar *service;
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
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Request does not contain the mandatory property '%s'",
              TP_PROP_CHANNEL_TYPE_DBUS_TUBE_SERVICE_NAME);
          goto error;
        }

      if (!tp_dbus_check_valid_bus_name (service, TP_DBUS_NAME_TYPE_WELL_KNOWN,
            &err))
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
  if (handle == base_conn->self_handle)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Can't open a channel to your self handle");
      goto error;
    }

  channel = g_hash_table_lookup (self->priv->tubes_channels,
      GUINT_TO_POINTER (handle));

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      if (channel == NULL)
        {
          new_tubes_channel (self, handle, base_conn->self_handle,
              request_token, TRUE);
          return TRUE;
        }

      if (require_new)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Tubes channel with contact #%u already exists", handle);
          DEBUG ("Tubes channel with contact #%u already exists",
              handle);
          goto error;
        }

      tp_channel_manager_emit_request_already_satisfied (self,
          request_token, TP_EXPORTABLE_CHANNEL (channel));
      return TRUE;
    }
  else
    {
      gboolean tubes_channel_already_existed = (channel != NULL);
      GabbleTubeIface *new_channel;

      if (channel == NULL)
        {
          /* Don't give the request_token to new_tubes_channel() because we
           * must emit NewChannels with 2 channels together */
          channel = new_tubes_channel (self, handle, base_conn->self_handle,
              NULL, FALSE);
        }
      g_assert (channel != NULL);

      new_channel = gabble_tubes_channel_tube_request (channel, request_token,
          request_properties, require_new);
      if (new_channel != NULL)
        {
          GHashTable *channels;
          GSList *request_tokens;

          channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
              NULL, NULL);
          if (!tubes_channel_already_existed)
            g_hash_table_insert (channels, channel, NULL);

          if (request_token != NULL)
            request_tokens = g_slist_prepend (NULL, request_token);
          else
            request_tokens = NULL;

          g_hash_table_insert (channels, new_channel, request_tokens);
          tp_channel_manager_emit_new_channels (self, channels);

          g_hash_table_unref (channels);
          g_slist_free (request_tokens);
        }
      else
        {
          tp_channel_manager_emit_request_already_satisfied (self,
              request_token, TP_EXPORTABLE_CHANNEL (channel));
        }

      return TRUE;
    }

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
