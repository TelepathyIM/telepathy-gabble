/*
 * handle-repo.c - mechanism to store and retrieve handles on a connection
 * (abstract interface)
 *
 * Copyright (C) 2007 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007 Nokia Corporation
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

/**
 * SECTION:handle-repo
 * @title: TpHandleRepoIface
 * @short_description: abstract interface for handle allocation
 * @see_also: TpDynamicHandleRepo, TpStaticHandleRepo
 *
 * Abstract interface of a repository for handles, supporting operations
 * which include checking for validity, reference counting, lookup by
 * string value and lookup by numeric value. See #TpDynamicHandleRepo
 * and #TpStaticHandleRepo for concrete implementations.
 */

#include <telepathy-glib/handle-repo.h>

#include <telepathy-glib/internal-handle-repo.h>

static void
repo_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      initialized = TRUE;

      param_spec = g_param_spec_uint ("handle-type", "Handle type",
                                      "The TpHandleType held in this handle "
                                      "repository.",
                                      0, G_MAXUINT32, 0,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);
    }
}

GType
tp_handle_repo_iface_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo info = {
        sizeof (TpHandleRepoIfaceClass),
        repo_base_init,
        NULL,   /* base_finalize */
        NULL,   /* class_init */
        NULL,   /* class_finalize */
        NULL,   /* class_data */
        0,
        0,      /* n_preallocs */
        NULL    /* instance_init */
      };

      type = g_type_register_static (G_TYPE_INTERFACE, "TpHandleRepoIface",
          &info, 0);
    }

  return type;
}


/**
 * tp_handle_is_valid:
 * @self: A handle repository implementation
 * @handle: A handle of the type stored in the repository @self
 * @error: Set to InvalidHandle if %FALSE is returned
 *
 * <!--Returns: says it all-->
 *
 * Returns: %TRUE if the handle is nonzero and is present in the repository,
 * else %FALSE
 */

gboolean
tp_handle_is_valid (TpHandleRepoIface *self,
    TpHandle handle,
    GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->handle_is_valid (self,
      handle, error);
}


/**
 * tp_handles_are_valid:
 * @self: A handle repository implementation
 * @handles: Array of TpHandle representing handles of the type stored in
 *           the repository @self
 * @allow_zero: If %TRUE, zero is treated like a valid handle
 * @error: Set to InvalidHandle if %FALSE is returned
 *
 * <!--Returns: says it all-->
 *
 * Returns: %TRUE if the handle is present in the repository, else %FALSE
 */

gboolean
tp_handles_are_valid (TpHandleRepoIface *self,
    const GArray *handles,
    gboolean allow_zero,
    GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->handles_are_valid (self,
      handles, allow_zero, error);
}


/**
 * tp_handle_ref:
 * @self: A handle repository implementation
 * @handle: A handle of the type stored in the repository
 *
 * Increase the reference count of the given handle, which must be present
 * in the repository. For repository implementations which never free handles
 * (like #TpStaticHandleRepo) this has no effect.
 */

void
tp_handle_ref (TpHandleRepoIface *self,
               TpHandle handle)
{
  TP_HANDLE_REPO_IFACE_GET_CLASS (self)->ref_handle (self, handle);
}


/**
 * tp_handles_ref:
 * @self: A handle repository implementation
 * @handles: A GArray of TpHandle representing handles
 *
 * Increase the reference count of the given handles. If a handle appears
 * multiple times in @handles it will be referenced that many times. If
 * any zero entries appear in @handles they will be ignored without error;
 * it is an error for any other invalid handle to appear in @handles.
 */
void
tp_handles_ref (TpHandleRepoIface *self,
                const GArray *handles)
{
  guint i;
  TpHandle h;
  void (*ref) (TpHandleRepoIface *, TpHandle) =
    TP_HANDLE_REPO_IFACE_GET_CLASS (self)->ref_handle;

  for (i = 0; i < handles->len; i++)
    {
      h = g_array_index (handles, TpHandle, i);
      if (h != 0)
          ref (self, h);
    }
}


/**
 * tp_handle_unref:
 * @self: A handle repository implementation
 * @handle: A handle of the type stored in the repository
 *
 * Decrease the reference count of the given handle. If it reaches zero,
 * delete the handle. It is an error to attempt to unref a handle
 * which is not present in the repository.
 *
 * For repository implementations which never free handles (like
 * #TpStaticHandleRepo) this has no effect.
 */

void
tp_handle_unref (TpHandleRepoIface *self,
                 TpHandle handle)
{
  TP_HANDLE_REPO_IFACE_GET_CLASS (self)->unref_handle (self, handle);
}


/**
 * tp_handles_unref:
 * @self: A handle repository implementation
 * @handles: A GArray of TpHandle representing handles
 *
 * Decrease the reference count of the given handles. If a handle appears
 * multiple times in @handles it will be dereferenced that many times. If
 * any zero entries appear in @handles they will be ignored without error;
 * it is an error for any other invalid handle to appear in @handles.
 */
void
tp_handles_unref (TpHandleRepoIface *self,
                  const GArray *handles)
{
  guint i;
  TpHandle h;
  void (*unref) (TpHandleRepoIface *, TpHandle) =
    TP_HANDLE_REPO_IFACE_GET_CLASS (self)->unref_handle;

  for (i = 0; i < handles->len; i++)
    {
      h = g_array_index (handles, TpHandle, i);
      if (h != 0)
        unref (self, h);
    }
}


/**
 * tp_handle_client_hold:
 * @self: A handle repository implementation
 * @client: The unique bus name of a D-Bus peer
 * @handle: A handle of the type stored in the repository
 * @error: Set if %FALSE is returned
 *
 * Hold the given handle on behalf of the named client.
 * If the client leaves the bus, the reference is automatically discarded.
 *
 * Handles held multiple times are the same as handles held
 * once: the client either holds a handle or it doesn't. In particular,
 * if you call tp_handle_client_hold() multiple times, then call
 * tp_handle_client_release() just once, the client no longer holds the handle.
 *
 * It is an error for @handle not to be present in the repository.
 *
 * Returns: %TRUE if the client name is valid; else %FALSE with @error set.
 */

gboolean
tp_handle_client_hold (TpHandleRepoIface *self,
                       const gchar *client,
                       TpHandle handle,
                       GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_hold_handle (self,
      client, handle, error);
}


typedef gboolean (*HoldReleaseFunc) (TpHandleRepoIface *, const gchar *,
    TpHandle, GError **);

/**
 * tp_handles_client_hold:
 * @self: A handle repository implementation
 * @client: The D-Bus unique name of a client
 * @handles: A GArray of TpHandle representing handles
 * @error: Used to return an error if %FALSE is returned
 *
 * Hold the given handles on behalf of the named client.
 * If the client leaves the bus, the reference is automatically discarded.
 *
 * If any of the handles are zero they will be ignored without error.
 * It is an error for any other invalid handle to be in @handles:
 * the caller is expected to have validated them first, e.g. using
 * tp_handles_are_valid().
 *
 * Handles appearing multiple times are the same as handles appearing
 * once: the client either holds a handle or it doesn't.
 *
 * If %FALSE is returned, the reference counts of all handles are unaffected
 * (the function either fails completely or succeeds completely).
 *
 * Returns: %TRUE if the client name is valid; else %FALSE with @error set.
 */
gboolean
tp_handles_client_hold (TpHandleRepoIface *self,
                        const gchar *client,
                        const GArray *handles,
                        GError **error)
{
  guint i, j;
  TpHandle h;
  HoldReleaseFunc hold =
    TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_hold_handle;
  HoldReleaseFunc release =
    TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_release_handle;

  for (i = 0; i < handles->len; i++)
    {
      h = g_array_index (handles, TpHandle, i);
      if (h != 0)
        if (!hold (self, client, h, error))
          {
            /* undo what we already did */
            for (j = 0; j < i; j++)
              {
                h = g_array_index (handles, TpHandle, j);
                if (h != 0)
                  release (self, client, h, NULL);
              }
            return FALSE;
          }
    }
  /* success */
  return TRUE;
}


/**
 * tp_handle_client_release:
 * @self: A handle repository implementation
 * @client: The unique bus name of a D-Bus peer
 * @handle: A handle of the type stored in the repository
 * @error: Set if %FALSE is returned
 *
 * If the named client holds the given handle, release it.
 * If this causes the reference count to become zero, delete the handle.
 *
 * For repository implementations which never free handles (like
 * #TpStaticHandleRepo) this has no effect.
 *
 * Returns: %TRUE if the client name is valid and the client previously held
 * a reference to the handle, else %FALSE.
 */

gboolean
tp_handle_client_release (TpHandleRepoIface *self,
                          const gchar *client,
                          TpHandle handle,
                          GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_release_handle (self,
      client, handle, error);
}


/**
 * tp_handles_client_release:
 * @self: A handle repository implementation
 * @client: The D-Bus unique name of a client
 * @handles: A GArray of TpHandle representing handles
 * @error: Used to return an error if %FALSE is returned
 *
 * Releases a reference to the given handles on behalf of the named client.
 *
 * If any of the handles are zero they will be ignored without error.
 * It is an error for any other invalid handle to be in @handles:
 * the caller is expected to have validated them first, e.g. using
 * tp_handles_are_valid().
 *
 * If %FALSE is returned, the reference counts of all handles are unaffected
 * (the function either fails completely or succeeds completely).
 *
 * Returns: %TRUE if the client name is valid and the client previously held
 * a reference to all the handles, else %FALSE.
 */
gboolean
tp_handles_client_release (TpHandleRepoIface *self,
                           const gchar *client,
                           const GArray *handles,
                           GError **error)
{
  guint i, j;
  TpHandle h;
  gboolean ret = TRUE;
  HoldReleaseFunc hold =
    TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_hold_handle;
  HoldReleaseFunc release =
    TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_release_handle;

  /* We don't want to release the last reference to any handle, since that
   * would prevent us from undoing it on error. So, reference them all. */
  tp_handles_ref (self, handles);

  for (i = 0; i < handles->len; i++)
    {
      h = g_array_index (handles, TpHandle, i);
      if (h != 0)
        if (!release (self, client, h, error))
          {
            /* undo what we already did */
            for (j = 0; j < i; j++)
              {
                h = g_array_index (handles, TpHandle, j);
                if (h != 0)
                  hold (self, client, h, NULL);
              }
            ret = FALSE;
            goto out;
          }
    }

out:
  /* now we've either succeeded or undone a partial success, we don't need
   * to ref all the handles any more */
  tp_handles_unref (self, handles);
  return ret;
}


/**
 * tp_handle_inspect:
 * @self: A handle repository implementation
 * @handle: A handle of the type stored in the repository
 *
 * <!--Returns: says it all-->
 *
 * Returns: the string represented by the given handle, or NULL if the
 * handle is absent from the repository. The string is owned by the
 * handle repository and will remain valid as long as a reference to
 * the handle exists.
 */

const char *
tp_handle_inspect (TpHandleRepoIface *self,
    TpHandle handle)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->inspect_handle (self,
      handle);
}


/**
 * tp_handle_ensure:
 * @self: A handle repository implementation
 * @id: A string whose handle is required
 * @context: User data to be passed to the normalization callback
 * @error: Used to return an error if 0 is returned
 *
 * Return a new reference to the handle for the given string. The handle
 * is normalized, if possible. If no such handle exists it will be created.
 *
 * Returns: the handle corresponding to the given string, or 0 if it
 * is invalid.
 */

TpHandle
tp_handle_ensure (TpHandleRepoIface *self,
                  const gchar *id,
                  gpointer context,
                  GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->ensure_handle (self,
      id, context, error);
}


/**
 * tp_handle_lookup:
 * @self: A handle repository implementation
 * @id: A string whose handle is required
 * @context: User data to be passed to the normalization callback
 * @error: Used to raise an error if the handle does not exist or is
 *  invalid
 *
 * Return the handle for the given string, without incrementing its
 * reference count. The handle is normalized if possible.
 *
 * Returns: the handle corresponding to the given string, or 0 if it
 * does not exist or is invalid
 */

TpHandle
tp_handle_lookup (TpHandleRepoIface *self,
                  const gchar *id,
                  gpointer context,
                  GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->lookup_handle (self,
      id, context, error);
}


/**
 * tp_handle_set_qdata:
 * @repo: A handle repository implementation
 * @handle: A handle to set data on
 * @key_id: Key id to associate data with
 * @data: data to associate with handle
 * @destroy: A #GDestroyNotify to call to destroy the data,
 *           or NULL if not needed.
 *
 * Associates a blob of data with a given handle and a given key
 *
 * If @destroy is set, then the data is freed when the handle is freed.
 *
 * Inspecting the return value from this function is deprecated; it will
 * be declared void in a future release.
 */

void
tp_handle_set_qdata (TpHandleRepoIface *repo,
                     TpHandle handle,
                     GQuark key_id,
                     gpointer data,
                     GDestroyNotify destroy)
{
  TP_HANDLE_REPO_IFACE_GET_CLASS (repo)->set_qdata (repo,
      handle, key_id, data, destroy);
}

/**
 * tp_handle_get_qdata:
 * @repo: A handle repository implementation
 * @handle: A handle to get data from
 * @key_id: Key id of data to fetch
 *
 * <!--Returns: says it all-->
 *
 * Returns: the data associated with a given key on a given handle; %NULL
 * if there is no associated data.
 */
gpointer
tp_handle_get_qdata (TpHandleRepoIface *repo, TpHandle handle,
                     GQuark key_id)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (repo)->get_qdata (repo,
      handle, key_id);
}
