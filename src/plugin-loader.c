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

#include "plugin-loader.h"

#include <glib.h>

#ifdef ENABLE_PLUGINS
# include <gmodule.h>
#endif

#include <telepathy-glib/errors.h>

#define DEBUG_FLAG GABBLE_DEBUG_PLUGINS
#include "debug.h"
#include "plugin.h"

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

      DEBUG ("loaded '%s' (%s), implementing these sidecars: %s",
          gabble_plugin_get_name (plugin), path, sidecars);

      g_free (sidecars);

      g_ptr_array_add (self->priv->plugins, plugin);
    }
}

static void
gabble_plugin_loader_probe (GabblePluginLoader *self)
{
  GError *error = NULL;
  GDir *d;
  const gchar *file;

  if (!g_module_supported ())
    {
      DEBUG ("modules aren't supported on this platform.");
      return;
    }

  d = g_dir_open (PLUGIN_DIR, 0, &error);

  if (d == NULL)
    {
      DEBUG ("%s", error->message);
      g_error_free (error);
      return;
    }

  while ((file = g_dir_read_name (d)) != NULL)
    {
      gchar *path;

      if (!g_str_has_suffix (file, G_MODULE_SUFFIX))
        continue;

      path = g_build_filename (PLUGIN_DIR, file, NULL);
      plugin_loader_try_to_load (self, path);
      g_free (path);
    }

  g_dir_close (d);
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
  static GObject *singleton = NULL;

  if (singleton == NULL)
    {
      /* We keep a ref in here to ensure the loader never dies. */
      singleton = G_OBJECT_CLASS (gabble_plugin_loader_parent_class)->
          constructor (type, n_props, props);
    }

  return g_object_ref (singleton);
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

  g_ptr_array_free (self->priv->plugins, TRUE);
  self->priv->plugins = NULL;

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

          gabble_plugin_create_sidecar (p, sidecar_interface, create_sidecar_cb,
              res);
          return;
        }
    }

  g_simple_async_report_error_in_idle (G_OBJECT (self), callback, user_data,
      TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "No plugin implements sidecar '%s'",
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
