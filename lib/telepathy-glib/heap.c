/*
 * TpHeap - a heap queue
 *
 * Copyright (C) 2006 Nokia Corporation. All rights reserved.
 *
 * Contact: Olli Salli (Nokia-M/Helsinki) <olli.salli@nokia.com>
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

#include <telepathy-glib/heap.h>

#include <glib.h>

#define DEFAULT_SIZE 64

struct _TpHeap
{
  GPtrArray *data;
  GCompareFunc comparator;
};

TpHeap *
tp_heap_new (GCompareFunc comparator)
{
  TpHeap *ret = g_slice_new (TpHeap);
  g_assert (comparator != NULL);

  ret->data = g_ptr_array_sized_new (DEFAULT_SIZE);
  ret->comparator = comparator;

  return ret;
}

void
tp_heap_destroy (TpHeap * heap)
{
  g_return_if_fail (heap != NULL);

  g_ptr_array_free (heap->data, TRUE);
  g_slice_free (TpHeap, heap);
}

void
tp_heap_clear (TpHeap *heap)
{
  g_return_if_fail (heap != NULL);

  g_ptr_array_free (heap->data, TRUE);
  heap->data = g_ptr_array_sized_new (DEFAULT_SIZE);
}

#define HEAP_INDEX(heap, index) (g_ptr_array_index ((heap)->data, (index)-1))

void
tp_heap_add (TpHeap *heap, gpointer element)
{
  guint m;

  g_return_if_fail (heap != NULL);

  g_ptr_array_add (heap->data, element);
  m = heap->data->len;
  while (m != 1)
    {
      gpointer parent = HEAP_INDEX (heap, m / 2);

      if (heap->comparator (element, parent) == -1)
        {
          HEAP_INDEX (heap, m / 2) = element;
          HEAP_INDEX (heap, m) = parent;
          m /= 2;
        }
      else
        break;
    }
}

gpointer
tp_heap_peek_first (TpHeap *heap)
{
  g_return_val_if_fail (heap != NULL, NULL);

  if (heap->data->len > 0)
    return HEAP_INDEX (heap, 1);
  else
    return NULL;
}

static gpointer
extract_element (TpHeap * heap, int index)
{
  gpointer ret;

  g_return_val_if_fail (heap != NULL, NULL);

  if (heap->data->len > 0)
    {
      guint m = heap->data->len;
      guint i = 1, j;
      ret = HEAP_INDEX (heap, index);

      HEAP_INDEX (heap, index) = HEAP_INDEX (heap, m);

      while (i * 2 <= m)
        {
          /* select the child which is supposed to come FIRST */
          if ((i * 2 + 1 <= m)
              && (heap->
                  comparator (HEAP_INDEX (heap, i * 2),
                              HEAP_INDEX (heap, i * 2 + 1)) == 1))
            j = i * 2 + 1;
          else
            j = i * 2;

          if (heap->comparator (HEAP_INDEX (heap, i), HEAP_INDEX (heap, j)) ==
              1)
            {
              gpointer tmp = HEAP_INDEX (heap, i);
              HEAP_INDEX (heap, i) = HEAP_INDEX (heap, j);
              HEAP_INDEX (heap, j) = tmp;
              i = j;
            }
          else
            break;
        }

      g_ptr_array_remove_index (heap->data, m - 1);
    }
  else
    ret = NULL;

  return ret;
}

void
tp_heap_remove (TpHeap *heap, gpointer element)
{
    guint i;

    g_return_if_fail (heap != NULL);

    for (i = 1; i <= heap->data->len; i++)
      {
          if (element == HEAP_INDEX (heap, i))
            {
              extract_element (heap, i);
              break;
            }
      }
}

gpointer
tp_heap_extract_first (TpHeap * heap)
{
  g_return_val_if_fail (heap != NULL, NULL);

  if (heap->data->len == 0)
      return NULL;

  return extract_element (heap, 1);
}

guint
tp_heap_size (TpHeap *heap)
{
  g_return_val_if_fail (heap != NULL, 0);

  return heap->data->len;
}
