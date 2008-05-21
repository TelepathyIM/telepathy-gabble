/*
 * olpc-activity-view.c - Source for GabbleOlpcActivityView
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

#include "olpc-activity-view.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <loudmouth/loudmouth.h>
#include <telepathy-glib/dbus.h>

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

typedef struct _GabbleOlpcActivityViewPrivate GabbleOlpcActivityViewPrivate;
struct _GabbleOlpcActivityViewPrivate
{
  GabbleConnection *conn;
  char *object_path;
  guint id;

  TpHandleSet *activities;

  gboolean dispose_has_run;
};

static void activity_view_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (
    GabbleOlpcActivityView, gabble_olpc_activity_view, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_ACTIVITY_VIEW,
      activity_view_iface_init));

#define GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE(obj) \
    ((GabbleOlpcActivityViewPrivate *) obj->priv)


static void
gabble_olpc_activity_view_init (GabbleOlpcActivityView *self)
{
  GabbleOlpcActivityViewPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_OLPC_ACTIVITY_VIEW, GabbleOlpcActivityViewPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}

static void
gabble_olpc_activity_view_dispose (GObject *object)
{
  GabbleOlpcActivityView *self = GABBLE_OLPC_ACTIVITY_VIEW (object);
  GabbleOlpcActivityViewPrivate *priv = GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE (
      self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_olpc_activity_view_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_olpc_activity_view_parent_class)->dispose (object);
}

static void
gabble_olpc_activity_view_finalize (GObject *object)
{
  GabbleOlpcActivityView *self = GABBLE_OLPC_ACTIVITY_VIEW (object);
  GabbleOlpcActivityViewPrivate *priv = GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE (
      self);

  g_free (priv->object_path);
  tp_handle_set_destroy(priv->activities);

  G_OBJECT_CLASS (gabble_olpc_activity_view_parent_class)->finalize (object);
}

static void
gabble_olpc_activity_view_get_property (GObject *object,
                                        guint property_id,
                                        GValue *value,
                                        GParamSpec *pspec)
{
  GabbleOlpcActivityView *self = GABBLE_OLPC_ACTIVITY_VIEW (object);
  GabbleOlpcActivityViewPrivate *priv = GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE (
      self);

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
gabble_olpc_activity_view_set_property (GObject *object,
                                        guint property_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
  GabbleOlpcActivityView *self = GABBLE_OLPC_ACTIVITY_VIEW (object);
  GabbleOlpcActivityViewPrivate *priv = GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE (
      self);

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
gabble_olpc_activity_view_constructor (GType type,
                                    guint n_props,
                                    GObjectConstructParam *props)
{
  GObject *obj;
  GabbleOlpcActivityViewPrivate *priv;
  DBusGConnection *bus;
  TpBaseConnection *conn;
  TpHandleRepoIface *room_repo;

  obj = G_OBJECT_CLASS (gabble_olpc_activity_view_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE (
      GABBLE_OLPC_ACTIVITY_VIEW (obj));
  conn = (TpBaseConnection *)priv->conn;

  priv->object_path = g_strdup_printf ("%s/OlpcActivityView%u",
      conn->object_path, priv->id);
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  room_repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_ROOM);
  priv->activities  = tp_handle_set_new (room_repo);

  return obj;
}

static void
gabble_olpc_activity_view_class_init (
    GabbleOlpcActivityViewClass *gabble_olpc_activity_view_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_olpc_activity_view_class);
  GParamSpec *param_spec;

  object_class->get_property = gabble_olpc_activity_view_get_property;
  object_class->set_property = gabble_olpc_activity_view_set_property;
  object_class->constructor = gabble_olpc_activity_view_constructor;

  g_type_class_add_private (gabble_olpc_activity_view_class,
      sizeof (GabbleOlpcActivityViewPrivate));

  object_class->dispose = gabble_olpc_activity_view_dispose;
  object_class->finalize = gabble_olpc_activity_view_finalize;

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
        G_OBJECT_CLASS_TYPE (gabble_olpc_activity_view_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        gabble_marshal_VOID__VOID,
        G_TYPE_NONE, 0);
}

GabbleOlpcActivityView *
gabble_olpc_activity_view_new (GabbleConnection *conn,
                            guint id)
{
  return g_object_new (GABBLE_TYPE_OLPC_ACTIVITY_VIEW,
      "connection", conn,
      "id", id,
      NULL);
}

static void
olpc_activity_view_close (GabbleSvcOLPCActivityView *iface,
                       DBusGMethodInvocation *context)
{
  GabbleOlpcActivityView *self = GABBLE_OLPC_ACTIVITY_VIEW (iface);
  GabbleOlpcActivityViewPrivate *priv = GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE (
      self);
  LmMessage *msg;
  gchar *id_str;
  GError *error = NULL;

  id_str = g_strdup_printf ("%u", priv->id);

  msg = lm_message_build (priv->conn->olpc_gadget_activity,
      LM_MESSAGE_TYPE_MESSAGE,
      '(', "close", "",
        '@', "xmlns", NS_OLPC_ACTIVITY,
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

  gabble_svc_olpc_activity_view_return_from_close (context);

  lm_message_unref (msg);

  g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
}

void
gabble_olpc_activity_view_add_activities (GabbleOlpcActivityView *self,
                                          TpHandleSet *activities)
{
  GabbleOlpcActivityViewPrivate *priv = GABBLE_OLPC_ACTIVITY_VIEW_GET_PRIVATE (
      self);
  TpIntSet *added;

  added = tp_handle_set_update (priv->activities,
      tp_handle_set_peek (activities));

  tp_intset_destroy (added);
}

static void
activity_view_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleSvcOLPCActivityViewClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_olpc_activity_view_implement_##x (\
    klass, olpc_activity_view_##x)
  IMPLEMENT(close);
#undef IMPLEMENT
}
