/*
 * auth-manager.h - Header for GabbleAuthManager
 * Copyright (C) 2010 Collabora Ltd.
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

#ifndef __AUTH_MANAGER_H__
#define __AUTH_MANAGER_H__

#include <glib-object.h>
#include <wocky/wocky-auth-registry.h>
#include <telepathy-glib/handle.h>

#include "extensions/extensions.h"

G_BEGIN_DECLS

typedef struct _GabbleAuthManager GabbleAuthManager;
typedef struct _GabbleAuthManagerClass GabbleAuthManagerClass;
typedef struct _GabbleAuthManagerPrivate GabbleAuthManagerPrivate;

struct _GabbleAuthManagerClass {
  WockyAuthRegistryClass parent_class;
};

struct _GabbleAuthManager {
  WockyAuthRegistry parent;
  GabbleAuthManagerPrivate *priv;
};

GType gabble_auth_manager_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_AUTH_MANAGER \
  (gabble_auth_manager_get_type ())
#define GABBLE_AUTH_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_AUTH_MANAGER, GabbleAuthManager))
#define GABBLE_AUTH_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_AUTH_MANAGER,\
                           GabbleAuthManagerClass))
#define GABBLE_IS_AUTH_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_AUTH_MANAGER))
#define GABBLE_IS_AUTH_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_AUTH_MANAGER))
#define GABBLE_AUTH_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_AUTH_MANAGER,\
                              GabbleAuthManagerClass))

G_END_DECLS

#endif /* #ifndef __AUTH_MANAGER_H__ */
