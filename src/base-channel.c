/*
 * base-channel.c - base class for Channel implementations
 *
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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

/**
 * SECTION:base-channel
 * @title: GabbleBaseChannel
 * @short_description: base class for #TpExportableChannel implementations
 * @see_also: #TpSvcChannel
 *
 * This base class makes it easier to write #TpExportableChannel
 * implementations by implementing some of its properties, and defining other
 * relevant properties.
 *
 * Subclasses must implement the Close method on #TpSvcChannel (setting
 * #GabbleBaseChannel:closed to %TRUE when it is called). The default
 * implementation for #TpExportableChannel:channel-properties just includes the
 * immutable properties from the Channel interface; subclasses will almost
 * certainly want to override this to include other immutable properties. The
 * default implementation for #TpExportableChannel:channel-destroyed is simply
 * the value of #GabbleBaseChannel:closed; this should be fine for channels
 * that don't respawn. They may also choose to override
 * #GabbleBaseChannel:requested, whose default implementation is "initiator ==
 * self_handle?".
 *
 * Subclasses should fill in #GabbleBaseChannel:channel_type,
 * #GabbleBaseChannel:interfaces during construction, and ensure that
 * #GabbleBaseChannel:object_path is not %NULL by the time construction is
 * finished (if it is not set by the object's creator, they must fill it in
 * themself); #GabbleBaseChannel will take care of freeing it.
 */

#include "config.h"
#include "base-channel.h"

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#define DEBUG_FLAG GABBLE_DEBUG_BASE_CHANNEL

#include "debug.h"

enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_TARGET_ID,
  PROP_REQUESTED,
  PROP_CONNECTION,
  PROP_INTERFACES,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  LAST_PROPERTY
};

struct _GabbleBaseChannelPrivate
{
  gboolean dispose_has_run;
};

static void channel_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleBaseChannel, gabble_base_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    )

/**
 * gabble_base_channel_register:
 * @chan: a channel
 *
 * Make the channel appear on the bus.  @chan->object_path must have been set
 * to a valid path, which must not already be in use as another object's path.
 */
void
gabble_base_channel_register (GabbleBaseChannel *chan)
{
  DBusGConnection *bus = tp_get_bus ();

  g_assert (chan->object_path != NULL);

  dbus_g_connection_register_g_object (bus, chan->object_path,
      (GObject *) chan);
}

static void
gabble_base_channel_init (GabbleBaseChannel *self)
{
  GabbleBaseChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BASE_CHANNEL, GabbleBaseChannelPrivate);

  self->priv = priv;
}

static GObject *
gabble_base_channel_constructor (GType type,
                                 guint n_props,
                                 GObjectConstructParam *props)
{
  GObject *obj;
  GabbleBaseChannel *chan;
  TpBaseConnection *conn;
  TpHandleRepoIface *handles;

  obj = G_OBJECT_CLASS (gabble_base_channel_parent_class)->constructor (
      type, n_props, props);
  chan = GABBLE_BASE_CHANNEL (obj);
  conn = (TpBaseConnection *) chan->conn;

  if (chan->target_type != TP_HANDLE_TYPE_NONE)
    {
      handles = tp_base_connection_get_handles (conn, chan->target_type);
      g_assert (handles != NULL);
      g_assert (chan->target != 0);
      tp_handle_ref (handles, chan->target);
    }

  if (chan->initiator != 0)
    {
      handles = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
      g_assert (handles != NULL);
      tp_handle_ref (handles, chan->initiator);
    }

  return obj;
}

static void
gabble_base_channel_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  GabbleBaseChannel *chan = GABBLE_BASE_CHANNEL (object);
  TpBaseConnection *base_conn = (TpBaseConnection *) chan->conn;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, chan->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, chan->channel_type);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, chan->target_type);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, chan->target);
      break;
    case PROP_TARGET_ID:
      if (chan->target != 0)
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, chan->target_type);

          g_value_set_string (value, tp_handle_inspect (repo, chan->target));
        }
      else
        {
          g_value_set_static_string (value, "");
        }
      break;
    case PROP_INITIATOR_HANDLE:
      g_value_set_uint (value, chan->initiator);
      break;
    case PROP_INITIATOR_ID:
      if (chan->initiator != 0)
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);

          g_assert (chan->initiator != 0);
          g_value_set_string (value, tp_handle_inspect (repo, chan->initiator));
        }
      else
        {
          g_value_set_static_string (value, "");
        }
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, (chan->initiator == base_conn->self_handle));
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, chan->conn);
      break;
    case PROP_INTERFACES:
      g_value_set_boxed (value, chan->interfaces);
      break;
    case PROP_CHANNEL_DESTROYED:
      g_value_set_boolean (value, chan->closed);
      break;
    case PROP_CHANNEL_PROPERTIES:
      g_value_take_boxed (value,
          tp_dbus_properties_mixin_make_properties_hash (
              object,
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
gabble_base_channel_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  GabbleBaseChannel *chan = GABBLE_BASE_CHANNEL (object);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (chan->object_path);
      chan->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      /* we don't ref it here because we don't necessarily have access to the
       * contact repo yet - instead we ref it in the constructor.
       */
      chan->target = g_value_get_uint (value);
      break;
    case PROP_INITIATOR_HANDLE:
      /* similarly we can't ref this yet */
      chan->initiator = g_value_get_uint (value);
      break;
    case PROP_HANDLE_TYPE:
    case PROP_CHANNEL_TYPE:
      /* these properties are writable in the interface, but not actually
       * meaningfully changeable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      chan->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_base_channel_dispose (GObject *object)
{
  GabbleBaseChannel *chan = GABBLE_BASE_CHANNEL (object);
  GabbleBaseChannelPrivate *priv = chan->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!chan->closed)
    {
      chan->closed = TRUE;
      tp_svc_channel_emit_closed (chan);
    }

  if (G_OBJECT_CLASS (gabble_base_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_base_channel_parent_class)->dispose (object);
}

static void
gabble_base_channel_finalize (GObject *object)
{
  GabbleBaseChannel *chan = GABBLE_BASE_CHANNEL (object);
  TpBaseConnection *conn = (TpBaseConnection *) chan->conn;
  TpHandleRepoIface *handles;

  if (chan->target != 0)
    {
      handles = tp_base_connection_get_handles (conn, chan->target_type);
      tp_handle_unref (handles, chan->target);
    }

  if (chan->initiator != 0)
    {
      handles = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
      tp_handle_unref (handles, chan->initiator);
    }

  g_free (chan->object_path);

  G_OBJECT_CLASS (gabble_base_channel_parent_class)->finalize (object);
}

static void
gabble_base_channel_class_init (GabbleBaseChannelClass *gabble_base_channel_class)
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
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_base_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_base_channel_class,
      sizeof (GabbleBaseChannelPrivate));

  object_class->constructor = gabble_base_channel_constructor;

  object_class->get_property = gabble_base_channel_get_property;
  object_class->set_property = gabble_base_channel_set_property;

  object_class->dispose = gabble_base_channel_dispose;
  object_class->finalize = gabble_base_channel_finalize;

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

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this channel.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_string ("target-id", "Target's identifier",
      "The string obtained by inspecting the target handle",
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

  gabble_base_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleBaseChannelClass, dbus_props_class));
}

static void
gabble_base_channel_get_channel_type (TpSvcChannel *iface,
                                      DBusGMethodInvocation *context)
{
  GabbleBaseChannel *chan = GABBLE_BASE_CHANNEL (iface);

  tp_svc_channel_return_from_get_channel_type (context, chan->channel_type);
}

static void
gabble_base_channel_get_handle (TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  GabbleBaseChannel *chan = GABBLE_BASE_CHANNEL (iface);

  tp_svc_channel_return_from_get_handle (context, chan->target_type,
      chan->target);
}

static void
gabble_base_channel_get_interfaces (TpSvcChannel *iface,
                                    DBusGMethodInvocation *context)
{
  GabbleBaseChannel *chan = GABBLE_BASE_CHANNEL (iface);

  tp_svc_channel_return_from_get_interfaces (context, chan->interfaces);
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_base_channel_##x)
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

