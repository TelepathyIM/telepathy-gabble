/*
 * allocator.h - mechanism to manage allocations of a limited number of items
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

#ifndef __ALLOCATOR_H__
#define __ALLOCATOR_H__

#include <glib.h>

#include "telepathy-constants.h"

G_BEGIN_DECLS

typedef struct _GabbleAllocator GabbleAllocator;

GabbleAllocator *gabble_allocator_new (gulong size, guint limit);
void gabble_allocator_destroy (GabbleAllocator *alloc);

#define ga_new(alloc, type) \
    ((type *) gabble_allocator_alloc (alloc))
#define ga_new0(alloc, type) \
    ((type *) gabble_allocator_alloc0 (alloc))

gpointer gabble_allocator_alloc (GabbleAllocator *alloc);
gpointer gabble_allocator_alloc0 (GabbleAllocator *alloc);

void gabble_allocator_free (GabbleAllocator *alloc, gpointer thing);

G_END_DECLS

#endif /* #ifndef __ALLOCATOR_H__ */
