/*
 * olpc-view.h - Header for GabbleOlpcView
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

#ifndef __GABBLE_OLPC_VIEW_H__
#define __GABBLE_OLPC_VIEW_H__

#include <glib-object.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/handle-repo.h>

#include "gabble-connection.h"

G_BEGIN_DECLS

typedef enum
{
  GABBLE_OLPC_VIEW_TYPE_BUDDY,
  GABBLE_OLPC_VIEW_TYPE_ACTIVITY,
  NUM_GABBLE_OLPC_VIEW_TYPE
} GabbleOlpcViewType;

typedef struct _GabbleOlpcView GabbleOlpcView;
typedef struct _GabbleOlpcViewClass GabbleOlpcViewClass;

struct _GabbleOlpcViewClass {
  GObjectClass parent_class;
};

struct _GabbleOlpcView {
  GObject parent;

  gpointer priv;
};

GType gabble_olpc_view_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_OLPC_VIEW \
  (gabble_olpc_view_get_type ())
#define GABBLE_OLPC_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_OLPC_VIEW, GabbleOlpcView))
#define GABBLE_OLPC_VIEW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_OLPC_VIEW,\
                           GabbleOlpcViewClass))
#define GABBLE_IS_OLPC_VIEW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_OLPC_VIEW))
#define GABBLE_IS_OLPC_VIEW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_OLPC_VIEW))
#define GABBLE_OLPC_VIEW_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_OLPC_VIEW,\
                              GabbleOlpcViewClass))

GabbleOlpcView * gabble_olpc_view_new (GabbleConnection *conn,
    GabbleOlpcViewType type, guint id);

void gabble_olpc_view_add_buddies (GabbleOlpcView *self,
    GArray *handles, GPtrArray *buddies_properties, TpHandle room);

void gabble_olpc_view_remove_buddies (GabbleOlpcView *self,
    TpHandleSet *handles);

gboolean gabble_olpc_view_set_buddy_properties (GabbleOlpcView *self,
    TpHandle buddy, GHashTable *properties);

GHashTable * gabble_olpc_view_get_buddy_properties (GabbleOlpcView *self,
    TpHandle buddy);

void gabble_olpc_view_add_activities (GabbleOlpcView *self,
    GHashTable *activities);

void gabble_olpc_view_remove_activities (GabbleOlpcView *self,
    TpHandleSet *rooms);

GPtrArray * gabble_olpc_view_get_buddy_activities (GabbleOlpcView *self,
    TpHandle buddy);

void gabble_olpc_view_buddies_left_activity (GabbleOlpcView *self,
    GArray *buddies, TpHandle room);

G_END_DECLS

#endif /* #ifndef __GABBLE_OLPC_VIEW_H__ */
