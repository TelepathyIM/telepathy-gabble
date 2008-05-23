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
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/group-mixin.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include "debug.h"
#include "extensions/extensions.h"
#include "gabble-connection.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

/* signals */
enum
{
  CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_ID,
  LAST_PROPERTY
};

typedef struct _GabbleOlpcBuddyViewPrivate GabbleOlpcBuddyViewPrivate;
struct _GabbleOlpcBuddyViewPrivate
{
  GabbleConnection *conn;
  char *object_path;
  guint id;

  /* TpHandle => GHashTable * */
  GHashTable *buddy_properties;

  gboolean dispose_has_run;
};

static void buddy_view_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (
    GabbleOlpcBuddyView, gabble_olpc_buddy_view, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_BUDDY_VIEW,
      buddy_view_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init));

#define GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE(obj) \
    ((GabbleOlpcBuddyViewPrivate *) obj->priv)


static void
gabble_olpc_buddy_view_init (GabbleOlpcBuddyView *self)
{
  GabbleOlpcBuddyViewPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_OLPC_BUDDY_VIEW, GabbleOlpcBuddyViewPrivate);

  self->priv = priv;

  priv->buddy_properties = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_hash_table_unref);

  priv->dispose_has_run = FALSE;
}

static void
gabble_olpc_buddy_view_dispose (GObject *object)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (object);
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  if (priv->buddy_properties != NULL)
    {
      g_hash_table_destroy (priv->buddy_properties);
      priv->buddy_properties = NULL;
    }

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->dispose (object);
}

static void
gabble_olpc_buddy_view_finalize (GObject *object)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (object);
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);

  g_free (priv->object_path);

  tp_group_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->finalize (object);
}

static void
gabble_olpc_buddy_view_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (object);
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_ID:
        g_value_set_uint (value, priv->id);
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
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_ID:
        priv->id = g_value_get_uint (value);
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
  GabbleOlpcBuddyViewPrivate *priv;
  DBusGConnection *bus;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_handles;

  obj = G_OBJECT_CLASS (gabble_olpc_buddy_view_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (GABBLE_OLPC_BUDDY_VIEW (obj));
  conn = (TpBaseConnection *)priv->conn;

  priv->object_path = g_strdup_printf ("%s/OlpcBuddyView%u",
      conn->object_path, priv->id);
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  /* initialize group mixin */
  tp_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleOlpcBuddyView, group),
      contact_handles, 0);

  /* set initial group flags */
  tp_group_mixin_change_flags (obj, 0, 0);

  return obj;
}

static gboolean
view_add_member (GObject *obj,
                 TpHandle handle,
                 const gchar *message,
                 GError **error)
{
  /* this function is never supposed to be called */
  g_return_val_if_reached (TRUE);
}

static void
gabble_olpc_buddy_view_class_init (GabbleOlpcBuddyViewClass *gabble_olpc_buddy_view_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_olpc_buddy_view_class);
  GParamSpec *param_spec;

  object_class->get_property = gabble_olpc_buddy_view_get_property;
  object_class->set_property = gabble_olpc_buddy_view_set_property;
  object_class->constructor = gabble_olpc_buddy_view_constructor;

  g_type_class_add_private (gabble_olpc_buddy_view_class,
      sizeof (GabbleOlpcBuddyViewPrivate));

  object_class->dispose = gabble_olpc_buddy_view_dispose;
  object_class->finalize = gabble_olpc_buddy_view_finalize;

   param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this view object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string (
      "object-path",
      "D-Bus object path",
      "The D-Bus object path of this view object",
      NULL,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_uint (
      "id",
      "query ID",
      "The ID of the query associated with this view",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
        G_OBJECT_CLASS_TYPE (gabble_olpc_buddy_view_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        gabble_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleOlpcBuddyViewClass, group_class),
      view_add_member, NULL);
}

GabbleOlpcBuddyView *
gabble_olpc_buddy_view_new (GabbleConnection *conn,
                            guint id)
{
  return g_object_new (GABBLE_TYPE_OLPC_BUDDY_VIEW,
      "connection", conn,
      "id", id,
      NULL);
}

static void
olpc_buddy_view_close (GabbleSvcOLPCBuddyView *iface,
                       DBusGMethodInvocation *context)
{
  GabbleOlpcBuddyView *self = GABBLE_OLPC_BUDDY_VIEW (iface);
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);
  LmMessage *msg;
  gchar *id_str;
  GError *error = NULL;

  id_str = g_strdup_printf ("%u", priv->id);

  msg = lm_message_build (priv->conn->olpc_gadget_buddy,
      LM_MESSAGE_TYPE_MESSAGE,
      '(', "close", "",
        '@', "xmlns", NS_OLPC_BUDDY,
        '@', "id", id_str,
      ')', NULL);
  g_free (id_str);

  if (!_gabble_connection_send (priv->conn, msg, &error))
    {
      dbus_g_method_return_error (context, error);
      lm_message_unref (msg);
      g_error_free (error);
      return;
    }

  gabble_svc_olpc_buddy_view_return_from_close (context);

  lm_message_unref (msg);

  g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
}

void
gabble_olpc_buddy_view_add_buddies (GabbleOlpcBuddyView *self,
                                    TpHandleSet *buddies,
                                    GPtrArray *buddies_properties)
{
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);
  TpIntSet *empty;
  GArray *buddies_array;
  guint i;

  empty = tp_intset_new ();
  buddies_array = tp_handle_set_to_array (buddies);

  g_assert (buddies_array->len == buddies_properties->len);

  tp_group_mixin_change_members (G_OBJECT (self), "",
      tp_handle_set_peek (buddies), empty, empty, empty,
      0, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  /* store properties */
  for (i = 0; i < buddies_array->len; i++)
    {
      TpHandle handle;
      GHashTable *properties;

      handle = g_array_index (buddies_array, TpHandle, i);
      properties = g_ptr_array_index (buddies_properties, i);

      g_hash_table_insert (priv->buddy_properties, GUINT_TO_POINTER (handle),
          properties);
      g_hash_table_ref (properties);
    }

  tp_intset_destroy (empty);
  g_array_free (buddies_array, TRUE);
}

static void
remove_properties_foreach (TpHandleSet *buddies,
                           TpHandle handle,
                           GabbleOlpcBuddyView *self)
{
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);

  g_hash_table_remove (priv->buddy_properties, GUINT_TO_POINTER (handle));
}

void
gabble_olpc_buddy_view_remove_buddies (GabbleOlpcBuddyView *self,
                                       TpHandleSet *buddies)
{
  TpIntSet *empty;

  empty = tp_intset_new ();

  tp_group_mixin_change_members (G_OBJECT (self), "",
      empty, tp_handle_set_peek (buddies), empty, empty,
      0, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  tp_handle_set_foreach (buddies,
      (TpHandleSetMemberFunc) remove_properties_foreach, self);

  tp_intset_destroy (empty);
}

gboolean
gabble_olpc_buddy_view_set_properties (GabbleOlpcBuddyView *self,
                                       TpHandle buddy,
                                       GHashTable *properties)
{
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);

  if (!tp_handle_set_is_member (self->group.members, buddy))
    {
      DEBUG ("buddy %d is not member of this view", buddy);
      return FALSE;
    }

  g_hash_table_insert (priv->buddy_properties, GUINT_TO_POINTER (buddy),
      properties);
  g_hash_table_ref (properties);

  return TRUE;
}

GHashTable *
gabble_olpc_buddy_view_get_properties (GabbleOlpcBuddyView *self,
                                       TpHandle buddy)
{
  GabbleOlpcBuddyViewPrivate *priv = GABBLE_OLPC_BUDDY_VIEW_GET_PRIVATE (self);

  return g_hash_table_lookup (priv->buddy_properties, GUINT_TO_POINTER (buddy));
}

static void
buddy_view_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleSvcOLPCBuddyViewClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_olpc_buddy_view_implement_##x (\
    klass, olpc_buddy_view_##x)
  IMPLEMENT(close);
#undef IMPLEMENT
}
