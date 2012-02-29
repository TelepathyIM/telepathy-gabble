/*
 * muc-tube-dbus.h - Header for GabbleMucTubeDBus
 * Copyright (C) 2012 Collabora Ltd.
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

#ifndef __GABBLE_MUC_TUBE_DBUS_H__
#define __GABBLE_MUC_TUBE_DBUS_H__

#include <glib-object.h>

#include "tube-dbus.h"

G_BEGIN_DECLS

typedef struct _GabbleMucTubeDBus GabbleMucTubeDBus;
typedef struct _GabbleMucTubeDBusClass GabbleMucTubeDBusClass;

struct _GabbleMucTubeDBusClass {
  GabbleTubeDBusClass parent_class;
};

struct _GabbleMucTubeDBus {
  GabbleTubeDBus parent;
};

GType gabble_muc_tube_dbus_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_MUC_TUBE_DBUS \
  (gabble_muc_tube_dbus_get_type ())
#define GABBLE_MUC_TUBE_DBUS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MUC_TUBE_DBUS, GabbleMucTubeDBus))
#define GABBLE_MUC_TUBE_DBUS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MUC_TUBE_DBUS,\
                           GabbleMucTubeDBusClass))
#define GABBLE_IS_MUC_TUBE_DBUS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MUC_TUBE_DBUS))
#define GABBLE_IS_MUC_TUBE_DBUS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MUC_TUBE_DBUS))
#define GABBLE_MUC_TUBE_DBUS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MUC_TUBE_DBUS,\
                              GabbleMucTubeDBusClass))

G_END_DECLS

#endif /* #ifndef __GABBLE_MUC_TUBE_DBUS_H__ */
