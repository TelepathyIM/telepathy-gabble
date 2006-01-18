/*
 * allocator.c - mechanism to manage allocations of a limited number of items
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

#include <glib.h>

#include "allocator.h"

/* private data types */

struct _GabbleAllocator
{
  gulong size;
  guint limit;
  guint count;
};

/* public functions */

GabbleAllocator *
gabble_allocator_new (gulong size, guint limit)
{
  GabbleAllocator *alloc;

  g_assert (size > 0);
  g_assert (limit > 0);

  alloc = g_new0 (GabbleAllocator, 1);

  alloc->size = size;
  alloc->limit = limit;

  return alloc;
}

void gabble_allocator_destroy (GabbleAllocator *alloc)
{
  g_free (alloc);
}

gpointer gabble_allocator_alloc (GabbleAllocator *alloc)
{
  gpointer ret;

  g_assert (alloc != NULL);
  g_assert (alloc->count <= alloc->limit);

  if (alloc->count == alloc->limit)
    {
      ret = NULL;
    }
  else
    {
      ret = g_malloc (alloc->size);
      alloc->count++;
    }

  return ret;
}

gpointer gabble_allocator_alloc0 (GabbleAllocator *alloc)
{
  gpointer ret;

  g_assert (alloc != NULL);
  g_assert (alloc->count <= alloc->limit);

  if (alloc->count == alloc->limit)
    {
      ret = NULL;
    }
  else
    {
      ret = g_malloc0 (alloc->size);
      alloc->count++;
    }

  return ret;
}

void gabble_allocator_free (GabbleAllocator *alloc, gpointer thing)
{
  g_assert (alloc != NULL);
  g_assert (thing != NULL);

  g_free (thing);
  alloc->count--;
}
