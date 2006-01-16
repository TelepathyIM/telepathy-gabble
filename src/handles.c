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

/* private data types */

struct _GabbleHandleRepo
{
  GHashTable *handles;
  GabbleHandle list_publish;
  GabbleHandle list_subscribe;
};

typedef struct _GabbleHandlePriv GabbleHandlePriv;

struct _GabbleHandlePriv
{
  guint refcount;
  TpHandleType type;
};

/* private functions */

static GabbleHandlePriv *
handle_priv_new (TpHandleType type)
{
  GabbleHandlePriv *priv;

  g_assert (type > TP_HANDLE_TYPE_NONE);
  g_assert (type <= TP_HANDLE_TYPE_LIST);

  priv = g_new0 (GabbleHandlePriv, 1);
  priv->type = type;

  return priv;
}

static void
handle_priv_free (GabbleHandlePriv *priv)
{
  g_assert (priv != NULL);

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

  priv = g_hash_table_lookup (repo->handles, GINT_TO_POINTER (handle));

  if (priv == NULL)
    return NULL;

  if (priv->type != type)
    return NULL;

  return priv;
}

/* public API */

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
gabble_handle_type_is_valid (GabbleHandleType type)
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

  repo = g_new0 (GabbleHandleRepo, 1);

  repo->handles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);

  repo->list_publish = g_quark_from_static_string ("publish");
  repo->list_subscribe = g_quark_from_static_string ("subscribe");

  return repo;
}

void
gabble_handle_repo_destroy (GabbleHandleRepo *repo)
{
  g_assert (repo != NULL);
  g_assert (repo->handles != NULL);

  g_hash_table_destroy (repo->handles);

  g_free (repo);
}

gboolean
gabble_handle_ref (GabbleHandleRepo *repo,
                   TpHandleType type,
                   GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  priv = handle_priv_lookup (repo, type, handle);

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

  g_assert (priv->refcount > 0);

  priv->refcount--;

  if (priv->refcount == 0)
    {
      g_hash_table_remove (repo->handles, priv);

      handle_priv_free (priv);
    }

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
                           char *jid,
                           gboolean with_resource)
{
  char *username, *server, *resource, *clean_jid;
  GabbleHandle handle;

  g_assert (repo != NULL);
  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  gabble_handle_decode_jid (jid, &username, &server, &resource);

  g_assert (username != NULL);
  g_assert (*username != '\0');

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
      GabbleHandlePriv *priv;

      /* pretend this string is static and just don't free it instead */
      handle = g_quark_from_static_string (clean_jid);

      priv = handle_priv_new (TP_HANDLE_TYPE_CONTACT);
      g_hash_table_insert (repo->handles, GINT_TO_POINTER (handle), priv);
    }
  else
    {
      g_free (clean_jid);
    }

  return handle;
}

GabbleHandle gabble_handle_for_list_publish(GabbleHandleRepo *repo);
GabbleHandle gabble_handle_for_list_subscribe(GabbleHandleRepo *repo);
