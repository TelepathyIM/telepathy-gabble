/*
 * olpc-gadget-manager - ChannelManager for Gadget views
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

#ifndef GABBLE_OLPC_GADGET_MANAGER_H
#define GABBLE_OLPC_GADGET_MANAGER_H

#include <glib-object.h>
#include <telepathy-glib/handle.h>

#include "olpc-view.h"

G_BEGIN_DECLS

typedef struct _GabbleOlpcGadgetManagerClass GabbleOlpcGadgetManagerClass;
typedef struct _GabbleOlpcGadgetManagerPrivate GabbleOlpcGadgetManagerPrivate;

struct _GabbleOlpcGadgetManagerClass {
  GObjectClass parent_class;
};

struct _GabbleOlpcGadgetManager {
  GObject parent;

  GabbleOlpcGadgetManagerPrivate *priv;
};

GType gabble_olpc_gadget_manager_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_OLPC_GADGET_MANAGER \
  (gabble_olpc_gadget_manager_get_type ())
#define GABBLE_OLPC_GADGET_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_OLPC_GADGET_MANAGER,\
                              GabbleOlpcGadgetManager))
#define GABBLE_OLPC_GADGET_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_OLPC_GADGET_MANAGER,\
                           GabbleOlpcGadgetManagerClass))
#define GABBLE_IS_OLPC_GADGET_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_OLPC_GADGET_MANAGER))
#define GABBLE_IS_OLPC_GADGET_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_OLPC_GADGET_MANAGER))
#define GABBLE_OLPC_GADGET_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_OLPC_GADGET_MANAGER,\
                              GabbleOlpcGadgetManagerClass))

GHashTable * gabble_olpc_gadget_manager_find_buddy_properties (
    GabbleOlpcGadgetManager *self, TpHandle buddy);

GabbleOlpcView * gabble_olpc_gadget_manager_get_view (
    GabbleOlpcGadgetManager *self, guint id);

GPtrArray * gabble_olpc_gadget_manager_find_buddy_activities (
    GabbleOlpcGadgetManager *self, TpHandle contact);

void gabble_olpc_gadget_manager_close_all_views (GabbleOlpcGadgetManager *self);

G_END_DECLS

#endif
