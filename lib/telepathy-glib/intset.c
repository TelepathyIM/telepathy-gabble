/* intset.c - Source for a set of unsigned integers (implemented as a
 * variable length bitfield)
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

/**
 * SECTION:intset
 * @title: TpIntSet
 * @short_description: a set of unsigned integers
 * @see_also: #TpHandleSet
 *
 * A #TpIntSet is a set of unsigned integers, implemented as a
 * dynamically-allocated bitfield.
 */

#include <telepathy-glib/intset.h>

#include <string.h>
#include <glib.h>

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

/**
 * tp_intset_sized_new:
 * @size: 1 more than the largest integer you expect to store
 *
 * Allocate an integer set just large enough to store the given number of bits,
 * rounded up as necessary.
 *
 * The set will still expand automatically if you store larger integers;
 * this is just an optimization to avoid wasting memory (if the set is too
 * large) or time (if the set is too small and needs reallocation).
 *
 * Returns: a new, empty integer set to be destroyed with tp_intset_destroy()
 */
TpIntSet *
tp_intset_sized_new (guint size)
{
  /* convert from a size in bits to a size in 32-bit words */
  if (G_UNLIKELY (size == 0))
    size = 1;
  else
    size = ((size - 1) >> 5) + 1;

  return _tp_intset_new_with_size (size);
}

/**
 * tp_intset_new:
 *
 * Allocate a new integer set with a default memory allocation.
 *
 * Returns: a new, empty integer set to be destroyed with tp_intset_destroy()
 */
TpIntSet *
tp_intset_new ()
{
  return _tp_intset_new_with_size (DEFAULT_SIZE);
}

/**
 * tp_intset_destroy:
 * @set: set
 *
 * Free all memory used by the set.
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
 * @set: set
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
 * Add an integer into a TpIntSet.
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
    set->bits = g_renew (guint32, set->bits, newsize);
    memset (set->bits + set->size, 0,
        sizeof (guint32) * (newsize - set->size));
    set->size = newsize;
    g_assert (offset<newsize);
  }
  set->bits[offset] = set->bits[offset] | (1<<(element & 0x1f));
}

/**
 * tp_intset_remove:
 * @set: set
 * @element: integer to add
 *
 * Remove an integer from a TpIntSet
 *
 * Returns: %TRUE if @element was previously in @set
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

static inline gboolean
_tp_intset_is_member (const TpIntSet *set, guint element)
{
  guint offset;

  offset = element >> 5;
  if (offset >= set->size)
    return FALSE;
  else
    return (set->bits[offset] & (1 << (element & 0x1f))) != 0;
}

/**
 * tp_intset_is_member:
 * @set: set
 * @element: integer to test
 *
 * Tests if @element is a member of @set
 *
 * Returns: %TRUE if @element is in @set
 */
gboolean
tp_intset_is_member (const TpIntSet *set, guint element)
{
  g_return_val_if_fail (set != NULL, FALSE);

  return _tp_intset_is_member (set, element);
}

/**
 * tp_intset_foreach:
 * @set: set
 * @func: @TpIntFunc to use to iterate the set
 * @userdata: user data to pass to each call of @func
 *
 * Call @func(element, @userdata) for each element of @set.
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
addint (guint i, gpointer data)
{
  GArray *array = (GArray *) data;
  g_array_append_val (array, i);
}

/**
 * tp_intset_to_array:
 * @set: set to convert
 *
 * <!--Returns: says it all-->
 *
 * Returns: a GArray of guint (which must be freed by the caller) containing
 * the same integers as @set.
 */
GArray *
tp_intset_to_array (TpIntSet *set)
{
  GArray *array;

  g_return_val_if_fail (set != NULL, NULL);

  array = g_array_new (FALSE, TRUE, sizeof (guint));

  tp_intset_foreach (set, addint, array);

  return array;
}


/**
 * tp_intset_from_array:
 * @array: An array of guint
 *
 * <!--Returns: says it all-->
 *
 * Returns: A set containing the same integers as @array.
 */

TpIntSet *
tp_intset_from_array (GArray *array)
{
  TpIntSet *set;
  guint max, i;

  g_return_val_if_fail (array != NULL, NULL);

  /* look at the 1st, last and middle values in the array to get an
   * approximation of the largest */
  max = 0;
  if (array->len > 0)
    max = g_array_index (array, guint, 0);
  if (array->len > 1)
    max = MAX (max, g_array_index (array, guint, array->len - 1));
  if (array->len > 2)
    max = MAX (max, g_array_index (array, guint, (array->len - 1) >> 1));
  set = _tp_intset_new_with_size (1 + (max >> 5));

  for (i = 0; i < array->len; i++)
    {
      tp_intset_add (set, g_array_index (array, guint, i));
    }

  return set;
}


/**
 * tp_intset_size:
 * @set: A set of integers
 *
 * <!--Returns: says it all-->
 *
 * Returns: The number of integers in @set
 */

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


/**
 * tp_intset_is_equal:
 * @left: A set of integers
 * @right: A set of integers
 *
 * <!--Returns: says it all-->
 *
 * Returns: %TRUE if @left and @right contain the same bits
 */

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


/**
 * tp_intset_copy:
 * @orig: A set of integers
 *
 * <!--Returns: says it all-->
 *
 * Returns: A set containing the same integers as @orig, to be freed with
 * tp_intset_destroy() by the caller
 */

TpIntSet *
tp_intset_copy (const TpIntSet *orig)
{
  TpIntSet *ret;

  g_return_val_if_fail (orig != NULL, NULL);

  ret = _tp_intset_new_with_size (orig->size);
  memcpy (ret->bits, orig->bits, (ret->size * sizeof (guint32)));

  return ret;
}


/**
 * tp_intset_intersection:
 * @left: The left operand
 * @right: The right operand
 *
 * <!--Returns: says it all-->
 *
 * Returns: The set of those integers which are in both @left and @right
 * (analogous to the bitwise operation left & right), to be freed with
 * tp_intset_destroy() by the caller
 */

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


/**
 * tp_intset_union:
 * @left: The left operand
 * @right: The right operand
 *
 * <!--Returns: says it all-->
 *
 * Returns: The set of those integers which are in either @left or @right
 * (analogous to the bitwise operation left | right), to be freed with
 * tp_intset_destroy() by the caller
 */

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


/**
 * tp_intset_difference:
 * @left: The left operand
 * @right: The right operand
 *
 * <!--Returns: says it all-->
 *
 * Returns: The set of those integers which are in @left and not in @right
 * (analogous to the bitwise operation left & (~right)), to be freed with
 * tp_intset_destroy() by the caller
 */

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


/**
 * tp_intset_symmetric_difference:
 * @left: The left operand
 * @right: The right operand
 *
 * <!--Returns: says it all-->
 *
 * Returns: The set of those integers which are in either @left or @right
 * but not both (analogous to the bitwise operation left ^ right), to be freed
 * with tp_intset_destroy() by the caller
 */

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
    g_string_append_printf (tmp, "%u", i);
  else
    g_string_append_printf (tmp, " %u", i);
}

/**
 * tp_intset_dump:
 * @set: An integer set
 *
 * <!--Returns: says it all-->
 *
 * Returns: a string which the caller must free with g_free, listing the
 * numbers in @set in a human-readable format
 */
gchar *
tp_intset_dump (const TpIntSet *set)
{
  GString *tmp = g_string_new ("");

  tp_intset_foreach (set, _dump_foreach, tmp);
  return g_string_free (tmp, FALSE);
}

/**
 * tp_intset_iter_next:
 * @iter: An iterator originally initialized with TP_INT_SET_INITIALIZER(set)
 *
 * If there are integers in (@iter->set) higher than (@iter->element), set
 * (iter->element) to the next one and return %TRUE. Otherwise return %FALSE.
 *
 * Usage:
 *
 * <informalexample><programlisting>
 * TpIntSetIter iter = TP_INT_SET_INIT (intset);
 * while (tp_intset_iter_next (&amp;iter;))
 * {
 *   printf ("%u is in the intset\n", iter.element);
 * }
 * </programlisting></informalexample>
 *
 * Returns: %TRUE if (@iter->element) has been advanced
 */
gboolean
tp_intset_iter_next (TpIntSetIter *iter)
{
  g_return_val_if_fail (iter != NULL, FALSE);
  g_return_val_if_fail (iter->set != NULL, FALSE);

  do
    {
      if (iter->element == (guint)(-1))
        {
          /* only just started */
          iter->element = 0;
        }
      else
        {
          ++iter->element;
        }

      if (_tp_intset_is_member (iter->set, iter->element))
        {
          return TRUE;
        }
    }
  while (iter->element < (iter->set->size << 5)
         && iter->element != (guint)(-1));
  return FALSE;
}
