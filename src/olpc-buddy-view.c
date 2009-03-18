/*
 * olpc-buddy-view.c - Source for GabbleOlpcBuddyView
 * Copyright (C) 2008 Collabora Ltd.
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

#include "olpc-buddy-view.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include "conn-olpc.h"
#include "debug.h"
#include "extensions/extensions.h"
#include "olpc-activity.h"
#include "namespaces.h"
#include "util.h"

/* properties */
enum
{
  /* org.laptop.Telepathy.Channel.Type.BuddyView D-Bus properties */
  PROP_VIEW_PROPERTIES = 1,
  PROP_ALIAS,

  PROP_CHANNEL_TYPE,
  PROP_CHANNEL_PROPERTIES,

  LAST_PROPERTY
};

struct _GabbleOlpcBuddyViewPrivate
{
  GHashTable *properties;
  gchar *alias;

  gboolean dispose_has_run;
};

G_DEFINE_TYPE_WITH_CODE (
    GabbleOlpcBuddyView, gabble_olpc_buddy_view, GABBLE_TYPE_OLPC_VIEW,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_CHANNEL_TYPE_BUDDY_VIEW,
      NULL);
    );

static void
gabble_olpc_buddy_view_init (GabbleOlpcBuddyView *self)
{
  GabbleOlpcBuddyViewPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_OLPC_BUDDY_VIEW, GabbleOlpcBuddyViewPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}

static void
gabble_olpc_buddy_view_dispose (GObject *object)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (object);
  GabbleOlpcBuddyViewPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->dispose (object);
}

static void
gabble_olpc_buddy_view_finalize (GObject *object)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (object);
  GabbleOlpcBuddyViewPrivate *priv = self->priv;

  g_hash_table_destroy (priv->properties);
  g_free (priv->alias);

  if (G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->finalize)
    G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->finalize (object);
}

static void
gabble_olpc_buddy_view_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (object);
  GabbleOlpcBuddyViewPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value,
            GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW);
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
                GABBLE_IFACE_OLPC_CHANNEL_INTERFACE_VIEW, "MaxSize",
                GABBLE_IFACE_OLPC_CHANNEL_INTERFACE_VIEW, "Buddies",
                GABBLE_IFACE_OLPC_CHANNEL_INTERFACE_VIEW, "Activities",
                GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW, "Properties",
                GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW, "Alias",
                NULL));
        break;
      case PROP_VIEW_PROPERTIES:
        g_value_set_boxed (value, priv->properties);
        break;
      case PROP_ALIAS:
        g_value_set_string (value, priv->alias);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_olpc_buddy_view_set_property (GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (object);
  GabbleOlpcBuddyViewPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CHANNEL_TYPE:
        /* these properties are writable in the interface, but not actually
         * meaningfully changeable on this channel, so we do nothing */
        break;
      case PROP_VIEW_PROPERTIES:
        priv->properties = g_value_dup_boxed (value);
        break;
      case PROP_ALIAS:
        priv->alias = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_olpc_buddy_view_constructor (GType type,
                                    guint n_props,
                                    GObjectConstructParam *props)
{
  GObject *obj;
  GabbleOlpcView *view;
  GabbleOlpcBuddyViewPrivate *priv;
  DBusGConnection *bus;
  TpBaseConnection *conn;

  obj = G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->
           constructor (type, n_props, props);

  view = GABBLE_OLPC_VIEW (obj);

  priv = GABBLE_OLPC_BUDDY_VIEW (obj)->priv;
  conn = (TpBaseConnection *) view->conn;

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, view->object_path, obj);

  if (priv->properties == NULL)
    {
      priv->properties = g_hash_table_new (g_direct_hash, g_direct_equal);
    }

  return obj;
}

static LmMessage *
gabble_olpc_buddy_view_create_close_msg (GabbleOlpcView *view)
{
  gchar *id_str;
  LmMessage *msg;

  id_str = g_strdup_printf ("%u", view->id);

  msg = lm_message_build (view->conn->olpc_gadget_buddy,
      LM_MESSAGE_TYPE_MESSAGE,
      '(', "close", "",
        '@', "xmlns", NS_OLPC_BUDDY,
        '@', "id", id_str,
      ')', NULL);

  g_free (id_str);

  return msg;
}

static LmHandlerResult
buddy_view_query_result_cb (GabbleConnection *conn,
                            LmMessage *sent_msg,
                            LmMessage *reply_msg,
                            GObject *_view,
                            gpointer user_data)
{
  LmMessageNode *view_node;
  GabbleOlpcView *self = GABBLE_OLPC_VIEW (_view);

  view_node = lm_message_node_get_child_with_namespace (reply_msg->node,
      "view", NS_OLPC_BUDDY);
  if (view_node == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  add_buddies_to_view_from_node (conn, self, view_node, "buddy", 0);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
gabble_olpc_buddy_view_send_request (GabbleOlpcView *view,
                                     GError **error)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (view);
  GabbleOlpcBuddyViewPrivate *priv = self->priv;
  LmMessage *query;
  LmMessageNode *buddy_node;
  gchar *max_str, *id_str;

  max_str = g_strdup_printf ("%u", view->max_size);
  id_str = g_strdup_printf ("%u", view->id);

  query = lm_message_build_with_sub_type (view->conn->olpc_gadget_buddy,
    LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET,
    '(', "view", "",
        '@', "xmlns", NS_OLPC_BUDDY,
        '@', "id", id_str,
        '@', "size", max_str,
        '(', "buddy", "",
          '*', &buddy_node,
        ')',
    ')', NULL);

  /* BuddyView.Properties */
  if (g_hash_table_size (priv->properties) != 0)
    {
      LmMessageNode *properties_node;

      properties_node = lm_message_node_add_child (buddy_node, "properties",
          NULL);
      lm_message_node_set_attribute (properties_node, "xmlns",
          NS_OLPC_BUDDY_PROPS);

      lm_message_node_add_children_from_properties (properties_node,
          priv->properties, "property");
    }

  /* BuddyView.Alias */
  if (priv->alias != NULL)
    {
      lm_message_node_set_attribute (buddy_node, "alias", priv->alias);
    }

  g_free (max_str);
  g_free (id_str);

  if (!_gabble_connection_send_with_reply (view->conn, query,
      buddy_view_query_result_cb, G_OBJECT (self), NULL, NULL))
  {
    g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
      "Failed to send buddy search query to server");

    DEBUG ("Failed to send buddy search query to server");
    lm_message_unref (query);
    return FALSE;
  }

  lm_message_unref (query);
  return TRUE;
}

static void
gabble_olpc_buddy_view_class_init (
    GabbleOlpcBuddyViewClass *gabble_olpc_buddy_view_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_olpc_buddy_view_class);
  GabbleOlpcViewClass *view_class = GABBLE_OLPC_VIEW_CLASS (
      gabble_olpc_buddy_view_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl buddy_view_props[] = {
      { "Properties", "view-properties", NULL },
      { "Alias", "alias", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        buddy_view_props,
      },
      { NULL }
  };

  object_class->get_property = gabble_olpc_buddy_view_get_property;
  object_class->set_property = gabble_olpc_buddy_view_set_property;
  object_class->constructor = gabble_olpc_buddy_view_constructor;

  g_type_class_add_private (gabble_olpc_buddy_view_class,
      sizeof (GabbleOlpcBuddyViewPrivate));

  object_class->dispose = gabble_olpc_buddy_view_dispose;
  object_class->finalize = gabble_olpc_buddy_view_finalize;

  view_class->create_close_msg = gabble_olpc_buddy_view_create_close_msg;
  view_class->send_request = gabble_olpc_buddy_view_send_request;

  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_boxed ("view-properties",
      "View's search properties",
      "The buddy properties Gadget should look for",
      TP_HASH_TYPE_STRING_VARIANT_MAP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_VIEW_PROPERTIES,
      param_spec);

  param_spec = g_param_spec_string ("alias", "View's search alias",
      "The buddy alias Gadget should look for",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ALIAS,
      param_spec);


  gabble_olpc_buddy_view_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleOlpcBuddyViewClass, dbus_props_class));
}

GabbleOlpcBuddyView *
gabble_olpc_buddy_view_new (GabbleConnection *conn,
                            const gchar *object_path,
                            guint id,
                            guint max_size,
                            GHashTable *properties,
                            const gchar *alias)

{
  return g_object_new (GABBLE_TYPE_OLPC_BUDDY_VIEW,
      "object-path", object_path,
      "connection", conn,
      "id", id,
      "max-size", max_size,
      "view-properties", properties,
      "alias", alias,
      NULL);
}
