/*
 * tp-handle-repo-dynamic.h - mechanism to store and retrieve handles on
 * a connection - implementation for "normal" dynamically-created handles
 *
 * Copyright (C) 2005, 2007 Collabora Ltd.
 * Copyright (C) 2005, 2007 Nokia Corp.
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

#ifndef __TP_HANDLE_REPO_DYNAMIC_H__
#define __TP_HANDLE_REPO_DYNAMIC_H__

#include <telepathy-glib/handle-repo.h>

G_BEGIN_DECLS

typedef struct _TpDynamicHandleRepo TpDynamicHandleRepo;
typedef struct _TpDynamicHandleRepoClass TpDynamicHandleRepoClass;

/**
 * TpDynamicHandleRepoNormalizeFunc:
 * @repo: The repository on which tp_handle_lookup() or tp_handle_ensure()
 *        was called
 * @id: The name to be normalized
 * @context: Arbitrary context passed to tp_handle_lookup() or
 *           tp_handle_ensure()
 * @error: Used to raise the Telepathy error InvalidHandle with an appropriate
 *         message if NULL is returned
 *
 * Signature of the normalization function optionally used by
 * #TpDynamicHandleRepo instances.
 *
 * Returns: a normalized version of @id (to be freed with g_free by the
 *          caller), or NULL if @id is not valid for this repository
 */

typedef gchar *(*TpDynamicHandleRepoNormalizeFunc)(TpHandleRepoIface *repo,
    const gchar *id, gpointer context, GError **error);

GType tp_dynamic_handle_repo_get_type (void);

#define TP_TYPE_DYNAMIC_HANDLE_REPO \
  (tp_dynamic_handle_repo_get_type ())
#define TP_DYNAMIC_HANDLE_REPO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_DYNAMIC_HANDLE_REPO,\
  TpDynamicHandleRepo))
#define TP_DYNAMIC_HANDLE_REPO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_DYNAMIC_HANDLE_REPO,\
  TpDynamicHandleRepo))
#define TP_IS_DYNAMIC_HANDLE_REPO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_DYNAMIC_HANDLE_REPO))
#define TP_IS_DYNAMIC_HANDLE_REPO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_DYNAMIC_HANDLE_REPO))
#define TP_DYNAMIC_HANDLE_REPO_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_DYNAMIC_HANDLE_REPO,\
  TpDynamicHandleRepoClass))

TpHandle tp_dynamic_handle_repo_lookup_exact (TpHandleRepoIface *irepo,
    const char *id);

/**
 * tp_dynamic_handle_repo_new:
 * @handle_type: The handle type
 * @normalize_func: The function to be used to normalize and validate handles,
 *  or %NULL to accept all handles as-is
 * @default_normalize_context: The context pointer to be passed to the
 *  @normalize_func if a %NULL context is passed to tp_handle_lookup() and
 *  tp_handle_ensure(); this may itself be %NULL
 *
 * <!---->
 *
 * Returns: a new dynamic handle repository
 */

static inline
/* spacer so gtkdoc documents this function as though not static */
TpHandleRepoIface *tp_dynamic_handle_repo_new (TpHandleType handle_type,
    TpDynamicHandleRepoNormalizeFunc normalize_func,
    gpointer default_normalize_context);

static inline TpHandleRepoIface *
tp_dynamic_handle_repo_new (TpHandleType handle_type,
                            TpDynamicHandleRepoNormalizeFunc normalize_func,
                            gpointer default_normalize_context)
{
  return (TpHandleRepoIface *) g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
      "handle-type", (guint)handle_type,
      "normalize-function", (gpointer)normalize_func,
      "default-normalize-context", default_normalize_context,
      NULL);
}

G_END_DECLS

#endif

