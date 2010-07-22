/*
 * write_mgr_file.c - utility to produce gabble.manager. Part of Gabble.
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#include <stdio.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-protocol.h>
#include <telepathy-glib/enums.h>

#include "connection-manager.h"
#include "protocol.h"

static gchar *
mgr_file_contents (const char *busname,
                   const char *objpath,
                   GSList *protocols,
                   GError **error)
{
  GKeyFile *f = g_key_file_new ();

  g_key_file_set_string (f, "ConnectionManager", "BusName", busname);
  g_key_file_set_string (f, "ConnectionManager", "ObjectPath", objpath);

  while (protocols != NULL)
    {
      TpBaseProtocol *protocol = protocols->data;
      const TpCMParamSpec *parameters =
          tp_base_protocol_get_parameters (protocol);
      const TpCMParamSpec *row;
      gchar *section_name = g_strdup_printf ("Protocol %s",
          tp_base_protocol_get_name (protocol));

      for (row = parameters; row->name; row++)
        {
          gchar *param_name = g_strdup_printf ("param-%s", row->name);
          gchar *param_value = g_strdup_printf ("%s%s%s%s", row->dtype,
              (row->flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED ? " required" : ""),
              (row->flags & TP_CONN_MGR_PARAM_FLAG_REGISTER ? " register" : ""),
              (row->flags & TP_CONN_MGR_PARAM_FLAG_SECRET ? " secret" : ""));
          g_key_file_set_string (f, section_name, param_name, param_value);
          g_free (param_value);
          g_free (param_name);
        }

      for (row = parameters; row->name; row++)
        {
          if (row->flags & TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT)
            {
              gchar *default_name = g_strdup_printf ("default-%s", row->name);

              switch (row->gtype)
                {
                case G_TYPE_STRING:
                  g_key_file_set_string (f, section_name, default_name,
                                        row->def);
                  break;
                case G_TYPE_INT:
                case G_TYPE_UINT:
                  g_key_file_set_integer (f, section_name, default_name,
                                         GPOINTER_TO_INT(row->def));
                  break;
                case G_TYPE_BOOLEAN:
                  g_key_file_set_boolean (f, section_name, default_name,
                                         GPOINTER_TO_INT(row->def) ? 1 : 0);
                  break;
                default:
                  /* can't be in the case because G_TYPE_STRV is actually a
                   * function */
                  if (row->gtype == G_TYPE_STRV)
                    {
                      g_key_file_set_string_list (f, section_name, default_name,
                          (const gchar **) row->def,
                          g_strv_length ((gchar **) row->def));
                    }
                }
              g_free (default_name);
            }
        }
      g_free (section_name);
      protocols = protocols->next;
    }
  return g_key_file_to_data (f, NULL, error);
}

int
main (void)
{
  GError *error = NULL;
  gchar *s;
  GSList *protocols = NULL;

  g_type_init ();
  dbus_g_type_specialized_init ();

  protocols = g_slist_prepend (protocols,
    gabble_jabber_protocol_new ());

  s = mgr_file_contents (TP_CM_BUS_NAME_BASE "gabble",
      TP_CM_OBJECT_PATH_BASE "gabble",
      protocols, &error);

  g_object_unref (protocols->data);
  g_slist_free (protocols);

  if (!s)
    {
      fprintf (stderr, "%s", error->message);
      g_error_free (error);
      return 1;
    }
  printf ("%s", s);
  g_free (s);
  return 0;
}
