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

#include "gheap.h"
#include "handles.h"
#include "telepathy-errors.h"

#include "config.h"

#ifdef ENABLE_HANDLE_LEAK_DEBUG
#include <stdlib.h>
#include <stdio.h>
#include <execinfo.h>

typedef struct _HandleLeakTrace HandleLeakTrace;

struct _HandleLeakTrace
{
  char **trace;
  int len;
};

static void
handle_leak_trace_free (HandleLeakTrace *hltrace)
{
  free (hltrace->trace);
  g_free (hltrace);
}

static void
handle_leak_trace_free_gfunc (gpointer data, gpointer user_data)
{
  return handle_leak_trace_free ((HandleLeakTrace *) data);
}

#endif /* ENABLE_HANDLE_LEAK_DEBUG */

typedef struct _GabbleHandlePriv GabbleHandlePriv;

struct _GabbleHandlePriv
{
  guint refcount;
  gchar *string;
#ifdef ENABLE_HANDLE_LEAK_DEBUG
  GSList *traces;
#endif /* ENABLE_HANDLE_LEAK_DEBUG */
  GData *datalist;
};

struct _GabbleHandleRepo
{
  GHashTable *contact_handles;
  GHashTable *room_handles;
  GData *list_handles;
  GHashTable *contact_strings;
  GHashTable *room_strings;
  GHeap *free_contact_handles;
  GHeap *free_room_handles;
  guint contact_serial;
  guint room_serial;
};

static const char *list_handle_strings[GABBLE_LIST_HANDLE_BLOCK] =
{
    "publish",      /* GABBLE_LIST_HANDLE_PUBLISH */
    "subscribe",    /* GABBLE_LIST_HANDLE_SUBSCRIBE */
    "known",        /* GABBLE_LIST_HANDLE_KNOWN */
    "block"         /* GABBLE_LIST_HANDLE_BLOCK */
};

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

  g_free(priv->string);
  g_datalist_clear (&(priv->datalist));
#ifdef ENABLE_HANDLE_LEAK_DEBUG
  g_slist_foreach (priv->traces, handle_leak_trace_free_gfunc, NULL);
  g_slist_free (priv->traces);
#endif /* ENABLE_HANDLE_LEAK_DEBUG */
  g_free (priv);
}

GabbleHandlePriv *
handle_priv_lookup (GabbleHandleRepo *repo,
                    TpHandleType type,
                    GabbleHandle handle)
{
  GabbleHandlePriv *priv = NULL;

  g_assert (repo != NULL);
  g_assert (gabble_handle_type_is_valid (type, NULL));
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
      g_assert_not_reached();
    }

  return priv;
}

static GabbleHandle
gabble_handle_alloc (GabbleHandleRepo *repo, TpHandleType type)
{
  GabbleHandle ret;

  g_assert (repo != NULL);
  g_assert (gabble_handle_type_is_valid (type, NULL));

  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      if (g_heap_size (repo->free_contact_handles))
        ret = GPOINTER_TO_UINT (g_heap_extract_first (repo->free_contact_handles));
      else
        ret = repo->contact_serial++;
      break;
    case TP_HANDLE_TYPE_ROOM:
      if (g_heap_size (repo->free_room_handles))
        ret = GPOINTER_TO_UINT (g_heap_extract_first (repo->free_room_handles));
      else
        ret = repo->room_serial++;
      break;
    default:
      g_assert_not_reached();
    }

  return ret;
}

static gint
handle_compare_func (gconstpointer a, gconstpointer b)
{
  GabbleHandle first = GPOINTER_TO_UINT (a);
  GabbleHandle second = GPOINTER_TO_UINT (b);

  return (first == second) ? 0 : ((first < second) ? -1 : 1);
}

void
handle_priv_remove (GabbleHandleRepo *repo,
                    TpHandleType type,
                    GabbleHandle handle)
{
  GabbleHandlePriv *priv;
  const gchar *string;

  g_assert (gabble_handle_type_is_valid (type, NULL));
  g_assert (handle != 0);
  g_assert (repo != NULL);

  priv = handle_priv_lookup (repo, type, handle);

  g_assert (priv != NULL);

  string = priv->string;

  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      g_hash_table_remove (repo->contact_strings, string);
      g_hash_table_remove (repo->contact_handles, GINT_TO_POINTER (handle));
      if (handle == repo->contact_serial-1)
        repo->contact_serial--;
      else
        g_heap_add (repo->free_contact_handles, GUINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_ROOM:
      g_hash_table_remove (repo->room_strings, string);
      g_hash_table_remove (repo->room_handles, GINT_TO_POINTER (handle));
      if (handle == repo->room_serial-1)
        repo->room_serial--;
      else
        g_heap_add (repo->free_room_handles, GUINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_LIST:
      g_dataset_id_remove_data (&repo->list_handles, handle);
      break;
    default:
      g_assert_not_reached ();
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
 * gabble_handle_decode_jid
 *
 * Parses a JID which may be one of the following forms:
 *  server
 *  server/resource
 *  username@server
 *  username@server/resource
 *  room@service/nick
 * And sets the caller's username, server and resource pointers. The
 * caller may set any of the pointers to NULL if they are not interested
 * in a certain component.
 *
 * The returned values may be NULL or zero-length if a component was either
 * not present or zero-length respectively in the given JID. The username and
 * server are lower-cased because the Jabber protocol treats these
 * case-insensitively.
 */
void
gabble_handle_decode_jid (const gchar *jid,
                          gchar **username,
                          gchar **server,
                          gchar **resource)
{
  char *tmp_jid, *tmp_username, *tmp_server, *tmp_resource;

  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  if (username != NULL)
    *username = NULL;

  if (server != NULL)
    *server = NULL;

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
  if (server != NULL)
    *server = g_utf8_strdown (tmp_server, -1);

  /* free our working copy */
  g_free (tmp_jid);
}

gboolean
gabble_handle_type_is_valid (TpHandleType type, GError **error)
{
  gboolean ret;

  if (type > TP_HANDLE_TYPE_NONE && type <= TP_HANDLE_TYPE_LIST)
    {
      ret = TRUE;
    }
  else
    {
      if (error != NULL)
        {
          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                "invalid handle type %u", type);
        }

      ret = FALSE;
    }

  return ret;
}

GabbleHandleRepo *
gabble_handle_repo_new ()
{
  GabbleHandleRepo *repo;
  GabbleHandle publish, subscribe, known, block;

  repo = g_new0 (GabbleHandleRepo, 1);

  repo->contact_handles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);

  repo->room_handles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);

  repo->contact_strings = g_hash_table_new (g_str_hash, g_str_equal);
  repo->room_strings = g_hash_table_new (g_str_hash, g_str_equal);

  repo->free_contact_handles = g_heap_new (handle_compare_func);
  repo->free_room_handles = g_heap_new (handle_compare_func);

  repo->contact_serial = 1;
  repo->room_serial = 1;

  g_datalist_init (&repo->list_handles);

  publish = GABBLE_LIST_HANDLE_PUBLISH;
  g_datalist_id_set_data_full (&repo->list_handles, (GQuark) publish,
      handle_priv_new(), (GDestroyNotify) handle_priv_free);

  subscribe = GABBLE_LIST_HANDLE_SUBSCRIBE;
  g_datalist_id_set_data_full (&repo->list_handles, (GQuark) subscribe,
      handle_priv_new(), (GDestroyNotify) handle_priv_free);

  known = GABBLE_LIST_HANDLE_KNOWN;
  g_datalist_id_set_data_full (&repo->list_handles, (GQuark) known,
      handle_priv_new(), (GDestroyNotify) handle_priv_free);

  block = GABBLE_LIST_HANDLE_BLOCK;
  g_datalist_id_set_data_full (&repo->list_handles, (GQuark) block,
      handle_priv_new(), (GDestroyNotify) handle_priv_free);

  return repo;
}

#ifdef ENABLE_HANDLE_LEAK_DEBUG

static void
handle_leak_debug_printbt_foreach (gpointer data, gpointer user_data)
{
  HandleLeakTrace *hltrace = (HandleLeakTrace *) data;
  int i;

  for (i = 1; i < hltrace->len; i++)
    {
      printf ("\t\t%s\n", hltrace->trace[i]);
    }

  printf ("\n");
}

static void
handle_leak_debug_printhandles_foreach (gpointer key, gpointer value, gpointer ignore)
{
  GabbleHandle handle = GPOINTER_TO_UINT (key);
  GabbleHandlePriv *priv = (GabbleHandlePriv *) value;

  printf ("\t%5u: %s (%u refs), traces:\n", handle, priv->string, priv->refcount);
  
  g_slist_foreach (priv->traces, handle_leak_debug_printbt_foreach, NULL);
}

static void
handle_leak_debug_print_report (GabbleHandleRepo *repo)
{
  g_assert (repo != NULL);

  printf ("The following contact handles were not freed:\n");
  g_hash_table_foreach (repo->contact_handles, handle_leak_debug_printhandles_foreach, NULL);
  printf ("The following room handles were not freed:\n");
  g_hash_table_foreach (repo->room_handles, handle_leak_debug_printhandles_foreach, NULL);
}

static HandleLeakTrace *
handle_leak_debug_bt ()
{
  void *bt_addresses[6];
  HandleLeakTrace *ret = g_new0 (HandleLeakTrace, 1);
  
  ret->len = backtrace (bt_addresses, 6);
  ret->trace = backtrace_symbols (bt_addresses, ret->len);

  return ret;
}

#define HANDLE_LEAK_DEBUG_DO(traces_slist) \
  { (traces_slist) =  g_slist_append ((traces_slist), handle_leak_debug_bt ()); }

#else /* !ENABLE_HANDLE_LEAK_DEBUG */

#define HANDLE_LEAK_DEBUG_DO(traces_slist) {}

#endif /* ENABLE_HANDLE_LEAK_DEBUG */

void
gabble_handle_repo_destroy (GabbleHandleRepo *repo)
{
  g_assert (repo != NULL);
  g_assert (repo->contact_handles);
  g_assert (repo->room_handles);
  g_assert (repo->contact_strings);
  g_assert (repo->room_strings);

#ifdef ENABLE_HANDLE_LEAK_DEBUG
  handle_leak_debug_print_report (repo);
#endif /* ENABLE_HANDLE_LEAK_DEBUG */

  g_hash_table_destroy (repo->contact_handles);
  g_hash_table_destroy (repo->room_handles);
  g_hash_table_destroy (repo->contact_strings);
  g_hash_table_destroy (repo->room_strings);
  g_heap_destroy (repo->free_contact_handles);
  g_heap_destroy (repo->free_room_handles);
  g_datalist_clear (&repo->list_handles);

  g_free (repo);
}

gboolean
gabble_handle_is_valid (GabbleHandleRepo *repo, TpHandleType type, GabbleHandle handle, GError **error)
{
  GArray *arr;
  gboolean ret;

  arr = g_array_new (FALSE, FALSE, sizeof (GabbleHandle));
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
  int i;

  g_return_val_if_fail (repo != NULL, FALSE);
  g_return_val_if_fail (array != NULL, FALSE);

  if (!gabble_handle_type_is_valid (type, error))
    return FALSE;

  for (i = 0; i < array->len; i++)
    {
      GabbleHandle handle = g_array_index (array, GabbleHandle, i);

      if ((handle == 0 && !allow_zero) ||
          (handle_priv_lookup (repo, type, handle) == NULL))
        {
          if (error != NULL)
            {
              *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                  "invalid handle %u", handle);
            }

          return FALSE;
        }
    }

  return TRUE;
}

gboolean
gabble_handle_ref (GabbleHandleRepo *repo,
                   TpHandleType type,
                   GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  if (type == TP_HANDLE_TYPE_LIST)
    {
      if (handle >= GABBLE_LIST_HANDLE_PUBLISH && handle <= GABBLE_LIST_HANDLE_BLOCK)
        return TRUE;
      else
        return FALSE;
    }

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return FALSE;

  priv->refcount++;

  HANDLE_LEAK_DEBUG_DO (priv->traces);

  return TRUE;
}

gboolean
gabble_handle_unref (GabbleHandleRepo *repo,
                     TpHandleType type,
                     GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  if (type == TP_HANDLE_TYPE_LIST)
    {
      if (handle >= GABBLE_LIST_HANDLE_PUBLISH && handle <= GABBLE_LIST_HANDLE_BLOCK)
        return TRUE;
      else
        return FALSE;
    }

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return FALSE;

  HANDLE_LEAK_DEBUG_DO (priv->traces);

  g_assert (priv->refcount > 0);

  priv->refcount--;

  if (priv->refcount == 0)
    handle_priv_remove (repo, type, handle);

  return TRUE;
}

const char *
gabble_handle_inspect (GabbleHandleRepo *repo,
                       TpHandleType type,
                       GabbleHandle handle)
{
  GabbleHandlePriv *priv;

  if (type == TP_HANDLE_TYPE_LIST)
    {
      g_assert (handle >= GABBLE_LIST_HANDLE_PUBLISH
                  && handle <= GABBLE_LIST_HANDLE_BLOCK);
      return list_handle_strings[handle-1];
    }

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return NULL;
  else
    return priv->string;
}

static GabbleHandle
_handle_lookup_by_jid (GabbleHandleRepo *repo,
                       const gchar *jid)
{
  GabbleHandle handle;

  handle = GPOINTER_TO_UINT (g_hash_table_lookup (repo->contact_strings, jid));

  if (0 == handle)
    return 0;

  return handle;
}

GabbleHandle
gabble_handle_for_contact (GabbleHandleRepo *repo,
                           const char *jid,
                           gboolean with_resource)
{
  char *username = NULL;
  char *server = NULL;
  char *resource = NULL;
  char *clean_jid = NULL;
  GabbleHandle handle = 0;
  GabbleHandlePriv *priv;

  g_assert (repo != NULL);
  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  gabble_handle_decode_jid (jid, &username, &server, &resource);

  if (NULL == username || '\0' == *username)
    goto OUT;

  if (NULL == resource && with_resource)
    goto OUT;

  if (NULL != resource)
    {
      clean_jid = g_strdup_printf ("%s@%s/%s", username, server, resource);
      handle = _handle_lookup_by_jid (repo, clean_jid);

      if (0 != handle)
        goto OUT;
    }

  if (!with_resource)
    {
      g_free (clean_jid);
      clean_jid = g_strdup_printf ("%s@%s", username, server);
      handle = _handle_lookup_by_jid (repo, clean_jid);

      if (0 != handle)
        goto OUT;
    }

  handle = gabble_handle_alloc (repo, TP_HANDLE_TYPE_CONTACT);
  priv = handle_priv_new ();
  priv->string = clean_jid;
  clean_jid = NULL;
  g_hash_table_insert (repo->contact_handles, GINT_TO_POINTER (handle), priv);
  g_hash_table_insert (repo->contact_strings, priv->string, GUINT_TO_POINTER (handle));

  HANDLE_LEAK_DEBUG_DO (priv->traces);

OUT:

  g_free (clean_jid);
  g_free (username);
  g_free (server);
  g_free (resource);
  return handle;
}

gboolean
gabble_handle_for_room_exists (GabbleHandleRepo *repo,
                               const gchar *jid,
                               gboolean ignore_nick)
{
  GabbleHandle handle;
  gchar *room, *service, *nick;
  gchar *clean_jid;

  gabble_handle_decode_jid (jid, &room, &service, &nick);

  if (!room || !service || room[0] == '\0')
    return FALSE;

  if (ignore_nick || !nick)
    clean_jid = g_strdup_printf ("%s@%s", room, service);
  else
    clean_jid = g_strdup_printf ("%s@%s/%s", room, service, nick);

  handle = GPOINTER_TO_UINT (g_hash_table_lookup (repo->room_strings,
                                                  clean_jid));
  
  g_free (clean_jid);
  g_free (room);
  g_free (service);
  g_free (nick);

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

      handle = GPOINTER_TO_UINT (g_hash_table_lookup (repo->room_strings, clean_jid));

      if (handle == 0)
        {
          GabbleHandlePriv *priv;
          handle = gabble_handle_alloc (repo, TP_HANDLE_TYPE_ROOM);
          priv = handle_priv_new ();
          priv->string = clean_jid;
          g_hash_table_insert (repo->room_handles, GUINT_TO_POINTER (handle), priv);
          g_hash_table_insert (repo->room_strings, clean_jid, GUINT_TO_POINTER (handle));
          HANDLE_LEAK_DEBUG_DO (priv->traces);
        }
      else
        {
          g_free (clean_jid);
        }
    }

  g_free (room);
  g_free (service);

  return handle;
}

GabbleHandle
gabble_handle_for_list (GabbleHandleRepo *repo,
                        const gchar *list)
{
  GabbleHandle handle = 0;
  int i;

  g_assert (repo != NULL);
  g_assert (list != NULL);

  for (i = 0; i < GABBLE_LIST_HANDLE_BLOCK; i++)
    {
      if (0 == strcmp (list_handle_strings[i], list))
        handle = (GabbleHandle) i + 1;
    }

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
