/* tp-intset.h - Headers for a Glib-link set of integers
 *
 * Copyright (C) 2005, 2006 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2005, 2006 Nokia Corporation
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

#ifndef __TP_INTSET_H__
#define __TP_INTSET_H__

#include <glib-object.h>

typedef struct _TpIntSet TpIntSet;
typedef void (*TpIntFunc) (guint i, gpointer userdata);

TpIntSet *tp_intset_new (void);
void tp_intset_destroy (TpIntSet *set);
void tp_intset_clear (TpIntSet *set);

void tp_intset_add (TpIntSet *set, guint element);
gboolean tp_intset_remove (TpIntSet *set, guint element);
gboolean tp_intset_is_member (const TpIntSet *set, guint element);

void tp_intset_foreach (const TpIntSet *set, TpIntFunc func, gpointer userdata);
GArray *tp_intset_to_array (TpIntSet *set);
TpIntSet *tp_intset_from_array (GArray *array);

guint tp_intset_size (const TpIntSet *set);

gboolean tp_intset_is_equal (const TpIntSet *left, const TpIntSet *right);

TpIntSet *tp_intset_copy (const TpIntSet *orig);
TpIntSet *tp_intset_intersection (const TpIntSet *left, const TpIntSet *right);
TpIntSet *tp_intset_union (const TpIntSet *left, const TpIntSet *right);
TpIntSet *tp_intset_difference (const TpIntSet *left, const TpIntSet *right);
TpIntSet *tp_intset_symmetric_difference (const TpIntSet *left, const TpIntSet *right);

gchar *tp_intset_dump (const TpIntSet *set);

#endif /*__TP_INTSET_H__*/
