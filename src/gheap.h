/*
 * Header file for GHeap
 *
 * Copyright (C) 2006 Nokia Corporation. All rights reserved.
 *
 * Contact: Olli Salli (Nokia-M/Helsinki) <olli.salli@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
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

#ifndef __G_HEAP_H__
#define __G_HEAP_H__

#include <glib.h>

typedef struct _GHeap GHeap;

GHeap *g_heap_new (GCompareFunc comparator);
void g_heap_destroy (GHeap *);
void g_heap_clear (GHeap *);

void g_heap_add (GHeap *heap, gpointer element);
gpointer g_heap_peek_first (GHeap *heap);
gpointer g_heap_extract_first (GHeap *heap);

guint g_heap_size (GHeap *heap);

#endif
