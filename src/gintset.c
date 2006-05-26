/* gintset.c - Source for a Glib-link set of integers
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

#include <string.h>
#include <glib.h>
#include "gintset.h"

#define DEFAULT_SIZE 16
#define DEFAULT_INCREMENT 8
#define DEFAULT_INCREMENT_LOG2 3

struct _GIntSet
{
  guint32 *bits;
  guint size;
};

static GIntSet *
_g_intset_new_with_size (guint size)
{
  GIntSet *set = g_new(GIntSet, 1);
  set->size = MAX (size, DEFAULT_SIZE);
  set->bits = g_new0 (guint32, set->size);
  return set;
}

GIntSet *
g_intset_new ()
{
  return _g_intset_new_with_size (DEFAULT_SIZE);
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
  g_return_if_fail (set != NULL);

  g_free (set->bits);
  g_free (set);
}

/**
 * g_intset_clear:
 * @set : set
 *
 * Unset every integer in the set.
 */
void
g_intset_clear (GIntSet *set)
{
  g_return_if_fail (set != NULL);

  memset (set->bits, 0, set->size);
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
  guint offset;
  guint newsize;

  g_return_if_fail (set != NULL);

  offset = element >> 5;

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
  guint offset;
  guint mask;

  g_return_val_if_fail (set != NULL, FALSE);

  offset = element >>5;
  mask = 1 << (element & 0x1f);
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
g_intset_is_member (const GIntSet *set, guint element)
{
  guint offset;

  g_return_val_if_fail (set != NULL, FALSE);

  offset = element >>5;
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

  g_return_if_fail (set != NULL);
  g_return_if_fail (func != NULL);

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
GArray *
g_intset_to_array (GIntSet *set)
{
  GArray *array;

  g_return_val_if_fail (set != NULL, NULL);

  array = g_array_new (FALSE, TRUE, sizeof (guint32));

  g_intset_foreach(set, addint, array);

  return array;
}

GIntSet *
g_intset_from_array (GArray *array)
{
  GIntSet *set;
  guint32 max, i;

  g_return_val_if_fail (array != NULL, NULL);

  /* look at the 1st, last and middle values in the array to get an
   * approximation of the largest */
  max = 0;
  if (array->len > 0)
    max = g_array_index (array, guint32, 0);
  if (array->len > 1)
    max = MAX (max, g_array_index (array, guint32, array->len - 1));
  if (array->len > 2)
    max = MAX (max, g_array_index (array, guint32, (array->len - 1) >> 1));
  set = _g_intset_new_with_size (1 + (max >> 5));

  for (i = 0; i < array->len; i++) {
    g_intset_add (set, g_array_index (array, guint32, i));
  }

  return set;
}

int
g_intset_size(const GIntSet *set)
{
  int i,count=0;
  guint32 n;

  g_return_val_if_fail (set != NULL, 0);

  for (i=0; i< set->size ; i++)
  {
    n = set->bits[i];
    n = n - ((n >> 1) & 033333333333) - ((n >> 2) & 011111111111);
    count += ((n + (n >> 3)) & 030707070707) % 63;
  }
  return count;
}

gboolean
g_intset_is_equal (const GIntSet *left, const GIntSet *right)
{
  const GIntSet *large, *small;
  int i;

  g_return_val_if_fail (left != NULL, FALSE);
  g_return_val_if_fail (right != NULL, FALSE);

  if (left->size > right->size)
    {
      large = left;
      small = right;
    }
  else
    {
      large = right;
      small = left;
    }

  for (i = 0; i < small->size; i++)
    {
      if (large->bits[i] != small->bits[i])
        return FALSE;
    }

  for (i = small->size; i < large->size; i++)
    {
      if (large->bits[i] != 0)
        return FALSE;
    }

  return TRUE;
}

GIntSet *
g_intset_copy (const GIntSet *orig)
{
  GIntSet *ret;

  g_return_val_if_fail (orig != NULL, NULL);

  ret = _g_intset_new_with_size (orig->size);
  memcpy (ret->bits, orig->bits, (ret->size * sizeof(guint32)));

  return ret;
}

GIntSet *
g_intset_intersection (const GIntSet *left, const GIntSet *right)
{
  const GIntSet *large, *small;
  GIntSet *ret;
  int i;

  g_return_val_if_fail (left != NULL, NULL);
  g_return_val_if_fail (right != NULL, NULL);

  if (left->size > right->size)
    {
      large = left;
      small = right;
    }
  else
    {
      large = right;
      small = left;
    }

  ret = g_intset_copy (small);

  for (i = 0; i < ret->size; i++)
    {
      ret->bits[i] &= large->bits[i];
    }

  return ret;
}

GIntSet *
g_intset_union (const GIntSet *left, const GIntSet *right)
{
  const GIntSet *large, *small;
  GIntSet *ret;
  int i;

  g_return_val_if_fail (left != NULL, NULL);
  g_return_val_if_fail (right != NULL, NULL);

  if (left->size > right->size)
    {
      large = left;
      small = right;
    }
  else
    {
      large = right;
      small = left;
    }

  ret = g_intset_copy (large);

  for (i = 0; i < small->size; i++)
    {
      ret->bits[i] |= small->bits[i];
    }

  return ret;
}

GIntSet *
g_intset_difference (const GIntSet *left, const GIntSet *right)
{
  GIntSet *ret;
  int i;

  g_return_val_if_fail (left != NULL, NULL);
  g_return_val_if_fail (right != NULL, NULL);

  ret = g_intset_copy (left);

  for (i = 0; i < MIN(right->size, left->size); i++)
    {
      ret->bits[i] &= ~right->bits[i];
    }

  return ret;
}

GIntSet *
g_intset_symmetric_difference (const GIntSet *left, const GIntSet *right)
{
  const GIntSet *large, *small;
  GIntSet *ret;
  int i;

  g_return_val_if_fail (left != NULL, NULL);
  g_return_val_if_fail (right != NULL, NULL);

  if (left->size > right->size)
    {
      large = left;
      small = right;
    }
  else
    {
      large = right;
      small = left;
    }

  ret = g_intset_copy (large);

  for (i = 0; i < small->size; i++)
    {
      ret->bits[i] ^= small->bits[i];
    }

  return ret;
}

