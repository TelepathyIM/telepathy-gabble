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

void
gabble_handle_repos_init (TpHandleRepoIface *repos[LAST_TP_HANDLE_TYPE+1])
{
  guint i;

  for (i = 0; i <= LAST_TP_HANDLE_TYPE; i++)
    {
      repos[i] = NULL;
    }

  repos[TP_HANDLE_TYPE_CONTACT] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_CONTACT, NULL);
  repos[TP_HANDLE_TYPE_ROOM] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_ROOM, NULL);
  repos[TP_HANDLE_TYPE_GROUP] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_GROUP, NULL);
  repos[TP_HANDLE_TYPE_LIST] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_STATIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_LIST,
          "handle-names", list_handle_strings, NULL);
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
