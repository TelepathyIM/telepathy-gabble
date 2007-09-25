/*
 * tube-dbus.h - Header for GabbleTubeDBus
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

#ifndef __GABBLE_TUBE_DBUS_H__
#define __GABBLE_TUBE_DBUS_H__

#include <glib-object.h>

#include "gabble-connection.h"
#include "bytestream-iface.h"

G_BEGIN_DECLS

typedef struct _GabbleTubeDBus GabbleTubeDBus;
typedef struct _GabbleTubeDBusClass GabbleTubeDBusClass;

struct _GabbleTubeDBusClass {
  GObjectClass parent_class;
};

struct _GabbleTubeDBus {
  GObject parent;

  gpointer priv;
};

GType gabble_tube_dbus_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_TUBE_DBUS \
  (gabble_tube_dbus_get_type ())
#define GABBLE_TUBE_DBUS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_TUBE_DBUS, GabbleTubeDBus))
#define GABBLE_TUBE_DBUS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_TUBE_DBUS,\
                           GabbleTubeDBusClass))
#define GABBLE_IS_TUBE_DBUS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_TUBE_DBUS))
#define GABBLE_IS_TUBE_DBUS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_TUBE_DBUS))
#define GABBLE_TUBE_DBUS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_TUBE_DBUS,\
                              GabbleTubeDBusClass))

GabbleTubeDBus * gabble_tube_dbus_new (GabbleConnection *conn, TpHandle handle,
    TpHandleType handle_type, TpHandle self_handle, TpHandle initiator,
    const gchar *service, GHashTable *parameters, const gchar *stream_id,
    guint id, GabbleBytestreamIface *bytestream);

void gabble_tube_dbus_add_name (GabbleTubeDBus *tube, TpHandle handle,
    const gchar *name);

gboolean gabble_tube_dbus_remove_name (GabbleTubeDBus *tube, TpHandle handle);

gboolean gabble_tube_dbus_handle_in_names (GabbleTubeDBus *tube,
    TpHandle handle);

/* Only extern for the benefit of tests/test-dtube-unique-names.c */
gchar * _gabble_generate_dbus_unique_name (const gchar *nick);

G_END_DECLS

#endif /* #ifndef __GABBLE_TUBE_DBUS_H__ */
