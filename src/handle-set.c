/*
 * handle-set.c - a set which refs a handle when inserted
 *
 * Copyright (C) 2005,2006 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
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
#include <glib.h>
#include "gintset.h"
#include "handles.h"
#include "handles-private.h"

#include "handle-set.h"

struct _GabbleHandleSet
{
  GabbleHandleRepo *repo;
  GIntSet *intset;
  TpHandleType type;
};

/**
 * handle_set_new:
 * @repo: #GabbleHandleRepo that holds the handles to be reffed by this set
 *
 * Creates a new #GabbleHandleSet
 *
 * Returns: A new #GabbleHandleSet
 */
GabbleHandleSet *
handle_set_new (GabbleHandleRepo *repo, TpHandleType type)
{
  GabbleHandleSet *set = g_new(GabbleHandleSet, 1);
  set->intset = g_intset_new();
  set->repo = repo;
  set->type = type;

  return set;
}

static void
freer (GabbleHandleSet *set, GabbleHandle handle, gpointer userdata)
{
  handle_set_remove (set, handle);
}

/**
 * handle_set_destroy:
 * @set:#GabbleHandleSetto destroy
 *
 * Delete a #GabbleHandleSet and unreference any handles that it holds
 */
void
handle_set_destroy (GabbleHandleSet *set)
{
  handle_set_foreach (set, freer, NULL);
  g_intset_destroy (set->intset);
  g_free(set);
}

/**
 * handle_set_add:
 * @set: #GabbleHandleSet to add this handle to
 * @handle: handle to add
 *
 * Add a handle to a #GabbleHandleSet,and reference it in the attched
 * #GabbleHandleRepo
 *
 */
void
handle_set_add (GabbleHandleSet *set, GabbleHandle handle)
{
  if (!g_intset_is_member(set->intset, handle))
    {
      gabble_handle_ref (set->repo, set->type, handle);
      g_intset_add (set->intset, handle);
    }
}

/**
 * handle_set_remove:
 * @set: #GabbleHandleSet to remove this handle from
 * @handle: handle to remove
 * @type: type of handle
 *
 * Remove a handle to a #GabbleHandleSet,and unreference it in the attched
 * #GabbleHandleRepo
 *
 * Returns: FALSE if the (handle,type) pair was invalid, or was not in this set
 */

gboolean
handle_set_remove (GabbleHandleSet *set, GabbleHandle handle)
{
  if (g_intset_is_member(set->intset, handle))
    {
      gabble_handle_unref (set->repo, set->type, handle);
      g_intset_remove (set->intset, handle);
      return TRUE;
    }
  return FALSE;
}

/**
 * handle_set_is_member:
 * @set: A #GabbleHandleSet
 * @handle: handle to check 
 * @type: type of handle
 *
 * Check if the (handle,type) pair is in this set
 *
 * Returns: TRUE if the (handle,type) pair is in this repo
 *
 */
gboolean
handle_set_is_member (GabbleHandleSet *set, GabbleHandle handle)
{
  return g_intset_is_member(set->intset, handle);
}

typedef struct __foreach_data
{
  GabbleHandleSet *set;
  GabbleHandleFunc func;
  gpointer userdata;
} _foreach_data;

static void
foreach_helper(guint i, gpointer userdata)
{
  _foreach_data *data = (_foreach_data*) userdata;

  data->func(data->set, i, data->userdata);
}

void
handle_set_foreach (GabbleHandleSet *set, GabbleHandleFunc func, gpointer userdata)
{
  _foreach_data data = {set, func, userdata};
  data.set = set;
  data.func = func;
  data.userdata = userdata;
  g_intset_foreach (set->intset, foreach_helper, &data);
}

int
handle_set_size (GabbleHandleSet *set)
{
  return g_intset_size (set->intset);
}

GArray *handle_set_to_array (GabbleHandleSet *set)
{
  g_return_val_if_fail (set == NULL, NULL);

  return g_intset_to_array (set->intset);
}
