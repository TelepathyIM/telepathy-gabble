/*
 * base-connection-manager.c - Source for TpBaseConnectionManager
 *
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
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

#include <telepathy-glib/base-connection-manager.h>

#define BUS_NAME_BASE    "org.freedesktop.Telepathy.ConnectionManager"
#define OBJECT_PATH_BASE "/org/freedesktop/Telepathy/ConnectionManager"

G_DEFINE_ABSTRACT_TYPE(TpBaseConnectionManager, tp_base_connection_manager, G_TYPE_OBJECT)

#define TP_BASE_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((TpBaseConnectionManagerPrivate *)obj->priv)

typedef struct _TpBaseConnectionManagerPrivate
{
  /* if TRUE, the object has gone away */
  gboolean dispose_has_run;
} TpBaseConnectionManagerPrivate;

static void
tp_base_connection_manager_dispose (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);
  TpBaseConnectionManagerPrivate *priv = 
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
}

static void
tp_base_connection_manager_finalize (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);

  (void)self;

  G_OBJECT_CLASS (tp_base_connection_manager_parent_class)->finalize (object);
}

static void
tp_base_connection_manager_class_init (TpBaseConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TpBaseConnectionManagerPrivate));
  object_class->dispose = tp_base_connection_manager_dispose;
  object_class->finalize = tp_base_connection_manager_finalize;
}

static void
tp_base_connection_manager_init (TpBaseConnectionManager *self)
{
  TpBaseConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TP_TYPE_BASE_CONNECTION_MANAGER, TpBaseConnectionManagerPrivate);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);

  (void)cls;

  self->priv = priv;
}
