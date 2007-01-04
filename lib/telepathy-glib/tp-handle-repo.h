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

#include <telepathy-glib/tp-intset.h>
#include <telepathy-glib/tp-handle.h>

G_BEGIN_DECLS

/* Forward declaration because it's in the HandleRepo API */
typedef struct _TpHandleSet TpHandleSet;

/* Handle repository abstract interface */

#define TP_TYPE_HANDLE_REPO_IFACE (tp_handle_repo_iface_get_type ())

#define TP_HANDLE_REPO_IFACE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    TP_TYPE_HANDLE_REPO_IFACE, TpHandleRepoIface))

#define TP_HANDLE_REPO_IFACE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), \
    TP_TYPE_HANDLE_REPO_IFACE, TpHandleRepoIfaceClass))

#define TP_IS_HANDLE_REPO_IFACE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    TP_TYPE_HANDLE_REPO_IFACE)

#define TP_IS_HANDLE_REPO_IFACE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
    TP_TYPE_HANDLE_REPO_IFACE)

#define TP_HANDLE_REPO_IFACE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
    TP_TYPE_HANDLE_REPO_IFACE, TpHandleRepoIfaceClass))

typedef struct _TpHandleRepoIface TpHandleRepoIface;    /* dummy typedef */
typedef struct _TpHandleRepoIfaceClass TpHandleRepoIfaceClass;

struct _TpHandleRepoIfaceClass {
    GTypeInterface parent_class;

    gboolean (*handle_is_valid) (TpHandleRepoIface *self, TpHandle handle,
        GError **error);
    gboolean (*handles_are_valid) (TpHandleRepoIface *self, const GArray *handles,
        gboolean allow_zero, GError **error);

    gboolean (*ref_handle) (TpHandleRepoIface *self, TpHandle handle);
    gboolean (*unref_handle) (TpHandleRepoIface *self, TpHandle handle);
    gboolean (*client_hold_handle) (TpHandleRepoIface *self,
        const gchar *client, TpHandle handle, GError **error);
    gboolean (*client_release_handle) (TpHandleRepoIface *self,
        const gchar *client, TpHandle handle, GError **error);

    const char *(*inspect_handle) (TpHandleRepoIface *self, TpHandle handle);
    TpHandle (*request_handle) (TpHandleRepoIface *self, const char *id,
        gboolean may_create);
};

GType tp_handle_repo_iface_get_type (void);

/* Public API for handle repositories */

gboolean tp_handle_is_valid (TpHandleRepoIface *self,
    TpHandle handle, GError **error);
gboolean tp_handles_are_valid (TpHandleRepoIface *self,
    const GArray *handles, gboolean allow_zero, GError **error);

gboolean tp_handle_ref (TpHandleRepoIface *self, TpHandle handle);
gboolean tp_handle_unref (TpHandleRepoIface *self, TpHandle handle);

gboolean tp_handle_client_hold (TpHandleRepoIface *self,
    const gchar *client, TpHandle handle, GError **error);
gboolean tp_handle_client_release (TpHandleRepoIface *self,
    const gchar *client, TpHandle handle, GError **error);

const char *tp_handle_inspect (TpHandleRepoIface *self,
    TpHandle handle);
TpHandle tp_handle_request (TpHandleRepoIface *self,
    const gchar *id, gboolean may_create);

/* Handle set helper class */

typedef void (*TpHandleSetMemberFunc)(TpHandleSet *set, TpHandle handle, gpointer userdata);

TpHandleSet * tp_handle_set_new (TpHandleRepoIface *);
void tp_handle_set_destroy (TpHandleSet *);

TpIntSet *tp_handle_set_peek (TpHandleSet *set);

void tp_handle_set_add (TpHandleSet *set, TpHandle handle);
gboolean tp_handle_set_remove (TpHandleSet *set, TpHandle handle);
gboolean tp_handle_set_is_member (TpHandleSet *set, TpHandle handle);

void tp_handle_set_foreach (TpHandleSet *set, TpHandleSetMemberFunc func, gpointer userdata);

int tp_handle_set_size (TpHandleSet *set);
GArray *tp_handle_set_to_array (TpHandleSet *set);

TpIntSet *tp_handle_set_update (TpHandleSet *set, const TpIntSet *add);
TpIntSet *tp_handle_set_difference_update (TpHandleSet *set, const TpIntSet *remove);

G_END_DECLS

#endif /*__HANDLE_SET_H__*/
