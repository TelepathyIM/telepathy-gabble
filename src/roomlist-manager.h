/*
 * roomlist-manager - ChannelManager for room lists
 * Copyright (C) 2006-2008 Collabora Ltd.
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

#ifndef GABBLE_ROOMLIST_MANAGER_H
#define GABBLE_ROOMLIST_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleRoomlistManager GabbleRoomlistManager;
typedef struct _GabbleRoomlistManagerClass GabbleRoomlistManagerClass;
typedef struct _GabbleRoomlistManagerPrivate GabbleRoomlistManagerPrivate;

struct _GabbleRoomlistManagerClass {
  GObjectClass parent_class;
};

struct _GabbleRoomlistManager {
  GObject parent;

  GabbleRoomlistManagerPrivate *priv;
};

GType gabble_roomlist_manager_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_ROOMLIST_MANAGER \
  (gabble_roomlist_manager_get_type ())
#define GABBLE_ROOMLIST_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_ROOMLIST_MANAGER,\
                              GabbleRoomlistManager))
#define GABBLE_ROOMLIST_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_ROOMLIST_MANAGER,\
                           GabbleRoomlistManagerClass))
#define GABBLE_IS_ROOMLIST_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_ROOMLIST_MANAGER))
#define GABBLE_IS_ROOMLIST_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_ROOMLIST_MANAGER))
#define GABBLE_ROOMLIST_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_ROOMLIST_MANAGER,\
                              GabbleRoomlistManagerClass))

G_END_DECLS

#endif
