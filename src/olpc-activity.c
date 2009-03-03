/*
 * olpc-activity.c - Source for GabbleOlpcActivity
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

#include "olpc-activity.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include "debug.h"
#include "extensions/extensions.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_ROOM,
  PROP_ID,
  PROP_PROPERTIES,
  LAST_PROPERTY
};

struct _GabbleOlpcActivityPrivate
{
  GabbleConnection *conn;

  gboolean dispose_has_run;
};

G_DEFINE_TYPE (GabbleOlpcActivity, gabble_olpc_activity, G_TYPE_OBJECT);

static void
gabble_olpc_activity_init (GabbleOlpcActivity *self)
{
  GabbleOlpcActivityPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_OLPC_ACTIVITY, GabbleOlpcActivityPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}

static void
gabble_olpc_activity_dispose (GObject *object)
{
  GabbleOlpcActivity *self = GABBLE_OLPC_ACTIVITY (object);
  GabbleOlpcActivityPrivate *priv = self->priv;
  TpHandleRepoIface *room_repo;

  if (priv->dispose_has_run)
    return;

  room_repo = tp_base_connection_get_handles ((TpBaseConnection *) priv->conn,
      TP_HANDLE_TYPE_ROOM);
  tp_handle_unref (room_repo, self->room);

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_olpc_activity_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_olpc_activity_parent_class)->dispose (object);
}

static void
gabble_olpc_activity_finalize (GObject *object)
{
  GabbleOlpcActivity *self = GABBLE_OLPC_ACTIVITY (object);

  if (self->id != NULL)
    {
      g_free (self->id);
      self->id = NULL;
    }

  if (self->properties != NULL)
    {
      g_hash_table_destroy (self->properties);
      self->properties = NULL;
    }

  G_OBJECT_CLASS (gabble_olpc_activity_parent_class)->finalize (object);
}

static void
gabble_olpc_activity_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabbleOlpcActivity *self = GABBLE_OLPC_ACTIVITY (object);
  GabbleOlpcActivityPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_ROOM:
        g_value_set_uint (value, self->room);
        break;
      case PROP_ID:
        g_value_set_string (value, self->id);
        break;
      case PROP_PROPERTIES:
        g_value_set_boxed (value, self->properties);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_olpc_activity_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabbleOlpcActivity *self = GABBLE_OLPC_ACTIVITY (object);
  GabbleOlpcActivityPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_ROOM:
        self->room = g_value_get_uint (value);
        break;
      case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
      case PROP_PROPERTIES:
        if (self->properties != NULL)
          g_hash_table_destroy (self->properties);

        self->properties = g_value_get_boxed (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_olpc_activity_constructor (GType type,
                                  guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleOlpcActivity *self;
  GabbleOlpcActivityPrivate *priv;
  TpHandleRepoIface *room_repo;

  obj = G_OBJECT_CLASS (gabble_olpc_activity_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_OLPC_ACTIVITY (obj);
  priv = self->priv;

  room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn,
      TP_HANDLE_TYPE_ROOM);

  g_assert (self->room != 0);

  tp_handle_ref (room_repo, self->room);

  DEBUG ("new activity %s (%d)", gabble_olpc_activity_get_room (self),
      self->room);

  return obj;
}

static void
gabble_olpc_activity_class_init (
    GabbleOlpcActivityClass *gabble_olpc_activity_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_olpc_activity_class);
  GParamSpec *param_spec;

  object_class->get_property = gabble_olpc_activity_get_property;
  object_class->set_property = gabble_olpc_activity_set_property;
  object_class->constructor = gabble_olpc_activity_constructor;

  g_type_class_add_private (gabble_olpc_activity_class,
      sizeof (GabbleOlpcActivityPrivate));

  object_class->dispose = gabble_olpc_activity_dispose;
  object_class->finalize = gabble_olpc_activity_finalize;

   param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this activity object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

   param_spec = g_param_spec_uint (
      "room",
      "activity room",
      "a TpHandle representing the activity room",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ROOM, param_spec);

   param_spec = g_param_spec_string (
      "id",
      "activity id",
      "the activity ID",
      NULL,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

   param_spec = g_param_spec_boxed (
      "properties",
      "activity properties",
      "a GHashTable containing activity's properties",
      G_TYPE_HASH_TABLE,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PROPERTIES, param_spec);
}

GabbleOlpcActivity *
gabble_olpc_activity_new (GabbleConnection *conn,
                          TpHandle room)
{
  return g_object_new (GABBLE_TYPE_OLPC_ACTIVITY,
      "connection", conn,
      "room", room,
      NULL);
}

const gchar *
gabble_olpc_activity_get_room (GabbleOlpcActivity *self)
{
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn,
      TP_HANDLE_TYPE_ROOM);

  return tp_handle_inspect (room_repo, self->room);
}

gboolean
gabble_olpc_activity_is_visible (GabbleOlpcActivity *self)
{
   GValue *gv;

  /* false if incomplete */
  if (self->id == NULL || self->properties == NULL)
    return FALSE;

  gv = g_hash_table_lookup (self->properties, "private");
  if (gv == NULL)
    {
      return FALSE;
    }

  /* if they put something non-boolean in it, err on the side of privacy */
  if (!G_VALUE_HOLDS_BOOLEAN (gv))
    return FALSE;

  /* if they specified a privacy level, go with it */
  return !g_value_get_boolean (gv);
}
