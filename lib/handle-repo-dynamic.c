/*
 * handle-repo-dynamic.c - mechanism to store and retrieve handles on a connection
 * (general implementation with dynamic handle allocation and recycling)
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

#include <telepathy-glib/handle-repo-dynamic.h>

#include <dbus/dbus-glib.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/heap.h>

/* Handle leak tracing */

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

/* Handle private data structure */

typedef struct _TpHandlePriv TpHandlePriv;

struct _TpHandlePriv
{
  /* Reference count */
  guint refcount;
  /* Unique ID */
  gchar *string;
#ifdef ENABLE_HANDLE_LEAK_DEBUG
  GSList *traces;
#endif /* ENABLE_HANDLE_LEAK_DEBUG */
  GData *datalist;
};

static TpHandlePriv *
handle_priv_new ()
{
  TpHandlePriv *priv;

  priv = g_new0 (TpHandlePriv, 1);

  g_datalist_init (&(priv->datalist));
  return priv;
}

/* Dynamic handle repo */

static void
handle_priv_free (TpHandlePriv *priv)
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

enum
{
  PROP_HANDLE_TYPE = 1,
};

struct _TpDynamicHandleRepoClass {
  GObjectClass parent_class;
};

struct _TpDynamicHandleRepo {
  GObject parent;

  TpHandleType handle_type;

  /* Map GUINT_TO_POINTER(handle) -> (TpHandlePriv *) */
  GHashTable *handle_to_priv;
  /* Map contact unique ID -> GUINT_TO_POINTER(handle) */
  GHashTable *string_to_handle;
  /* Heap-queue of GUINT_TO_POINTER(handle): previously used handles */
  TpHeap *free_handles;
  /* Smallest handle which has never been allocated */
  guint next_handle;
  /* Map (client name) -> (TpHandleSet *): handles being held by that client */
  GData *holder_to_handle_set;

  /* To listen for NameOwnerChanged. FIXME: centralize this? */
  DBusGProxy *bus_service_proxy;
};

static void dynamic_repo_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (TpDynamicHandleRepo, tp_dynamic_handle_repo,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (TP_TYPE_HANDLE_REPO_IFACE,
        dynamic_repo_iface_init));

static inline TpHandlePriv *
handle_priv_lookup (TpDynamicHandleRepo *repo,
                    TpHandle handle)
{
  return g_hash_table_lookup (repo->handle_to_priv, GINT_TO_POINTER (handle));
}

static TpHandle
handle_alloc (TpDynamicHandleRepo *repo)
{
  g_assert (repo != NULL);

  if (tp_heap_size (repo->free_handles))
    return GPOINTER_TO_UINT (tp_heap_extract_first (repo->free_handles));
  else
    return repo->next_handle++;
}

static gint
handle_compare_func (gconstpointer a, gconstpointer b)
{
  TpHandle first = GPOINTER_TO_UINT (a);
  TpHandle second = GPOINTER_TO_UINT (b);

  return (first == second) ? 0 : ((first < second) ? -1 : 1);
}

static void
handle_priv_remove (TpDynamicHandleRepo *repo,
    TpHandle handle)
{
  TpHandlePriv *priv;
  const gchar *string;

  g_assert (handle != 0);
  g_assert (repo != NULL);

  priv = handle_priv_lookup (repo, handle);

  g_assert (priv != NULL);

  string = priv->string;

  g_hash_table_remove (repo->string_to_handle, string);
  g_hash_table_remove (repo->handle_to_priv, GUINT_TO_POINTER (handle));
  if (handle == repo->next_handle - 1)
    repo->next_handle--;
  else
    tp_heap_add (repo->free_handles, GUINT_TO_POINTER (handle));
}

static void
handles_name_owner_changed_cb (DBusGProxy *proxy,
    const gchar *name,
    const gchar *old_owner,
    const gchar *new_owner,
    gpointer data)
{
  TpDynamicHandleRepo *repo = (TpDynamicHandleRepo *) data;

  if (old_owner && old_owner[0])
    {
      if (!new_owner || !new_owner[0])
        {
          g_datalist_remove_data (&repo->holder_to_handle_set, old_owner);
        }
    }
}

static void
tp_dynamic_handle_repo_init (TpDynamicHandleRepo *self)
{
  /* FIXME: some of this should be done in the c'tor probably */

  self->handle_to_priv = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);
  self->string_to_handle = g_hash_table_new (g_str_hash, g_str_equal);
  self->free_handles = tp_heap_new (handle_compare_func);
  self->next_handle = 1;
  g_datalist_init (&self->holder_to_handle_set);

  self->bus_service_proxy = dbus_g_proxy_new_for_name (tp_get_bus(),
      DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);

  dbus_g_proxy_add_signal (self->bus_service_proxy,
      "NameOwnerChanged", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_INVALID);
  /* FIXME: if dbus-glib gets arg matching, do this on a per-holder
   * basis so we don't wake up whenever any name owner changes */
  dbus_g_proxy_connect_signal (self->bus_service_proxy,
      "NameOwnerChanged", G_CALLBACK (handles_name_owner_changed_cb),
      self, NULL);

  return;
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
  TpHandle handle = GPOINTER_TO_UINT (key);
  TpHandlePriv *priv = (TpHandlePriv *) value;

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
  printf ("The following group handles were not freed:\n");
  g_hash_table_foreach (repo->group_handles, handle_leak_debug_printhandles_foreach, NULL);
}

static HandleLeakTrace *
handle_leak_debug_bt ()
{
  void *bt_addresses[16];
  HandleLeakTrace *ret = g_new0 (HandleLeakTrace, 1);
  
  ret->len = backtrace (bt_addresses, 16);
  ret->trace = backtrace_symbols (bt_addresses, ret->len);

  return ret;
}

#define HANDLE_LEAK_DEBUG_DO(traces_slist) \
  { (traces_slist) =  g_slist_append ((traces_slist), handle_leak_debug_bt ()); }

#else /* !ENABLE_HANDLE_LEAK_DEBUG */

#define HANDLE_LEAK_DEBUG_DO(traces_slist) {}

#endif /* ENABLE_HANDLE_LEAK_DEBUG */

static void
dynamic_finalize (GObject *obj)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (obj);

  GObjectClass *parent = G_OBJECT_CLASS (tp_dynamic_handle_repo_parent_class);

  g_assert (self->handle_to_priv);
  g_assert (self->string_to_handle);

  g_datalist_clear (&self->holder_to_handle_set);
  
#ifdef ENABLE_HANDLE_LEAK_DEBUG
  handle_leak_debug_print_report (self);
#endif

  g_hash_table_destroy (self->handle_to_priv);
  g_hash_table_destroy (self->string_to_handle);
  tp_heap_destroy (self->free_handles);

  dbus_g_proxy_disconnect_signal (self->bus_service_proxy,
      "NameOwnerChanged", G_CALLBACK (handles_name_owner_changed_cb),
      self);
  g_object_unref (G_OBJECT (self->bus_service_proxy));

  if (parent->finalize)
    parent->finalize (obj);
}

static void
dynamic_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (object);

  switch (property_id)
    {
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, self->handle_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
dynamic_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (object);

  switch (property_id)
    {
    case PROP_HANDLE_TYPE:
      self->handle_type = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
tp_dynamic_handle_repo_class_init (TpDynamicHandleRepoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = dynamic_finalize;

  object_class->get_property = dynamic_get_property;
  object_class->set_property = dynamic_set_property;

  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
}

static gboolean
dynamic_handle_is_valid (TpHandleRepoIface *irepo, TpHandle handle,
    GError **error)
{
  /* FIXME: cost per lookup */
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (irepo);

  if (handle_priv_lookup (self, handle) == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "handle %u is not currently valid for handle type %u '%s'",
          handle, self->handle_type, "TODO: label handle types");
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

static gboolean
dynamic_handles_are_valid (TpHandleRepoIface *irepo, const GArray *handles,
    gboolean allow_zero, GError **error)
{
  guint i;

  g_return_val_if_fail (handles != NULL, FALSE);

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);

      if (handle == 0 && allow_zero)
        continue;

      if (!dynamic_handle_is_valid (irepo, handle, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
dynamic_unref_handle (TpHandleRepoIface *repo, TpHandle handle)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (repo);
  TpHandlePriv *priv = handle_priv_lookup (self, handle);

  if (priv == NULL)
    return FALSE;

  HANDLE_LEAK_DEBUG_DO (priv->traces);

  g_assert (priv->refcount > 0);

  priv->refcount--;

  if (priv->refcount == 0)
    handle_priv_remove (self, handle);

  return TRUE;
}

static gboolean
dynamic_ref_handle (TpHandleRepoIface *repo, TpHandle handle)
{
  TpHandlePriv *priv = handle_priv_lookup (TP_DYNAMIC_HANDLE_REPO (repo),
      handle);

  if (priv == NULL)
    return FALSE;

  priv->refcount++;

  HANDLE_LEAK_DEBUG_DO (priv->traces);

  return TRUE;
}

static gboolean
dynamic_client_hold_handle (TpHandleRepoIface *repo,
                            const gchar *client_name,
                            TpHandle handle,
                            GError **error)
{
  TpDynamicHandleRepo *self;
  TpHandleSet *handle_set;

  g_assert (repo != NULL);
  
  self = TP_DYNAMIC_HANDLE_REPO (repo);

  if (!client_name || *client_name == '\0')
    {
      g_critical ("%s: called with invalid client name", G_STRFUNC);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid client name");
      return FALSE;
    }

  handle_set = (TpHandleSet *) g_datalist_get_data (
      &(self->holder_to_handle_set), client_name);

  if (!handle_set)
    {
      handle_set = tp_handle_set_new (repo);
      g_datalist_set_data_full (&(self->holder_to_handle_set),
                                client_name,
                                handle_set,
                                (GDestroyNotify) tp_handle_set_destroy);
    }

  tp_handle_set_add (handle_set, handle);

  return TRUE;
}

static gboolean
dynamic_client_release_handle (TpHandleRepoIface *repo,
                               const gchar *client_name,
                               TpHandle handle,
                               GError **error)
{
  TpDynamicHandleRepo *self;
  TpHandleSet *handle_set;

  g_assert (repo != NULL);

  self = TP_DYNAMIC_HANDLE_REPO (repo);

  if (!client_name || *client_name == '\0')
    {
      g_critical ("%s: called with invalid client name", G_STRFUNC);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid client name");
      return FALSE;
    }

  handle_set = (TpHandleSet *) g_datalist_get_data (
      &(self->holder_to_handle_set), client_name);

  if (!handle_set)
    {
      g_critical ("%s: no handle set found for the given client %s",
          G_STRFUNC, client_name);
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "the given client %s wasn't holding any handles", client_name);
      return FALSE;
    }

  if (!tp_handle_set_remove (handle_set, handle))
    {
      g_critical ("%s: the client %s wasn't holding the handle %u", G_STRFUNC,
          client_name, handle);
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "the given client %s wasn't holding the handle %u", client_name,
          handle);
      return FALSE;
    }

  return TRUE;
}

static const char *
dynamic_inspect_handle (TpHandleRepoIface *irepo, TpHandle handle)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (irepo);
  TpHandlePriv *priv = handle_priv_lookup (self, handle);
  if (priv == NULL)
    return NULL;
  else
    return priv->string;
}

static TpHandle
dynamic_request_handle (TpHandleRepoIface *irepo, const char *id,
    gboolean may_create)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (irepo);
  TpHandle handle;
  TpHandlePriv *priv;

  handle = GPOINTER_TO_UINT (g_hash_table_lookup (self->string_to_handle,
        id));
  if (handle)
    return handle;

  if (!may_create)
    return 0;

  handle = handle_alloc (self);
  priv = handle_priv_new ();
  priv->string = g_strdup (id);
  g_hash_table_insert (self->handle_to_priv, GUINT_TO_POINTER (handle), priv);
  g_hash_table_insert (self->string_to_handle, priv->string,
      GUINT_TO_POINTER (handle));
  HANDLE_LEAK_DEBUG_DO (priv->traces);
  return handle;
}

static gboolean
dynamic_set_qdata (TpHandleRepoIface *repo, TpHandle handle,
    GQuark key_id, gpointer data, GDestroyNotify destroy)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (repo);
  TpHandlePriv *priv = handle_priv_lookup (self, handle);
  if (priv == NULL)
    return FALSE;
  g_datalist_id_set_data_full (&priv->datalist, key_id, data, destroy);
  return TRUE;
}

static gpointer
dynamic_get_qdata (TpHandleRepoIface *repo, TpHandle handle,
    GQuark key_id)
{
  TpDynamicHandleRepo *self = TP_DYNAMIC_HANDLE_REPO (repo);
  TpHandlePriv *priv = handle_priv_lookup (self, handle);
  if (priv == NULL)
    return NULL;
  return g_datalist_id_get_data (&priv->datalist, key_id);
}

static void
dynamic_repo_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpHandleRepoIfaceClass *klass = (TpHandleRepoIfaceClass *) g_iface;

  klass->handle_is_valid = dynamic_handle_is_valid;
  klass->handles_are_valid = dynamic_handles_are_valid;
  klass->ref_handle = dynamic_ref_handle;
  klass->unref_handle = dynamic_unref_handle;
  klass->client_hold_handle = dynamic_client_hold_handle;
  klass->client_release_handle = dynamic_client_release_handle;
  klass->inspect_handle = dynamic_inspect_handle;
  klass->request_handle = dynamic_request_handle;
  klass->set_qdata = dynamic_set_qdata;
  klass->get_qdata = dynamic_get_qdata;
}

TpDynamicHandleRepo *
tp_dynamic_handle_repo_new (void)
{
  return TP_DYNAMIC_HANDLE_REPO (g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
        NULL));
}
