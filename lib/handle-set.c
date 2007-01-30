/*
 * handle-set.c - a set which refs a handle when inserted
 *
 * Copyright (C) 2005,2006,2007 Collabora Ltd.
 * Copyright (C) 2005,2006,2007 Nokia Corporation
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

/* there is no handle-set.h - handle set and handle repo have a circular
 * dependency, so they share a header */
#include <telepathy-glib/handle-repo.h>

#include <glib.h>

#include <telepathy-glib/intset.h>

struct _TpHandleSet
{
  TpHandleRepoIface *repo;
  TpIntSet *intset;
};

/**
 * tp_handle_set_new:
 * @repo: #TpHandleRepo that holds the handles to be reffed by this set
 *
 * Creates a new #TpHandleSet
 *
 * Returns: A new #TpHandleSet
 */
TpHandleSet *
tp_handle_set_new (TpHandleRepoIface *repo)
{
  TpHandleSet *set;
  g_assert (repo != NULL);

  set = g_new(TpHandleSet, 1);
  set->intset = tp_intset_new();
  set->repo = repo;

  return set;
}

static void
freer (TpHandleSet *set, TpHandle handle, gpointer userdata)
{
  tp_handle_set_remove (set, handle);
}

/**
 * tp_handle_set_destroy:
 * @set:#TpHandleSet to destroy
 *
 * Delete a #TpHandleSet and unreference any handles that it holds
 */
void
tp_handle_set_destroy (TpHandleSet *set)
{
  tp_handle_set_foreach (set, freer, NULL);
  tp_intset_destroy (set->intset);
  g_free (set);
}

/**
 * tp_handle_set_peek:
 * @set:#TpHandleSet to peek at
 *
 * Get the underlying TpIntSet used by this TpHandleSet
 */
TpIntSet *
tp_handle_set_peek (TpHandleSet *set)
{
  return set->intset;
}

/**
 * tp_handle_set_add:
 * @set: #TpHandleSet to add this handle to
 * @handle: handle to add
 *
 * Add a handle to a #TpHandleSet,and reference it in the attched
 * #TpHandleRepo
 *
 */
void
tp_handle_set_add (TpHandleSet *set, TpHandle handle)
{
  g_return_if_fail (set != NULL);
  g_return_if_fail (handle != 0);

  if (!tp_intset_is_member(set->intset, handle))
    {
      g_return_if_fail (tp_handle_ref (set->repo, handle));

      tp_intset_add (set->intset, handle);
    }
}

/**
 * tp_handle_set_remove:
 * @set: #TpHandleSet to remove this handle from
 * @handle: handle to remove
 *
 * Remove a handle to a #TpHandleSet,and unreference it in the attched
 * #TpHandleRepo
 *
 * Returns: FALSE if the handle was invalid, or was not in this set
 */

gboolean
tp_handle_set_remove (TpHandleSet *set, TpHandle handle)
{
  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (handle != 0, FALSE);

  if (tp_intset_is_member(set->intset, handle))
    {
      g_return_val_if_fail (tp_handle_unref (set->repo, handle), FALSE);

      tp_intset_remove (set->intset, handle);
      return TRUE;
    }

  return FALSE;
}

/**
 * tp_handle_set_is_member:
 * @set: A #TpHandleSet
 * @handle: handle to check
 *
 * Check if the handle is in this set
 *
 * Returns: TRUE if the handle is in this set
 *
 */
gboolean
tp_handle_set_is_member (TpHandleSet *set, TpHandle handle)
{
  return tp_intset_is_member(set->intset, handle);
}

typedef struct __foreach_data
{
  TpHandleSet *set;
  TpHandleSetMemberFunc func;
  gpointer userdata;
} _foreach_data;

static void
foreach_helper(guint i, gpointer userdata)
{
  _foreach_data *data = (_foreach_data*) userdata;

  data->func(data->set, i, data->userdata);
}

void
tp_handle_set_foreach (TpHandleSet *set, TpHandleSetMemberFunc func,
    gpointer userdata)
{
  _foreach_data data = {set, func, userdata};
  data.set = set;
  data.func = func;
  data.userdata = userdata;
  tp_intset_foreach (set->intset, foreach_helper, &data);
}

int
tp_handle_set_size (TpHandleSet *set)
{
  return tp_intset_size (set->intset);
}

GArray *
tp_handle_set_to_array (TpHandleSet *set)
{
  g_return_val_if_fail (set != NULL, NULL);

  return tp_intset_to_array (set->intset);
}

static void
ref_one (guint handle, gpointer data)
{
  TpHandleSet *set = (TpHandleSet *) data;
  tp_handle_ref (set->repo, handle);
}

/**
 * tp_handle_set_update:
 * @set: a #TpHandleSet to update
 * @add: a #TpIntSet of handles to add
 *
 * Add a set of handles to a handle set, referencing those which are not
 * already members. The TpIntSet returned must be freed with tp_intset_destroy.
 *
 * Returns: the handles which were added
 */
TpIntSet *
tp_handle_set_update (TpHandleSet *set, const TpIntSet *add)
{
  TpIntSet *ret, *tmp;

  g_return_val_if_fail (set != NULL, NULL);
  g_return_val_if_fail (add != NULL, NULL);

  /* reference each of ADD - CURRENT */
  ret = tp_intset_difference (add, set->intset);
  tp_intset_foreach (ret, ref_one, set);

  /* update CURRENT to be the union of CURRENT and ADD */
  tmp = tp_intset_union (add, set->intset);
  tp_intset_destroy (set->intset);
  set->intset = tmp;

  return ret;
}

static void
unref_one (guint handle, gpointer data)
{
  TpHandleSet *set = (TpHandleSet *) data;
  tp_handle_unref (set->repo, handle);
}

/**
 * tp_handle_set_difference_update:
 * @set: a #TpHandleSet to update
 * @remove: a #TpIntSet of handles to remove
 *
 * Remove a set of handles from a handle set, dereferencing those which are
 * members. The TpIntSet returned must be freed with tp_intset_destroy.
 *
 * Returns: the handles which were removed
 */
TpIntSet *
tp_handle_set_difference_update (TpHandleSet *set, const TpIntSet *remove)
{
  TpIntSet *ret, *tmp;

  g_return_val_if_fail (set != NULL, NULL);
  g_return_val_if_fail (remove != NULL, NULL);

  /* dereference each of REMOVE n CURRENT */
  ret = tp_intset_intersection (remove, set->intset);
  tp_intset_foreach (ret, unref_one, set);

  /* update CURRENT to be CURRENT - REMOVE */
  tmp = tp_intset_difference (set->intset, remove);
  tp_intset_destroy (set->intset);
  set->intset = tmp;

  return ret;
}
