/*
 * util.h - Headers for telepathy-glib utility functions
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#include <glib-object.h>

#ifndef __TP_UTIL_H__
#define __TP_UTIL_H__

gboolean tp_g_ptr_array_contains (GPtrArray *haystack, gpointer needle);

void tp_g_value_slice_free (GValue *value);

gboolean tp_strdiff (const gchar *left, const gchar *right);

gpointer tp_mixin_offset_cast (gpointer instance, guint offset);

gchar *tp_escape_as_identifier (const gchar *name);

#endif /* __GABBLE_UTIL_H__ */
