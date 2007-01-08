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
#include <dbus/dbus-glib.h>
#include <string.h>

#include <telepathy-glib/heap.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/helpers.h>

#include "handles.h"
#include "util.h"

#include "config.h"

struct _GabbleHandleRepo
{
  TpHandleRepoIface *repos[LAST_TP_HANDLE_TYPE + 1];
};

static const char *list_handle_strings[] =
{
    "publish",      /* GABBLE_LIST_HANDLE_PUBLISH */
    "subscribe",    /* GABBLE_LIST_HANDLE_SUBSCRIBE */
    "known",        /* GABBLE_LIST_HANDLE_KNOWN */
    "deny",         /* GABBLE_LIST_HANDLE_DENY */
    NULL
};

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

          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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

GabbleHandleRepo *
gabble_handle_repo_new ()
{
  GabbleHandleRepo *repo;

  repo = g_new0 (GabbleHandleRepo, 1);

  repo->repos[TP_HANDLE_TYPE_CONTACT] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_CONTACT, NULL);
  repo->repos[TP_HANDLE_TYPE_ROOM] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_ROOM, NULL);
  repo->repos[TP_HANDLE_TYPE_GROUP] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_GROUP, NULL);
  repo->repos[TP_HANDLE_TYPE_LIST] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_STATIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_LIST,
          "handle-names", list_handle_strings, NULL);

  return repo;
}

TpHandleSet *
handle_set_new (GabbleHandleRepo *repo, TpHandleType type)
{
  return tp_handle_set_new (gabble_handle_repo_get_tp_repo (repo, type));
}

TpHandleRepoIface
*gabble_handle_repo_get_tp_repo (GabbleHandleRepo *repo,
                                 TpHandleType type)
{
  if (!tp_handle_type_is_valid (type, NULL))
    return NULL;

  if (!repo->repos[type])
    {
      return NULL;
    }

  return repo->repos[type];
}

void
gabble_handle_repo_destroy (GabbleHandleRepo *repo)
{
  TpHandleType i;

  g_assert (repo != NULL);

  for (i = 1; i <= LAST_TP_HANDLE_TYPE; i++)
    {
      if (repo->repos[i])
        g_object_unref ((GObject *)repo->repos[i]);
    }

  g_free (repo);
}

gboolean
gabble_handles_are_valid (GabbleHandleRepo *repo,
                          TpHandleType type,
                          const GArray *array,
                          gboolean allow_zero,
                          GError **error);

gboolean
gabble_handle_is_valid (GabbleHandleRepo *repo, TpHandleType type, TpHandle handle, GError **error)
{
  GArray *arr;
  gboolean ret;

  arr = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  g_array_insert_val (arr, 0, handle);

  ret = gabble_handles_are_valid (repo, type, arr, FALSE, error);

  g_array_free (arr, TRUE);

  return ret;
}

gboolean
gabble_handles_are_valid (GabbleHandleRepo *repo,
                          TpHandleType type,
                          const GArray *array,
                          gboolean allow_zero,
                          GError **error)
{
  if (!tp_handle_type_is_valid (type, error))
    return FALSE;

  if (!repo->repos[type])
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "unsupported handle type %u", type);
      return FALSE;
    }

  return tp_handles_are_valid (repo->repos[type], array, 
      allow_zero, error);
}

TpHandle
gabble_handle_for_contact (TpHandleRepoIface *repo,
                           const char *jid,
                           gboolean with_resource)
{
  char *username = NULL;
  char *server = NULL;
  char *resource = NULL;
  char *clean_jid = NULL;
  TpHandle handle = 0;

  g_assert (repo != NULL);
  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  gabble_decode_jid (jid, &username, &server, &resource);

  if (NULL == username || '\0' == *username)
    goto OUT;

  if (NULL == resource && with_resource)
    goto OUT;

  if (NULL != resource)
    {
      clean_jid = g_strdup_printf ("%s@%s/%s", username, server, resource);
      handle = tp_handle_request (repo, clean_jid, FALSE);

      if (0 != handle)
        goto OUT;
    }

  if (!with_resource)
    {
      g_free (clean_jid);
      clean_jid = g_strdup_printf ("%s@%s", username, server);
      handle = tp_handle_request (repo, clean_jid, FALSE);

      if (0 != handle)
        goto OUT;
    }

  handle = tp_handle_request (repo, clean_jid, TRUE);
OUT:

  g_free (clean_jid);
  g_free (username);
  g_free (server);
  g_free (resource);
  return handle;
}

gboolean
gabble_handle_for_room_exists (TpHandleRepoIface *repo,
                               const gchar *jid,
                               gboolean ignore_nick)
{
  TpHandle handle;
  gchar *room, *service, *nick;
  gchar *clean_jid;

  g_assert (repo != NULL);

  gabble_decode_jid (jid, &room, &service, &nick);

  if (!room || !service || room[0] == '\0')
    return FALSE;

  if (ignore_nick || !nick)
    clean_jid = g_strdup_printf ("%s@%s", room, service);
  else
    clean_jid = g_strdup_printf ("%s@%s/%s", room, service, nick);

  /* FIXME: how can the version *with* a nick possibly be added? */
  handle = tp_handle_request (repo, clean_jid, FALSE);
  
  g_free (clean_jid);
  g_free (room);
  g_free (service);
  g_free (nick);

  if (handle == 0)
    return FALSE;

  /* FIXME: how can this possibly fail if it's there? */
  return (tp_handle_is_valid (repo, handle, NULL));
}

TpHandle
gabble_handle_for_room (TpHandleRepoIface *repo,
                        const gchar *jid)
{
  TpHandle handle;
  gchar *room, *service, *clean_jid;

  g_assert (repo != NULL);
  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  handle = 0;

  room = service = NULL;
  gabble_decode_jid (jid, &room, &service, NULL);

  if (room && service && *room != '\0')
    {
      clean_jid = g_strdup_printf ("%s@%s", room, service);

      handle = tp_handle_request (repo, clean_jid, TRUE);
      g_free (clean_jid);
    }

  g_free (room);
  g_free (service);

  return handle;
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
                         TpHandleType type, TpHandle handle,
                         GQuark key_id, gpointer data, GDestroyNotify destroy)
{
  if (repo->repos[type] == NULL || !TP_IS_DYNAMIC_HANDLE_REPO (repo->repos[type]))
    return FALSE;

  return tp_dynamic_handle_repo_set_qdata (
      (TpDynamicHandleRepo *)repo->repos[type], handle,
      key_id, data, destroy);
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
                         TpHandleType type, TpHandle handle,
                         GQuark key_id)
{
  if (repo->repos[type] == NULL || !TP_IS_DYNAMIC_HANDLE_REPO (repo->repos[type]))
    return NULL;

  return tp_dynamic_handle_repo_get_qdata (
      (TpDynamicHandleRepo *)repo->repos[type], handle, key_id);
}

/**
 * gabble_handle_client_hold:
 * @repo: a #GabbleHandleRepo
 * @client_name: D-Bus bus name of client to hold the handle for
 * @handle: the handle to hold
 * @type: type of handle to hold
 * @error: used to return a pointer to a GError detailing any error that occurred
 *
 * Marks a handle as held by a given client.
 *
 * Returns: Whether the handle was succesfully marked as held or an error occurred.
 */
gboolean
gabble_handle_client_hold (GabbleHandleRepo *repo,
                           const gchar *client_name,
                           TpHandle handle,
                           TpHandleType type,
                           GError **error)
{
  if (!tp_handle_type_is_valid (type, error))
    return FALSE;

  if (!repo->repos[type])
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "unsupported handle type %u", type);
      return FALSE;
    }

  return tp_handle_client_hold (repo->repos[type],
      client_name, handle, error);
}

/**
 * gabble_handle_client_release:
 * @repo: a #GabbleHandleRepo
 * @client_name: D-Bus bus name of client to release the handle for
 * @handle: the handle to release
 * @type: type of handle to release
 * @error: used to return a pointer to a GError detailing any error that occurred
 *
 * Unmarks a handle as held by a given client.
 *
 * Returns: Whether the handle had been marked as held by the given client and now unmarked or not.
 */
gboolean
gabble_handle_client_release (GabbleHandleRepo *repo,
                           const gchar *client_name,
                           TpHandle handle,
                           TpHandleType type,
                           GError **error)
{
  if (!tp_handle_type_is_valid (type, error))
    return FALSE;

  if (!repo->repos[type])
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "unsupported handle type %u", type);
      return FALSE;
    }

  return tp_handle_client_release (repo->repos[type],
      client_name, handle, error);
}
