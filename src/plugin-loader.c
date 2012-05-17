/*
 * plugin-loader.c — plugin support for telepathy-gabble
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

#include "config.h"

#include "plugin-loader.h"

#include <glib.h>

#ifdef ENABLE_PLUGINS
# include <gmodule.h>
#endif

#include <telepathy-glib/errors.h>
#include <telepathy-glib/presence-mixin.h>

#define DEBUG_FLAG GABBLE_DEBUG_PLUGINS
#include "debug.h"
#include "gabble/plugin.h"

G_DEFINE_TYPE(GabblePluginLoader,
    gabble_plugin_loader,
    G_TYPE_OBJECT)

struct _GabblePluginLoaderPrivate {
    GPtrArray *plugins;
};

#ifdef ENABLE_PLUGINS
static void
plugin_loader_try_to_load (
    GabblePluginLoader *self,
    const gchar *path)
{
  GModule *m = g_module_open (path, G_MODULE_BIND_LOCAL);
  gpointer func;
  GabblePluginCreateImpl create;
  GabblePlugin *plugin;

  if (m == NULL)
    {
      const gchar *e = g_module_error ();

      /* the errors often seem to be prefixed by the filename */
      if (g_str_has_prefix (e, path))
        DEBUG ("%s", e);
      else
        DEBUG ("%s: %s", path, e);

      return;
    }

  if (!g_module_symbol (m, "gabble_plugin_create", &func))
    {
      DEBUG ("%s", g_module_error ());
      g_module_close (m);
      return;
    }

  /* We're about to try to instantiate an object. This installs the
   * class with the type system, so we should ensure that this
   * plug-in is never accidentally unloaded.
   */
  g_module_make_resident (m);

  /* Here goes nothing... */
  create = func;
  plugin = create ();

  if (plugin == NULL)
    {
      g_warning ("gabble_plugin_create () failed for %s", path);
    }
  else
    {
      gchar *sidecars = g_strjoinv (", ",
          (gchar **) gabble_plugin_get_sidecar_interfaces (plugin));
      const gchar *version = gabble_plugin_get_version (plugin);

      if (version == NULL)
        version = "(unspecified)";

      DEBUG ("loaded '%s' version %s (%s), implementing these sidecars: %s",
          gabble_plugin_get_name (plugin), version, path, sidecars);

      g_free (sidecars);

      g_ptr_array_add (self->priv->plugins, plugin);
    }
}

static void
gabble_plugin_loader_probe (GabblePluginLoader *self)
{
  GError *error = NULL;
  const gchar *directory_names = g_getenv ("GABBLE_PLUGIN_DIR");
  gchar **dir_array;
  gchar **ptr;
  GDir *d;
  const gchar *file;

  if (!g_module_supported ())
    {
      DEBUG ("modules aren't supported on this platform.");
      return;
    }

  if (directory_names == NULL)
    directory_names = PLUGIN_DIR;

#ifdef G_OS_WIN32
  dir_array = g_strsplit (directory_names, ";", 0);
#else
  dir_array = g_strsplit (directory_names, ":", 0);
#endif

  for (ptr = dir_array ; *ptr != NULL ; ptr++)
    {
      DEBUG ("probing %s", *ptr);
      d = g_dir_open (*ptr, 0, &error);

      if (d == NULL)
        {
          DEBUG ("%s", error->message);
          g_error_free (error);
          continue;
        }

      while ((file = g_dir_read_name (d)) != NULL)
        {
          gchar *path;

          if (!g_str_has_suffix (file, G_MODULE_SUFFIX))
            continue;

          path = g_build_filename (*ptr, file, NULL);
          plugin_loader_try_to_load (self, path);
          g_free (path);
        }

      g_dir_close (d);
    }

  g_strfreev (dir_array);
}
#endif

static void
gabble_plugin_loader_init (GabblePluginLoader *self)
{
  GabblePluginLoaderPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_PLUGIN_LOADER, GabblePluginLoaderPrivate);

  self->priv = priv;
  priv->plugins = g_ptr_array_new_with_free_func (g_object_unref);
}

static GObject *
gabble_plugin_loader_constructor (
    GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  static gpointer singleton = NULL;

  if (singleton == NULL)
    {
      singleton = G_OBJECT_CLASS (gabble_plugin_loader_parent_class)->
          constructor (type, n_props, props);
      g_object_add_weak_pointer (G_OBJECT (singleton), &singleton);

      return singleton;
    }
  else
    {
      return g_object_ref (singleton);
    }
}

static void
gabble_plugin_loader_constructed (GObject *object)
{
  GabblePluginLoader *self = GABBLE_PLUGIN_LOADER (object);
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (gabble_plugin_loader_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

#ifdef ENABLE_PLUGINS
  gabble_plugin_loader_probe (self);
#else
  DEBUG ("built without plugin support, not actually loading anything");
  (void) self; /* silence unused variable warning. */
#endif
}

static void
gabble_plugin_loader_finalize (GObject *object)
{
  GabblePluginLoader *self = GABBLE_PLUGIN_LOADER (object);
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (gabble_plugin_loader_parent_class)->finalize;

  tp_clear_pointer (&self->priv->plugins, g_ptr_array_unref);

  if (chain_up != NULL)
    chain_up (object);
}

static void
gabble_plugin_loader_class_init (GabblePluginLoaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GabblePluginLoaderPrivate));

  object_class->constructor = gabble_plugin_loader_constructor;
  object_class->constructed = gabble_plugin_loader_constructed;
  object_class->finalize = gabble_plugin_loader_finalize;
}

GabblePluginLoader *
gabble_plugin_loader_dup ()
{
  return g_object_new (GABBLE_TYPE_PLUGIN_LOADER, NULL);
}

static void
create_sidecar_cb (
    GObject *plugin_obj,
    GAsyncResult *nested_result,
    gpointer user_data)
{
  GSimpleAsyncResult *result = user_data;
  GabbleSidecar *sidecar;
  GError *error = NULL;

  sidecar = gabble_plugin_create_sidecar_finish (GABBLE_PLUGIN (plugin_obj),
      nested_result, &error);

  if (sidecar == NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_clear_error (&error);
    }
  else
    {
      g_simple_async_result_set_op_res_gpointer (result, sidecar,
          g_object_unref);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

void
gabble_plugin_loader_create_sidecar (
    GabblePluginLoader *self,
    const gchar *sidecar_interface,
    GabbleConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabblePluginLoaderPrivate *priv = self->priv;
  guint i;

  for (i = 0; i < priv->plugins->len; i++)
    {
      GabblePlugin *p = g_ptr_array_index (priv->plugins, i);

      if (gabble_plugin_implements_sidecar (p, sidecar_interface))
        {
          GSimpleAsyncResult *res = g_simple_async_result_new (G_OBJECT (self),
              callback, user_data, gabble_plugin_loader_create_sidecar);

          GabblePluginConnection *gabble_conn =
            GABBLE_PLUGIN_CONNECTION (connection);
          gabble_plugin_create_sidecar_async (p, sidecar_interface,
              gabble_conn, session, create_sidecar_cb, res);
          return;
        }
    }

  g_simple_async_report_error_in_idle (G_OBJECT (self), callback, user_data,
      TP_ERROR, TP_ERROR_NOT_IMPLEMENTED, "No plugin implements sidecar '%s'",
      sidecar_interface);
}

GabbleSidecar *
gabble_plugin_loader_create_sidecar_finish (
    GabblePluginLoader *self,
    GAsyncResult *result,
    GError **error)
{
  GabbleSidecar *sidecar;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (self), gabble_plugin_loader_create_sidecar), NULL);

  sidecar = GABBLE_SIDECAR (g_simple_async_result_get_op_res_gpointer (
      G_SIMPLE_ASYNC_RESULT (result)));
  return g_object_ref (sidecar);
}

TpPresenceStatusSpec *
gabble_plugin_loader_append_statuses (
    GabblePluginLoader *self,
    const TpPresenceStatusSpec *base_statuses)
{
  GabblePluginLoaderPrivate *priv = self->priv;
  GArray *result = g_array_new (TRUE, TRUE, sizeof (TpPresenceStatusSpec));
  guint i;

  for (i = 0; base_statuses[i].name != NULL; i++)
    g_array_append_val (result, base_statuses[i]);

  for (i = 0; i < priv->plugins->len; i++)
    {
      GabblePlugin *p = g_ptr_array_index (priv->plugins, i);
      const TpPresenceStatusSpec *statuses =
          gabble_plugin_get_custom_presence_statuses (p);

      if (statuses != NULL)
        {
          guint j;

          for (j = 0; statuses[j].name != NULL; j++)
            g_array_append_val (result, statuses[j]);
        }
    }

  return (TpPresenceStatusSpec *) g_array_free (result, FALSE);
}

const gchar *
gabble_plugin_loader_presence_status_for_privacy_list (
    GabblePluginLoader *loader,
    const gchar *list_name)
{
  GabblePluginLoaderPrivate *priv = loader->priv;
  guint i;

  for (i = 0; i < priv->plugins->len; i++)
    {
      GabblePlugin *p = g_ptr_array_index (priv->plugins, i);
      const gchar *status =
        gabble_plugin_presence_status_for_privacy_list (p, list_name);

      if (status != NULL)
        return status;

    }

  return NULL;
}

static void
copy_to_other_array (gpointer data,
    gpointer user_data)
{
  g_ptr_array_add (user_data, data);
}

GPtrArray *
gabble_plugin_loader_create_channel_managers (
    GabblePluginLoader *self,
    GabblePluginConnection *plugin_connection)
{
  GPtrArray *out = g_ptr_array_new ();
  guint i;

  for (i = 0; i < self->priv->plugins->len; i++)
    {
      GabblePlugin *plugin = g_ptr_array_index (self->priv->plugins, i);
      GPtrArray *managers;

      managers = gabble_plugin_create_channel_managers (plugin,
          plugin_connection);

      if (managers == NULL)
        continue;

      g_ptr_array_foreach (managers, copy_to_other_array, out);
      g_ptr_array_unref (managers);
    }

  return out;
}
