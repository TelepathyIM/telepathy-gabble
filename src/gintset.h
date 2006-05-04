/* gintset.h - Headers for a Glib-link set of integers
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

#ifndef __G_INTSET_H__
#define __G_INTSET_H__

#include <glib-object.h>

typedef struct _GIntSet GIntSet;
typedef void (*GIntFunc)(guint i, gpointer userdata);

GIntSet * g_intset_new ();
void g_intset_destroy (GIntSet *);
void g_intset_clear (GIntSet *set);

void g_intset_add (GIntSet *set, guint element);
gboolean g_intset_remove (GIntSet *set, guint element);
gboolean g_intset_is_member (const GIntSet *set, guint element);

void g_intset_foreach (GIntSet *set, GIntFunc func, gpointer userdata);
GArray* g_intset_to_array (GIntSet *set);

int g_intset_size(const GIntSet *set);

gboolean g_intset_is_equal (const GIntSet *left, const GIntSet *right);

GIntSet *g_intset_copy (const GIntSet *orig);
GIntSet *g_intset_intersection (const GIntSet *left, const GIntSet *right);
GIntSet *g_intset_union (const GIntSet *left, const GIntSet *right);
GIntSet *g_intset_difference (const GIntSet *left, const GIntSet *right);
GIntSet *g_intset_symmetric_difference (const GIntSet *left, const GIntSet *right);

#endif /*__G_INTSET_H__*/
