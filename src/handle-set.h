/*
 * handle-set.h - a set which refs a handle when inserted
 *
 * Copyright (C) 2005,2006 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __HANDLE_SET_H__
#define __HANDLE_SET_H__

typedef struct _GabbleHandleSet GabbleHandleSet;
typedef void (*GabbleHandleFunc)(GabbleHandleSet *set, GabbleHandle handle, gpointer userdata);

GabbleHandleSet * handle_set_new (GabbleHandleRepo *, TpHandleType type);
void handle_set_destroy (GabbleHandleSet *);

void handle_set_add (GabbleHandleSet *set, GabbleHandle handle);
gboolean handle_set_remove (GabbleHandleSet *set, GabbleHandle handle);
gboolean handle_set_is_member (GabbleHandleSet *set, GabbleHandle handle);

void handle_set_foreach (GabbleHandleSet *set, GabbleHandleFunc func, gpointer userdata);

int handle_set_size (GabbleHandleSet *set);

#endif /*__HANDLE_SET_H__*/
