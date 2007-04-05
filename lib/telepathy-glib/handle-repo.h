/*
 * tp-handle-repo.h - handle reference-counting for connection managers
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

#ifndef __TP_HANDLE_REPO_H__
#define __TP_HANDLE_REPO_H__

#include <glib-object.h>

#include <telepathy-glib/intset.h>
#include <telepathy-glib/handle.h>

G_BEGIN_DECLS

/* Forward declaration because it's in the HandleRepo API */

/**
 * TpHandleSet:
 *
 * A set of handles. This is similar to a #TpIntSet (and implemented using
 * one), but adding a handle to the set also references it.
 */
typedef struct _TpHandleSet TpHandleSet;

/* Handle repository abstract interface */

#define TP_TYPE_HANDLE_REPO_IFACE (tp_handle_repo_iface_get_type ())

#define TP_HANDLE_REPO_IFACE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    TP_TYPE_HANDLE_REPO_IFACE, TpHandleRepoIface))

#define TP_IS_HANDLE_REPO_IFACE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    TP_TYPE_HANDLE_REPO_IFACE)

#define TP_HANDLE_REPO_IFACE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
    TP_TYPE_HANDLE_REPO_IFACE, TpHandleRepoIfaceClass))

/**
 * TpHandleRepoIface:
 *
 * Dummy typedef representing any implementation of this interface.
 */
typedef struct _TpHandleRepoIface TpHandleRepoIface;

/**
 * TpHandleRepoIfaceClass:
 *
 * The class of a handle repository interface. The structure layout is
 * only available within telepathy-glib, for the handle repository
 * implementations' benefit.
 */
typedef struct _TpHandleRepoIfaceClass TpHandleRepoIfaceClass;

GType tp_handle_repo_iface_get_type (void);

/* Public API for handle repositories */

gboolean tp_handle_is_valid (TpHandleRepoIface *self,
    TpHandle handle, GError **error);
gboolean tp_handles_are_valid (TpHandleRepoIface *self,
    const GArray *handles, gboolean allow_zero, GError **error);

void tp_handle_ref (TpHandleRepoIface *self, TpHandle handle);
void tp_handles_ref (TpHandleRepoIface *self, const GArray *handles);
void tp_handle_unref (TpHandleRepoIface *self, TpHandle handle);
void tp_handles_unref (TpHandleRepoIface *self, const GArray *handles);

gboolean tp_handle_client_hold (TpHandleRepoIface *self,
    const gchar *client, TpHandle handle, GError **error);
gboolean tp_handles_client_hold (TpHandleRepoIface *self,
    const gchar *client, const GArray *handles, GError **error);
gboolean tp_handle_client_release (TpHandleRepoIface *self,
    const gchar *client, TpHandle handle, GError **error);
gboolean tp_handles_client_release (TpHandleRepoIface *self,
    const gchar *client, const GArray *handles, GError **error);

const char *tp_handle_inspect (TpHandleRepoIface *self,
    TpHandle handle);
TpHandle tp_handle_lookup (TpHandleRepoIface *self,
    const gchar *id, gpointer context, GError **error);
TpHandle tp_handle_ensure (TpHandleRepoIface *self,
    const gchar *id, gpointer context, GError **error);

void tp_handle_set_qdata (TpHandleRepoIface *repo, TpHandle handle,
    GQuark key_id, gpointer data, GDestroyNotify destroy);
gpointer tp_handle_get_qdata (TpHandleRepoIface *repo, TpHandle handle,
    GQuark key_id);

/* Handle set helper class */

/**
 * TpHandleSetMemberFunc:
 * @set: The set of handles on which tp_handle_set_foreach() was called
 * @handle: A handle in the set
 * @userdata: Arbitrary user data as supplied to tp_handle_set_foreach()
 *
 * Signature of the callback used to iterate over the handle set in
 * tp_handle_set_foreach().
 */
typedef void (*TpHandleSetMemberFunc)(TpHandleSet *set, TpHandle handle,
    gpointer userdata);

TpHandleSet * tp_handle_set_new (TpHandleRepoIface *repo);
void tp_handle_set_destroy (TpHandleSet *set);

TpIntSet *tp_handle_set_peek (TpHandleSet *set);

void tp_handle_set_add (TpHandleSet *set, TpHandle handle);
gboolean tp_handle_set_remove (TpHandleSet *set, TpHandle handle);
gboolean tp_handle_set_is_member (TpHandleSet *set, TpHandle handle);

void tp_handle_set_foreach (TpHandleSet *set, TpHandleSetMemberFunc func,
    gpointer userdata);

int tp_handle_set_size (TpHandleSet *set);
GArray *tp_handle_set_to_array (TpHandleSet *set);

TpIntSet *tp_handle_set_update (TpHandleSet *set, const TpIntSet *add);
TpIntSet *tp_handle_set_difference_update (TpHandleSet *set,
    const TpIntSet *remove);

/* static inline because it relies on NUM_TP_HANDLE_TYPES */
/**
 * tp_handles_supported_and_valid:
 * @repos: An array of possibly null pointers to handle repositories, indexed
 *         by handle type, where a null pointer means an unsupported handle
 *         type
 * @handle_type: The handle type
 * @handles: A GArray of guint representing handles of the given type
 * @allow_zero: If %TRUE, zero is treated like a valid handle
 * @error: Used to return an error if %FALSE is returned
 *
 * Return %TRUE if the given handle type is supported (i.e. repos[handle_type]
 * is not %NULL) and the given handles are all valid in that repository.
 * If not, set @error and return %FALSE.
 *
 * Returns: %TRUE if the handle type is supported and the handles are all
 * valid.
 */

static inline
/* spacer so gtkdoc documents this function as though not static */
gboolean tp_handles_supported_and_valid (
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES],
    TpHandleType handle_type, const GArray *handles, gboolean allow_zero,
    GError **error);

static inline gboolean
tp_handles_supported_and_valid (TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES],
                                TpHandleType handle_type,
                                const GArray *handles,
                                gboolean allow_zero,
                                GError **error)
{
  if (!tp_handle_type_is_valid (handle_type, error))
    return FALSE;

  if (!repos[handle_type])
    {
      tp_g_set_error_unsupported_handle_type (handle_type, error);
      return FALSE;
    }
  return tp_handles_are_valid (repos[handle_type], handles, allow_zero, error);
}

G_END_DECLS

#endif /*__HANDLE_SET_H__*/
