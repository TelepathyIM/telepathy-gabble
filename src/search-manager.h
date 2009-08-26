/*
 * search-manager.h - Header for GabbleSearchManager
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __SEARCH_MANAGER_H__
#define __SEARCH_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleSearchManager GabbleSearchManager;
typedef struct _GabbleSearchManagerClass GabbleSearchManagerClass;
typedef struct _GabbleSearchManagerPrivate GabbleSearchManagerPrivate;

struct _GabbleSearchManagerClass {
  GObjectClass parent_class;
};

struct _GabbleSearchManager {
  GObject parent;
  GabbleSearchManagerPrivate *priv;
};

GType gabble_search_manager_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_SEARCH_MANAGER \
  (gabble_search_manager_get_type ())
#define GABBLE_SEARCH_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_SEARCH_MANAGER, GabbleSearchManager))
#define GABBLE_SEARCH_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_SEARCH_MANAGER,\
                           GabbleSearchManagerClass))
#define GABBLE_IS_SEARCH_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_SEARCH_MANAGER))
#define GABBLE_IS_SEARCH_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_SEARCH_MANAGER))
#define GABBLE_SEARCH_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_SEARCH_MANAGER,\
                              GabbleSearchManagerClass))


G_END_DECLS

#endif /* #ifndef __SEARCH_MANAGER_H__ */
