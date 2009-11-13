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
#include <gmodule.h>

#define DEBUG_FLAG GABBLE_DEBUG_PLUGINS
#include "debug.h"
#include "plugin.h"

#ifdef ENABLE_PLUGINS
void
gabble_plugin_loader_load (void)
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
      GModule *m;
      gchar *path;

      if (!g_str_has_suffix (file, G_MODULE_SUFFIX))
        continue;

      path = g_build_filename (PLUGIN_DIR, file, NULL);
      m = g_module_open (path, G_MODULE_BIND_LOCAL);

      if (m == NULL)
        {
          const gchar *e = g_module_error ();

          /* the errors often seem to be prefixed by the filename */
          if (g_str_has_prefix (e, path))
            DEBUG ("%s", e);
          else
            DEBUG ("%s: %s", path, e);
        }
      else
        {
          gpointer func;

          if (!g_module_symbol (m, "gabble_plugin_create", &func))
            {
              DEBUG ("%s", g_module_error ());

              g_module_close (m);
            }
          else
            {
              GabblePluginCreateImpl create = func;
              GabblePlugin *plugin;

              /* We're about to try to instantiate an object. This installs the
               * class with the type system, so we should ensure that this
               * plug-in is never accidentally unloaded.
               */
              g_module_make_resident (m);

              /* Here goes nothing... */
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
                  g_object_unref (plugin);
                }
            }
        }

      g_free (path);
    }

  g_dir_close (d);
}

#else /* ! ENABLE_PLUGINS */

void
gabble_plugin_loader_load (void)
{
  DEBUG ("built without plugin support");
}

#endif
