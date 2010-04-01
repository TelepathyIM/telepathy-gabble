/*
 * ft-manager.h - Header for GabbleFtManager
 * Copyright (C) 2009 Collabora Ltd.
 *   @author: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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

#ifndef __GABBLE_FT_MANAGER_H__
#define __GABBLE_FT_MANAGER_H__

#include <glib-object.h>

#include "bytestream-iface.h"
#include "types.h"

#include <extensions/_gen/interfaces.h>

G_BEGIN_DECLS

typedef struct _GabbleFtManager GabbleFtManager;
typedef struct _GabbleFtManagerClass GabbleFtManagerClass;
typedef struct _GabbleFtManagerPrivate GabbleFtManagerPrivate;

struct _GabbleFtManagerClass {
    GObjectClass parent_class;
};

struct _GabbleFtManager {
    GObject parent;

    GabbleFtManagerPrivate *priv;
};

GType gabble_ft_manager_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_FT_MANAGER \
  (gabble_ft_manager_get_type ())
#define GABBLE_FT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_FT_MANAGER, GabbleFtManager))
#define GABBLE_FT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_FT_MANAGER, GabbleFtManagerClass))
#define GABBLE_IS_FT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_FT_MANAGER))
#define GABBLE_IS_FT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_FT_MANAGER))
#define GABBLE_FT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_FT_MANAGER, GabbleFtManagerClass))

GabbleFtManager *gabble_ft_manager_new (GabbleConnection *connection);

void gabble_ft_manager_handle_si_request (GabbleFtManager *self,
    GabbleBytestreamIface *bytestream, TpHandle handle, const gchar *stream_id,
    LmMessage *msg);

#ifdef G_OS_UNIX
/* Slight encapsulation violation: this function isn't portable, but we
 * happen to know that it's only needed if we support Unix sockets, and
 * we only do *that* on Unix. Otherwise, we can leave it undefined. */
const gchar * gabble_ft_manager_get_tmp_dir (GabbleFtManager *self);
#endif

G_END_DECLS

#endif /* #ifndef __GABBLE_FT_MANAGER_H__*/
