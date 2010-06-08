/*
 * conn-sidecars.h - Gabble connection implementation of sidecars
 * Copyright © 2009 Collabora Ltd.
 * Copyright © 2009 Nokia Corporation
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

#include "conn-sidecars.h"

#include <telepathy-glib/dbus.h>

#include "extensions/extensions.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION
#include "debug.h"
#include "plugin-loader.h"
#include "gabble/sidecar.h"

static void
sidecars_conn_status_changed_cb (
    GabbleConnection *conn,
    guint status,
    guint reason,
    gpointer unused);

void
conn_sidecars_init (GabbleConnection *conn)
{
  conn->sidecars = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
  conn->pending_sidecars = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_list_free);

  g_signal_connect (conn, "status-changed",
      (GCallback) sidecars_conn_status_changed_cb, NULL);
}

void
conn_sidecars_dispose (GabbleConnection *conn)
{
  g_warn_if_fail (g_hash_table_size (conn->sidecars) == 0);
  g_hash_table_unref (conn->sidecars);
  conn->sidecars = NULL;

  g_warn_if_fail (g_hash_table_size (conn->pending_sidecars) == 0);
  g_hash_table_unref (conn->pending_sidecars);
  conn->pending_sidecars = NULL;
}

static gchar *
make_sidecar_path (
    GabbleConnection *conn,
    const gchar *sidecar_iface)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);

  return g_strdelimit (
      g_strdup_printf ("%s/Sidecar/%s", base_conn->object_path, sidecar_iface),
      ".", '/');
}

/**
 * connection_install_sidecar:
 *
 * Registers @sidecar on the bus, and returns its object path.
 */
static gchar *
connection_install_sidecar (
    GabbleConnection *conn,
    GabbleSidecar *sidecar,
    const gchar *sidecar_iface)
{
  gchar *path = make_sidecar_path (conn, sidecar_iface);

  dbus_g_connection_register_g_object (tp_get_bus (), path, G_OBJECT (sidecar));
  g_hash_table_insert (conn->sidecars, g_strdup (sidecar_iface),
      g_object_ref (sidecar));

  return path;
}

typedef struct {
    GabbleConnection *conn;
    gchar *sidecar_iface;
} Grr;

static Grr *
grr_new (
    GabbleConnection *conn,
    const gchar *sidecar_iface)
{
  Grr *grr = g_slice_new (Grr);

  grr->conn = g_object_ref (conn);
  grr->sidecar_iface = g_strdup (sidecar_iface);

  return grr;
}

static void
grr_free (Grr *grr)
{
  g_object_unref (grr->conn);
  g_free (grr->sidecar_iface);

  g_slice_free (Grr, grr);
}

static void
create_sidecar_cb (
    GObject *loader_obj,
    GAsyncResult *result,
    gpointer user_data)
{
  GabblePluginLoader *loader = GABBLE_PLUGIN_LOADER (loader_obj);
  Grr *ctx = user_data;
  GabbleConnection *conn = ctx->conn;
  const gchar *sidecar_iface = ctx->sidecar_iface;
  GabbleSidecar *sidecar;
  GList *contexts;
  GError *error = NULL;

  sidecar = gabble_plugin_loader_create_sidecar_finish (loader, result, &error);
  contexts = g_hash_table_lookup (conn->pending_sidecars, sidecar_iface);

  if (contexts == NULL)
    {
      /* We never use the empty list as a value in pending_sidecars, so this
       * must mean we've disconnected and already returned. Jettison the
       * sidecar!
       */
      DEBUG ("creating sidecar %s %s after connection closed; jettisoning!",
          sidecar_iface, (sidecar != NULL ? "succeeded" : "failed"));
      goto out;
    }

  if (sidecar != NULL)
    {
      const gchar *actual_iface = gabble_sidecar_get_interface (sidecar);

      if (tp_strdiff (ctx->sidecar_iface, actual_iface))
        {
          /* TODO: maybe this lives in the loader? It knows what the plugin is
           * called. */
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "A buggy plugin created a %s sidecar when asked to create %s",
              actual_iface, ctx->sidecar_iface);
        }
    }
  else /* sidecar == NULL */
    {
      /* If creating the sidecar failed, 'error' should have been set */
      g_return_if_fail (error != NULL);
    }

  if (error == NULL)
    {
      gchar *path = connection_install_sidecar (ctx->conn, sidecar,
          ctx->sidecar_iface);
      GHashTable *props = gabble_sidecar_get_immutable_properties (sidecar);
      GList *l;

      for (l = contexts; l != NULL; l = l->next)
        gabble_svc_connection_future_return_from_ensure_sidecar (l->data,
            path, props);

      g_hash_table_unref (props);
      g_free (path);
    }
  else
    {
      g_list_foreach (contexts, (GFunc) dbus_g_method_return_error, error);
    }

  g_hash_table_remove (ctx->conn->pending_sidecars, ctx->sidecar_iface);

out:
  if (sidecar != NULL)
    g_object_unref (sidecar);

  if (error != NULL)
    g_clear_error (&error);

  grr_free (ctx);
}

static void
gabble_connection_ensure_sidecar (
    GabbleSvcConnectionFUTURE *iface,
    const gchar *sidecar_iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  GabbleSidecar *sidecar;
  gpointer key, value;
  GError *error = NULL;

  if (base_conn->status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      GError e = { TP_ERRORS, TP_ERROR_DISCONNECTED,
          "This connection has already disconnected" };

      DEBUG ("already disconnected, declining request for %s", sidecar_iface);
      dbus_g_method_return_error (context, &e);
      return;
    }

  if (!tp_dbus_check_valid_interface_name (sidecar_iface, &error))
    {
      error->domain = TP_ERRORS;
      error->code = TP_ERROR_INVALID_ARGUMENT;
      DEBUG ("%s is malformed: %s", sidecar_iface, error->message);
      dbus_g_method_return_error (context, error);
      g_clear_error (&error);
      return;
    }

  sidecar = g_hash_table_lookup (conn->sidecars, sidecar_iface);

  if (sidecar != NULL)
    {
      gchar *path = make_sidecar_path (conn, sidecar_iface);
      GHashTable *props = gabble_sidecar_get_immutable_properties (sidecar);

      DEBUG ("sidecar %s already exists at %s", sidecar_iface, path);
      gabble_svc_connection_future_return_from_ensure_sidecar (context, path,
          props);

      g_free (path);
      g_hash_table_unref (props);
      return;
    }

  if (g_hash_table_lookup_extended (conn->pending_sidecars, sidecar_iface,
          &key, &value))
    {
      GList *contexts = value;

      DEBUG ("already awaiting %s, joining a queue of %u", sidecar_iface,
          g_list_length (contexts));

      contexts = g_list_prepend (contexts, context);
      g_hash_table_steal (conn->pending_sidecars, key);
      g_hash_table_insert (conn->pending_sidecars, key, contexts);
      return;
    }

  DEBUG ("enqueuing first request for %s", sidecar_iface);
  g_hash_table_insert (conn->pending_sidecars, g_strdup (sidecar_iface),
      g_list_prepend (NULL, context));

  if (base_conn->status == TP_CONNECTION_STATUS_CONNECTED)
    {
      GabblePluginLoader *loader = gabble_plugin_loader_dup ();

      DEBUG ("requesting %s from the plugin loader", sidecar_iface);
      gabble_plugin_loader_create_sidecar (loader, sidecar_iface, conn,
          conn->session, create_sidecar_cb, grr_new (conn, sidecar_iface));
      g_object_unref (loader);
    }
  else
    {
      DEBUG ("not yet connected; waiting.");
    }
}

static void
sidecars_conn_status_changed_cb (
    GabbleConnection *conn,
    guint status,
    guint reason,
    gpointer unused)
{
  DBusGConnection *bus = tp_get_bus ();
  GHashTableIter iter;
  gpointer key, value;

  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      g_hash_table_iter_init (&iter, conn->sidecars);

      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          DEBUG ("removing %s from the bus", gabble_sidecar_get_interface (value));
          dbus_g_connection_unregister_g_object (bus, G_OBJECT (value));
        }

      g_hash_table_iter_init (&iter, conn->pending_sidecars);

      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          const gchar *sidecar_iface = key;
          GList *contexts = value;
          GError *error = g_error_new (TP_ERRORS, TP_ERROR_CANCELLED,
              "Disconnected before %s could be created", sidecar_iface);

          DEBUG ("failing all %u requests for %s", g_list_length (contexts),
              sidecar_iface);
          g_list_foreach (contexts, (GFunc) dbus_g_method_return_error, error);
          g_error_free (error);
        }

      g_hash_table_remove_all (conn->sidecars);
      g_hash_table_remove_all (conn->pending_sidecars);
    }
  else if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
      GabblePluginLoader *loader = gabble_plugin_loader_dup ();

      DEBUG ("connected; requesting sidecars from plugins");
      g_hash_table_iter_init (&iter, conn->pending_sidecars);

      while (g_hash_table_iter_next (&iter, &key, NULL))
        {
          const gchar *sidecar_iface = key;

          DEBUG ("requesting %s from the plugin loader", sidecar_iface);
          gabble_plugin_loader_create_sidecar (loader, sidecar_iface, conn,
              conn->session, create_sidecar_cb, grr_new (conn, sidecar_iface));
        }

      g_object_unref (loader);
    }
}

void
conn_future_iface_init (
    gpointer g_iface,
    gpointer iface_data)
{
  GabbleSvcConnectionFUTUREClass *klass = g_iface;

#define IMPLEMENT(x) \
    gabble_svc_connection_future_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT (ensure_sidecar);
#undef IMPLEMENT
}
