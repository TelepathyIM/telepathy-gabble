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

#include "telepathy-constants.h"

G_BEGIN_DECLS

typedef struct _GabbleHandleRepo GabbleHandleRepo;
typedef GQuark GabbleHandle;

void gabble_handle_decode_jid (const char *jid, char **username, char **server, char **resource);

GabbleHandleRepo *gabble_handle_repo_new ();
void gabble_handle_repo_destroy (GabbleHandleRepo *repo);

gboolean gabble_handle_ref (GabbleHandleRepo *repo, TpHandleType type, GabbleHandle handle);
gboolean gabble_handle_unref (GabbleHandleRepo *repo, TpHandleType type, GabbleHandle handle);
const char *gabble_handle_inspect (GabbleHandleRepo *repo, TpHandleType type, GabbleHandle handle);

GabbleHandle gabble_handle_for_contact (GabbleHandleRepo *repo, char *jid, gboolean with_resource);
GabbleHandle gabble_handle_for_list_publish (GabbleHandleRepo *repo);
GabbleHandle gabble_handle_for_list_subscribe (GabbleHandleRepo *repo);

G_END_DECLS

#endif /* #ifndef __HANDLES_H__ */
