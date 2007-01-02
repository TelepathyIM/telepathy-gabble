/* intset.c - Source for a set of unsigned integers (implemented as a
 * variable length bitfield)
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

#include "telepathy-glib/tp-intset.h"

#define DEFAULT_SIZE 16
#define DEFAULT_INCREMENT 8
#define DEFAULT_INCREMENT_LOG2 3

struct _TpIntSet
{
  guint32 *bits;
  guint size;
};

static TpIntSet *
_tp_intset_new_with_size (guint size)
{
  TpIntSet *set = g_slice_new (TpIntSet);
  set->size = MAX (size, DEFAULT_SIZE);
  set->bits = g_new0 (guint32, set->size);
  return set;
}

TpIntSet *
tp_intset_new ()
{
  return _tp_intset_new_with_size (DEFAULT_SIZE);
}

/**
 * tp_intset_destroy:
 * @set: set
 *
 * delete the set
 */
void
tp_intset_destroy (TpIntSet *set)
{
  g_return_if_fail (set != NULL);

  g_free (set->bits);
  g_slice_free (TpIntSet, set);
}

/**
 * tp_intset_clear:
 * @set : set
 *
 * Unset every integer in the set.
 */
void
tp_intset_clear (TpIntSet *set)
{
  g_return_if_fail (set != NULL);

  memset (set->bits, 0, set->size * sizeof (guint32));
}

/**
 * tp_intset_add:
 * @set: set
 * @element: integer to add
 *
 * Add an integer into a TpIntSet
 */
void
tp_intset_add (TpIntSet *set, guint element)
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
 * tp_intset_remove:
 * @set: set
 * @element: integer to add
 *
 * Remove an integer from a TpIntSet
 * Returns: TRUE if element was in set
 */
gboolean
tp_intset_remove (TpIntSet *set, guint element)
{
  guint offset;
  guint mask;

  g_return_val_if_fail (set != NULL, FALSE);

  offset = element >> 5;
  mask = 1 << (element & 0x1f);
  if (offset >= set->size)
    return FALSE;
  else if (!(set->bits[offset] & mask))
    return FALSE;
  else
    {
      set->bits[offset] &= ~mask;
      return TRUE;
    }
}

/**
 * tp_intset_is_member:
 * @set: set
 * @element: integer to test
 *
 * Tests if @element is a member of @set
 * Returns: TRUE if element was in set
 */
gboolean
tp_intset_is_member (const TpIntSet *set, guint element)
{
  guint offset;

  g_return_val_if_fail (set != NULL, FALSE);

  offset = element >> 5;
  if (offset >= set->size)
    return FALSE;
  else
    return (set->bits[offset] & (1 << (element & 0x1f)));
}

/**
 * tp_intset_foreach:
 * @set: set
 * @func: @TpIntFunc to use to iterate the set
 * @userdata: user data to pass to each call of @func
 *
 * Iterates every member of the set calling @func
 */

void
tp_intset_foreach (const TpIntSet *set, TpIntFunc func, gpointer userdata)
{
  guint i, j;

  g_return_if_fail (set != NULL);
  g_return_if_fail (func != NULL);

  for (i = 0; i < set->size; i++)
    {
      if (set->bits[i])
        for (j = 0; j < 32; j++)
          {
            if (set->bits[i] & 1 << j)
              func (i * 32 + j, userdata);
          }
    }
}


static void
addint (guint32 i, gpointer data)
{
  GArray *array = (GArray *) data;
  g_array_append_val (array, i);
}

/**
 * tp_intset_to_array:
 * @set: set to convert
 * Convert a TpIntSet to an array, which must be freed with g_array_free by
 * the caller.
 */
GArray *
tp_intset_to_array (TpIntSet *set)
{
  GArray *array;

  g_return_val_if_fail (set != NULL, NULL);

  array = g_array_new (FALSE, TRUE, sizeof (guint32));

  tp_intset_foreach (set, addint, array);

  return array;
}

TpIntSet *
tp_intset_from_array (GArray *array)
{
  TpIntSet *set;
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
  set = _tp_intset_new_with_size (1 + (max >> 5));

  for (i = 0; i < array->len; i++)
    {
      tp_intset_add (set, g_array_index (array, guint32, i));
    }

  return set;
}

guint
tp_intset_size (const TpIntSet *set)
{
  guint i, count = 0;
  guint32 n;

  g_return_val_if_fail (set != NULL, 0);

  for (i = 0; i < set->size; i++)
    {
      n = set->bits[i];
      n = n - ((n >> 1) & 033333333333) - ((n >> 2) & 011111111111);
      count += ((n + (n >> 3)) & 030707070707) % 63;
    }

  return count;
}

gboolean
tp_intset_is_equal (const TpIntSet *left, const TpIntSet *right)
{
  const TpIntSet *large, *small;
  guint i;

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

TpIntSet *
tp_intset_copy (const TpIntSet *orig)
{
  TpIntSet *ret;

  g_return_val_if_fail (orig != NULL, NULL);

  ret = _tp_intset_new_with_size (orig->size);
  memcpy (ret->bits, orig->bits, (ret->size * sizeof (guint32)));

  return ret;
}

TpIntSet *
tp_intset_intersection (const TpIntSet *left, const TpIntSet *right)
{
  const TpIntSet *large, *small;
  TpIntSet *ret;
  guint i;

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

  ret = tp_intset_copy (small);

  for (i = 0; i < ret->size; i++)
    {
      ret->bits[i] &= large->bits[i];
    }

  return ret;
}

TpIntSet *
tp_intset_union (const TpIntSet *left, const TpIntSet *right)
{
  const TpIntSet *large, *small;
  TpIntSet *ret;
  guint i;

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

  ret = tp_intset_copy (large);

  for (i = 0; i < small->size; i++)
    {
      ret->bits[i] |= small->bits[i];
    }

  return ret;
}

TpIntSet *
tp_intset_difference (const TpIntSet *left, const TpIntSet *right)
{
  TpIntSet *ret;
  guint i;

  g_return_val_if_fail (left != NULL, NULL);
  g_return_val_if_fail (right != NULL, NULL);

  ret = tp_intset_copy (left);

  for (i = 0; i < MIN (right->size, left->size); i++)
    {
      ret->bits[i] &= ~right->bits[i];
    }

  return ret;
}

TpIntSet *
tp_intset_symmetric_difference (const TpIntSet *left, const TpIntSet *right)
{
  const TpIntSet *large, *small;
  TpIntSet *ret;
  guint i;

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

  ret = tp_intset_copy (large);

  for (i = 0; i < small->size; i++)
    {
      ret->bits[i] ^= small->bits[i];
    }

  return ret;
}

static void
_dump_foreach (guint i, gpointer data)
{
   GString *tmp = (GString *) data;

  if (tmp->len == 0)
    g_string_append_printf (tmp, "%d", i);
  else
    g_string_append_printf (tmp, " %d", i);
}

gchar *
Tp_intset_dump (const TpIntSet *set)
{
  GString *tmp = g_string_new ("");

  tp_intset_foreach (set, _dump_foreach, tmp);
  return g_string_free (tmp, FALSE);
}
