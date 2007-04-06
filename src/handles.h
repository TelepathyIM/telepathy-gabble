/*
 * handles.h - mechanism to store and retrieve handles on a connection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __HANDLES_H__
#define __HANDLES_H__

#include <glib.h>

#include <telepathy-glib/handle-repo.h>

#include "gabble-types.h"
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

gboolean gabble_handle_jid_is_valid (TpHandleType type, const gchar *jid,
    GError **error);

G_END_DECLS

#endif /* #ifndef __HANDLES_H__ */
