/*
 * olpc-activity-view.h - Header for GabbleOlpcActivityView
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

#ifndef __GABBLE_OLPC_ACTIVITY_VIEW_H__
#define __GABBLE_OLPC_ACTIVITY_VIEW_H__

#include <glib-object.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/handle-repo.h>

#include "olpc-view.h"

G_BEGIN_DECLS

typedef struct _GabbleOlpcActivityView GabbleOlpcActivityView;
typedef struct _GabbleOlpcActivityViewClass GabbleOlpcActivityViewClass;
typedef struct _GabbleOlpcActivityViewPrivate GabbleOlpcActivityViewPrivate;

struct _GabbleOlpcActivityViewClass {
  GabbleOlpcViewClass parent_class;

  TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleOlpcActivityView {
  GabbleOlpcView parent;

  GabbleOlpcActivityViewPrivate *priv;
};

GType gabble_olpc_activity_view_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_OLPC_ACTIVITY_VIEW \
  (gabble_olpc_activity_view_get_type ())
#define GABBLE_OLPC_ACTIVITY_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_OLPC_ACTIVITY_VIEW, GabbleOlpcActivityView))
#define GABBLE_OLPC_ACTIVITY_VIEW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_OLPC_ACTIVITY_VIEW,\
                           GabbleOlpcActivityViewClass))
#define GABBLE_IS_OLPC_ACTIVITY_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_OLPC_ACTIVITY_VIEW))
#define GABBLE_IS_OLPC_ACTIVITY_VIEW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_OLPC_ACTIVITY_VIEW))
#define GABBLE_OLPC_ACTIVITY_VIEW_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_OLPC_ACTIVITY_VIEW,\
                              GabbleOlpcActivityViewClass))

GabbleOlpcActivityView * gabble_olpc_activity_view_new (GabbleConnection *conn,
    const gchar *object_path, guint id, guint max_size, GHashTable *properties,
    const GArray *participants);

G_END_DECLS

#endif /* #ifndef __GABBLE_OLPC_ACTIVITY_VIEW_H__ */
