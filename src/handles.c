/*
 * handles.c - mechanism to store and retrieve handles on a connection
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
#include <string.h>

#include "handles.h"
#include "handles-private.h"
#include "telepathy-errors.h"

/* private functions */

static GabbleHandlePriv *
handle_priv_new ()
{
  GabbleHandlePriv *priv;

  priv = g_new0 (GabbleHandlePriv, 1);

  g_datalist_init (&(priv->datalist));
  return priv;
}

static void
handle_priv_free (GabbleHandlePriv *priv)
{
  g_assert (priv != NULL);

  g_datalist_clear (&(priv->datalist));
  g_free (priv);
}

GabbleHandlePriv *
handle_priv_lookup (GabbleHandleRepo *repo,
                    TpHandleType type,
                    GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  g_assert (repo != NULL);
  g_assert (handle != 0);

  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      priv = g_hash_table_lookup (repo->contact_handles, GINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_ROOM:
      priv = g_hash_table_lookup (repo->room_handles, GINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_LIST:
      priv = g_datalist_id_get_data (&repo->list_handles, handle);
      break;
    default:
      g_critical ("Invalid handle type requested in handle_priv_lookup!");
      return NULL;
    }

  return priv;
}

void
handle_priv_remove (GabbleHandleRepo *repo,
                    TpHandleType type,
                    GabbleHandlePriv *priv)
{
  g_assert (repo != NULL);

  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      g_hash_table_remove (repo->contact_handles, priv);
      break;
    case TP_HANDLE_TYPE_ROOM:
      g_hash_table_remove (repo->room_handles, priv);
      break;
    default:
      g_critical ("Invalid handle type requested in handle_priv_remove!");
      return;
    }
}


/* public API */

/**
 * gabble_handle_jid_is_valid
 *
 * Validates a jid for given handle type and returns TRUE/FALSE
 * on success/failure. In the latter case further information is
 * provided through error if set.
 */
gboolean
gabble_handle_jid_is_valid (TpHandleType type, const gchar *jid, GError **error)
{
  if (type == TP_HANDLE_TYPE_CONTACT || type == TP_HANDLE_TYPE_ROOM)
    {
      if (!strchr (jid, '@'))
        {
          g_debug ("%s: jid %s has no @", G_STRFUNC, jid);

          if (error)
            *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                  "jid %s has no @", jid);

          return FALSE;
        }

      /* FIXME: do more extensive checking */
    }
  else
    {
      g_assert_not_reached ();
      /* FIXME: add checking for other types here */
    }

  return TRUE;
}

/**
 * gabble_handle_jid_get_base
 *
 * Utility function to return the base portion of a jid.
 */
gchar *
gabble_handle_jid_get_base (const gchar *jid)
{
  gchar *base_jid;
  const gchar *p;

  p = strchr (jid, '/');
  if (p == NULL)
    {
      base_jid = g_strdup (jid);
    }
  else
    {
      gint len = p - jid;

      base_jid = g_new (gchar, len + 1);
      memcpy (base_jid, jid, len);
      base_jid[len] = '\0';
    }

  return base_jid;
}

/**
 * gabble_handle_decode_jid
 *
 * Parses a JID which may be one of the following forms:
 *  server
 *  server/resource
 *  username@server
 *  username@server/resource
 * And sets the caller's username, server and resource pointers. The
 * caller must provide a server pointer, and may set username and resource
 * to NULL if they are not interested. The returned values may be NULL
 * or zero-length if a component was either not present or zero-length
 * respectively in the given JID. The username and server are lower-cased
 * because the Jabber protocol treats these case-insensitively.
 */
void
gabble_handle_decode_jid (const char *jid,
                          char **username,
                          char **server,
                          char **resource)
{
  char *tmp_jid, *tmp_username, *tmp_server, *tmp_resource;

  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  g_assert (server != NULL);

  if (username != NULL)
    *username = NULL;

  if (resource != NULL)
    *resource = NULL;

  /* take a local copy so we don't modify the caller's string */
  tmp_jid = g_strdup (jid);

  /* find an @ in username, truncate username to that length, and point
   * 'server' to the byte afterwards */
  tmp_server = strchr (tmp_jid, '@');
  if (tmp_server)
    {
      tmp_username = tmp_jid;

      *tmp_server = '\0';
      tmp_server++;

      /* store the username if the user provided a pointer */
      if (username != NULL)
        *username = g_utf8_strdown (tmp_username, -1);
    }
  else
    {
      tmp_username = NULL;
      tmp_server = tmp_jid;
    }

  /* if we have a server, find a / in it, truncate it to that length, and point
   * 'resource' to the byte afterwards. otherwise, do the same to username to
   * find any resource there. */
  tmp_resource = strchr (tmp_server, '/');
  if (tmp_resource)
    {
      *tmp_resource = '\0';
      tmp_resource++;

      /* store the resource if the user provided a pointer */
      if (resource != NULL)
        *resource = g_strdup (tmp_resource);
    }

  /* the server must be stored after the resource, in case we truncated a
   * resource from it */
  *server = g_utf8_strdown (tmp_server, -1);

  /* free our working copy */
  g_free (tmp_jid);
}

gboolean
gabble_handle_type_is_valid (TpHandleType type)
{
  if (type > TP_HANDLE_TYPE_NONE && type <= TP_HANDLE_TYPE_LIST)
    return TRUE;
  else
    return FALSE;
}

GabbleHandleRepo *
gabble_handle_repo_new ()
{
  GabbleHandleRepo *repo;
  GabbleHandle publish, subscribe;

  repo = g_new0 (GabbleHandleRepo, 1);

  repo->contact_handles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);

  repo->room_handles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);

  g_datalist_init (&repo->list_handles);

  publish = gabble_handle_for_list_publish (repo);
  g_datalist_id_set_data_full (&repo->list_handles, publish,
      handle_priv_new(), (GDestroyNotify) handle_priv_free);

  subscribe = gabble_handle_for_list_subscribe (repo);
  g_datalist_id_set_data_full (&repo->list_handles, subscribe,
      handle_priv_new(), (GDestroyNotify) handle_priv_free);

  return repo;
}

void
gabble_handle_repo_destroy (GabbleHandleRepo *repo)
{
  g_assert (repo != NULL);
  g_assert (repo->contact_handles);
  g_assert (repo->room_handles);

  g_hash_table_destroy (repo->contact_handles);
  g_hash_table_destroy (repo->room_handles);
  g_datalist_clear (&repo->list_handles);

  g_free (repo);
}

gboolean
gabble_handle_is_valid (GabbleHandleRepo *repo, TpHandleType type, GabbleHandle handle)
{
  return (handle_priv_lookup (repo, type, handle) != NULL);
}

gboolean
gabble_handle_ref (GabbleHandleRepo *repo,
                   TpHandleType type,
                   GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return FALSE;

  priv->refcount++;

  return TRUE;
}

gboolean
gabble_handle_unref (GabbleHandleRepo *repo,
                     TpHandleType type,
                     GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return FALSE;

  g_assert (priv->refcount > 0);

  priv->refcount--;

  if (priv->refcount == 0)
    handle_priv_remove (repo, type, priv);
  return TRUE;
}

const char *
gabble_handle_inspect (GabbleHandleRepo *repo,
                       TpHandleType type,
                       GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return NULL;
  else
    return g_quark_to_string (handle);
}

GabbleHandle
gabble_handle_for_contact (GabbleHandleRepo *repo,
                           const char *jid,
                           gboolean with_resource)
{
  char *username, *server, *resource, *clean_jid;
  GabbleHandle handle;

  g_assert (repo != NULL);
  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  username = server = resource = NULL;
  gabble_handle_decode_jid (jid, &username, &server,
                            with_resource ? &resource
                                          : NULL);

  if (username == NULL || *username == '\0')
    {
      return 0;
    }

  if (with_resource && resource != NULL)
    {
      clean_jid = g_strdup_printf ("%s@%s/%s", username, server, resource);
    }
  else
    {
      clean_jid = g_strdup_printf ("%s@%s", username, server);
    }

  g_free (username);
  g_free (server);

  if (resource)
    g_free (resource);

  handle = g_quark_try_string (clean_jid);

  if (handle == 0)
    {
      /* pretend this string is static and just don't free it instead */
      handle = g_quark_from_static_string (clean_jid);
    }
  else
    {
      g_free (clean_jid);
    }

  /* existence of the quark cannot be presumed to mean the handle exists
   * in this repository, because of multiple connections */
  if (!handle_priv_lookup (repo, TP_HANDLE_TYPE_CONTACT, handle))
    {
      GabbleHandlePriv *priv;
      priv = handle_priv_new ();
      g_hash_table_insert (repo->contact_handles, GINT_TO_POINTER (handle), priv);
    }

  return handle;
}

gboolean
gabble_handle_for_room_exists (GabbleHandleRepo *repo,
                               const gchar *jid)
{
  GabbleHandle handle;

  handle = g_quark_try_string (jid);
  if (handle == 0)
    return FALSE;

  return (handle_priv_lookup (repo, TP_HANDLE_TYPE_ROOM, handle) != NULL);
}

GabbleHandle
gabble_handle_for_room (GabbleHandleRepo *repo,
                        const gchar *jid)
{
  GabbleHandle handle;
  gchar *room, *service, *clean_jid;

  g_assert (repo != NULL);
  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  handle = 0;

  room = service = NULL;
  gabble_handle_decode_jid (jid, &room, &service, NULL);

  if (room && service && *room != '\0')
    {
      clean_jid = g_strdup_printf ("%s@%s", room, service);

      handle = g_quark_try_string (clean_jid);

      if (handle == 0)
        {
          /* pretend this string is static and just don't free it instead */
          handle = g_quark_from_static_string (clean_jid);
        }
      else
        {
          g_free (clean_jid);
        }

      /* existence of the quark cannot be presumed to mean the handle exists
       * in this repository, because of multiple connections */
      if (!handle_priv_lookup (repo, TP_HANDLE_TYPE_ROOM, handle))
        {
          GabbleHandlePriv *priv;
          priv = handle_priv_new ();
          g_hash_table_insert (repo->room_handles, GINT_TO_POINTER (handle), priv);
        }
    }

  if (room)
    g_free (room);
  if (service)
    g_free (service);

  return handle;
}

GabbleHandle
gabble_handle_for_list_publish (GabbleHandleRepo *repo)
{
  static GabbleHandle publish = 0;

  g_return_val_if_fail (repo != NULL, 0);

  if (publish == 0)
    {
      publish = g_quark_from_static_string ("publish");
    }

  return publish;
}

GabbleHandle
gabble_handle_for_list_subscribe (GabbleHandleRepo *repo)
{
  static GabbleHandle subscribe = 0;

  g_return_val_if_fail (repo != NULL, 0);

  if (subscribe == 0)
    {
      subscribe = g_quark_from_static_string ("subscribe");
    }

  return subscribe;
}

/**
 * gabble_handle_set_qdata:
 * @repo: A #GabbleHandleRepo
 * @type: The handle type
 * @handle: A handle to set data on
 * @key_id: Key id to associate data with
 * @data: data to associate with handle
 * @destroy: A #GDestroyNotify to call to detroy the data,
 *           or NULL if not needed.
 *
 * Associates a blob of data with a given handle and a given key
 *
 * If @destroy is set, then the data is freed when the handle is freed.
 */

gboolean
gabble_handle_set_qdata (GabbleHandleRepo *repo,
                         TpHandleType type, GabbleHandle handle,
                         GQuark key_id, gpointer data, GDestroyNotify destroy)
{
  GabbleHandlePriv *priv;
  priv = handle_priv_lookup (repo, type, handle);

  if (!priv)
    return FALSE;

  g_datalist_id_set_data_full (&priv->datalist, key_id, data, destroy);
  return TRUE;
}

/**
 * gabble_handle_get_qdata:
 * @repo: A #GabbleHandleRepo
 * @type: The handle type
 * @handle: A handle to get data from
 * @key_id: Key id of data to fetch
 *
 * Gets the data associated with a given key on a given handle
 */
gpointer
gabble_handle_get_qdata (GabbleHandleRepo *repo,
                         TpHandleType type, GabbleHandle handle,
                         GQuark key_id)
{
  GabbleHandlePriv *priv;
  priv = handle_priv_lookup (repo, type, handle);

  if (!priv)
    return NULL;

  return g_datalist_id_get_data(&priv->datalist, key_id);
}
