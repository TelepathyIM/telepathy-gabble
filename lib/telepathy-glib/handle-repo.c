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

#include <telepathy-glib/handle-repo.h>

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

gboolean
tp_handle_is_valid (TpHandleRepoIface *self,
    TpHandle handle,
    GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->handle_is_valid (self,
      handle, error);
}

gboolean
tp_handles_are_valid (TpHandleRepoIface *self,
    const GArray *handles,
    gboolean allow_zero,
    GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->handles_are_valid (self,
      handles, allow_zero, error);
}

gboolean
tp_handle_ref (TpHandleRepoIface *self,
    TpHandle handle)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->ref_handle (self,
      handle);
}

gboolean
tp_handle_unref (TpHandleRepoIface *self,
    TpHandle handle)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->unref_handle (self,
      handle);
}

gboolean
tp_handle_client_hold (TpHandleRepoIface *self,
    const gchar *client,
    TpHandle handle, GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_hold_handle (self,
      client, handle, error);
}

gboolean
tp_handle_client_release (TpHandleRepoIface *self,
    const gchar *client,
    TpHandle handle, GError **error)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->client_release_handle (self,
      client, handle, error);
}

const char *
tp_handle_inspect (TpHandleRepoIface *self,
    TpHandle handle)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->inspect_handle (self,
      handle);
}

TpHandle
tp_handle_request (TpHandleRepoIface *self,
    const gchar *id,
    gboolean may_create)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (self)->request_handle (self,
      id, may_create);
}

/**
 * tp_handle_set_qdata:
 * @repo: A #TpHandleRepo
 * @handle: A handle to set data on
 * @key_id: Key id to associate data with
 * @data: data to associate with handle
 * @destroy: A #GDestroyNotify to call to destroy the data,
 *           or NULL if not needed.
 *
 * Associates a blob of data with a given handle and a given key
 *
 * If @destroy is set, then the data is freed when the handle is freed.
 */

gboolean
tp_handle_set_qdata (TpHandleRepoIface *repo, TpHandle handle,
                     GQuark key_id, gpointer data, GDestroyNotify destroy)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (repo)->set_qdata (repo,
      handle, key_id, data, destroy);
}

/**
 * tp_handle_get_qdata:
 * @repo: A #TpHandleRepo
 * @type: The handle type
 * @handle: A handle to get data from
 * @key_id: Key id of data to fetch
 *
 * Gets the data associated with a given key on a given handle
 */
gpointer
tp_handle_get_qdata (TpHandleRepoIface *repo, TpHandle handle,
                     GQuark key_id)
{
  return TP_HANDLE_REPO_IFACE_GET_CLASS (repo)->get_qdata (repo,
      handle, key_id);
}
