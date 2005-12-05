/*
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

#include <string.h>
#include <glib.h>
#include "gintset.h"

#define DEFAULT_SIZE 32
#define DEFAULT_INCREMENT 8
#define DEFAULT_INCREMENT_LOG2 3

struct _GIntSet
{
  guint32 *bits;
  guint size;
};

GIntSet *
g_intset_new ()
{
  GIntSet *set= g_new(GIntSet,1);
  set->bits = g_new0(guint32, DEFAULT_SIZE);
  set->size = DEFAULT_SIZE;
  return set;
}

/**
 * g_intset_destroy:
 * @set: set
 *
 * delete the set
 */
void
g_intset_destroy (GIntSet *set)
{
  g_free (set->bits);
  g_free (set);
}

/**
 * g_intset_add:
 * @set: set
 * @element: integer to add
 *
 * Add an integer into a GIntSet
 */
void
g_intset_add (GIntSet *set, guint element)
{
  guint offset = element>>5;
  guint newsize;
  if (offset >= set->size)
  {
    newsize = ((offset>>DEFAULT_INCREMENT_LOG2) +1 ) << DEFAULT_INCREMENT_LOG2;
    set->bits = g_renew(guint32, set->bits, newsize);
    memset (set->bits + set->size, 0, sizeof(guint32) * (newsize - set->size));
    set->size = newsize; 
    g_assert(offset<newsize);
  }
  set->bits[offset] = set->bits[offset] | (1<<(element & 0x1f));
}

/**
 * g_intset_remove:
 * @set: set
 * @element: integer to add
 *
 * Remove an integer from a GIntSet
 * Returns: TRUE if element was in set 
 */
gboolean
g_intset_remove (GIntSet *set, guint element)
{
  guint offset = element >>5;
  guint mask = 1 << (element & 0x1f);
  if (offset >= set->size)
    return FALSE;
  else if (! (set->bits[offset] & mask))
    return FALSE;
  else
  {
    set->bits[offset] &= ~mask;
    return TRUE;
  }
}

/**
 * g_intset_is_member:
 * @set: set
 * @element: integer to test 
 *
 * Tests if @element is a member of @set
 * Returns: TRUE if element was in set 
 */
gboolean
g_intset_is_member (GIntSet *set, guint element)
{
  guint offset = element >>5;
  if (offset >= set->size)
    return FALSE;
  else
    return (set->bits[offset] & (1 << (element & 0x1f)));
}

/**
 * g_intset_foreach:
 * @set: set
 * @func: @GIntFunc to use to iterate the set
 * @userdata: user data to pass to each call of @func
 *
 * Iterates every member of the set calling @func
 */

void 
g_intset_foreach (GIntSet *set, GIntFunc func, gpointer userdata)
{
  guint i, j;
  for (i=0; i<set->size; i++)
  {
    for (j=0; j<32; j++)
    {
      if (set->bits[i] & 1<<j)
        func(i*32 +j, userdata);
    }
  }
}


static void 
addint(guint32 i, gpointer data)
{
  GArray * array = (GArray *) data;
  g_array_append_val (array, i);
}

/**
 * g_intset_to_array:
 * @set: set to convert
 * Convert a gintset to an array, allocates the array for you, hence you
 * must free it after use.
 */
GArray* 
g_intset_to_array (GIntSet *set)
{
  GArray *array;
  
  array = g_array_new (FALSE, TRUE, sizeof (guint32));

  g_intset_foreach(set, addint, array);
  return array;
}

int 
g_intset_size(GIntSet *set)
{
  int i,count=0;
  guint32 n;

  for (i=0; i< set->size ; i++)
  {
    n = set->bits[i];
    n = n - ((n >> 1) & 033333333333) - ((n >> 2) & 011111111111);
    count += ((n + (n >> 3)) & 030707070707) % 63;
  }
  return count;
}
