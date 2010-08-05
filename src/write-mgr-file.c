/*
 * write_mgr_file.c - utility to produce gabble.manager. Part of Gabble.
 * Copyright (C) 2006-2010 Collabora Ltd.
 * Copyright (C) 2006-2010 Nokia Corporation
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
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-protocol.h>
#include <telepathy-glib/telepathy-glib.h>

#include "protocol.h"


#define MAYBE_WRITE_STR(prop, key) \
  { \
    const gchar *val = tp_asv_get_string (props, prop); \
    if (val && *val) \
        g_key_file_set_string (f, section_name, key, val); \
  }

static void
write_parameters (GKeyFile *f, gchar *section_name, TpBaseProtocol *protocol)
{
  const TpCMParamSpec *parameters =
      tp_base_protocol_get_parameters (protocol);
  const TpCMParamSpec *row;

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
}

static void
write_rcc_property (GKeyFile *keyfile,
                    const gchar *group_name,
                    const gchar *key,
                    GValue *val)
{
  switch (G_VALUE_TYPE (val))
    {
      case G_TYPE_BOOLEAN:
        {
          gchar *kf_key = g_strconcat (key,
            " " DBUS_TYPE_BOOLEAN_AS_STRING, NULL);
          g_key_file_set_boolean (keyfile, group_name, kf_key,
            g_value_get_boolean (val));
          g_free (kf_key);
          break;
        }

      case G_TYPE_STRING:
        {
          gchar *kf_key = g_strconcat (key,
            " " DBUS_TYPE_STRING_AS_STRING, NULL);
          g_key_file_set_string (keyfile, group_name, kf_key,
            g_value_get_string (val));
          g_free (kf_key);
          break;
        }

      case G_TYPE_UINT:
        {
          gchar *kf_key = g_strconcat (key,
            " " DBUS_TYPE_UINT32_AS_STRING, NULL);
          gchar *kf_val = g_strdup_printf ("%u", g_value_get_uint (val));
          g_key_file_set_value (ctx->keyfile, ctx->group_name, kf_key, kf_val);
          g_free (kf_key);
          g_free (kf_val);
          break;
        }

      case G_TYPE_UINT64:
        {
          gchar *kf_key = g_strconcat (key,
            " " DBUS_TYPE_UINT64_AS_STRING, NULL);
          gchar *kf_val = g_strdup_printf ("%llu", g_value_get_uint64 (val));
          g_key_file_set_value (ctx->keyfile, ctx->group_name, kf_key, kf_val);
          g_free (kf_key);
          g_free (kf_val);
          break;
        }

      case G_TYPE_INT:
        {
          gchar *kf_key = g_strconcat (key,
            " " DBUS_TYPE_INT32_AS_STRING, NULL);
          g_key_file_set_integer (keyfile, group_name, kf_key,
            g_value_get_int (val));
          g_free (kf_key);
          break;
        }

      case G_TYPE_INT64:
        {
          gchar *kf_key = g_strconcat (key,
            " " DBUS_TYPE_UINT64_AS_STRING, NULL);
          gchar *kf_val = g_strdup_printf ("%lld", g_value_get_int64 (val));
          g_key_file_set_value (ctx->keyfile, ctx->group_name, kf_key, kf_val);
          g_free (kf_key);
          g_free (kf_val);
          break;
        }

      default:
        {
          if (G_VALUE_TYPE (val) == G_TYPE_STRV)
            {
              gchar **list = g_value_get_boxed (val);
              gchar *kf_key = g_strconcat (key, " "
                  DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_STRING_AS_STRING, NULL);
              g_key_file_set_string_list (keyfile, group_name,
                  kf_key, (const gchar **) list, g_strv_length (list));
              g_free (kf_key);
              break;
            }

          /* we'd rather crash than forget to write required rcc property */
          g_assert_not_reached ();
        }
    }
}

static gchar *
generate_group_name (GHashTable *props)
{
  static guint counter = 0;
  gchar *retval;
  gchar *chan_type = g_ascii_strdown (tp_asv_get_string (props,
      TP_PROP_CHANNEL_CHANNEL_TYPE), -1);
  gchar *chan_type_suffix;
  gchar *handle_type_name;
  guint handle_type = tp_asv_get_uint32 (props,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);

  g_assert (chan_type != NULL);
  chan_type_suffix = strrchr (chan_type, '.');
  g_assert (chan_type_suffix != NULL);
  chan_type_suffix++;

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handle_type_name = "-1on1";
      break;

    case TP_HANDLE_TYPE_ROOM:
      handle_type_name = "-multi";
      break;

    case TP_HANDLE_TYPE_GROUP:
      handle_type_name = "-group";
      break;

    case TP_HANDLE_TYPE_LIST:
      handle_type_name = "-list";
      break;

    default:
      handle_type_name = "";
    }

  retval = g_strdup_printf ("%s%s-%d", chan_type_suffix, handle_type_name,
      ++counter);

  g_free (chan_type);
  return retval;
}

static void
write_rccs (GKeyFile *f, const gchar *section_name, GHashTable *props)
{
  GPtrArray *rcc_list = tp_asv_get_boxed (props,
      TP_PROP_PROTOCOL_REQUESTABLE_CHANNEL_CLASSES,
      TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);
  guint i;
  gchar **group_names = g_new0 (gchar *, rcc_list->len + 1);

  for (i = 0; i < rcc_list->len; i++)
    {
      gchar **allowed;
      gchar *group_name;
      GHashTable *fixed;
      GHashTableIter iter;
      gpointer k, v;

      tp_value_array_unpack (g_ptr_array_index (rcc_list, i), 2,
        &fixed, &allowed);

      group_name = generate_group_name (fixed);

      g_hash_table_iter_init (&iter, fixed);
      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          const gchar *key = k;
          GValue *val = v;

          write_rcc_property (f, group_name, key, val);
        }

      /* takes ownership */
      group_names[i] = group_name;

      g_key_file_set_string_list (f, group_name, "allowed",
        (const gchar **) allowed, g_strv_length (allowed));
    }

  g_key_file_set_string_list (f, section_name, "RequestableChannelClasses",
      (const gchar **) group_names, rcc_list->len);

  g_strfreev (group_names);
}

static gchar *
mgr_file_contents (const char *busname,
                   const char *objpath,
                   GSList *protocols,
                   GError **error)
{
  GKeyFile *f = g_key_file_new ();
  gchar *file_data;

  g_key_file_set_string (f, "ConnectionManager", "BusName", busname);
  g_key_file_set_string (f, "ConnectionManager", "ObjectPath", objpath);

  /* there are no CM interfaces defined yet, so we cheat */
  g_key_file_set_string (f, "ConnectionManager", "Interfaces", "");

  while (protocols != NULL)
    {
      TpBaseProtocol *protocol = protocols->data;
      GHashTable *props =
          tp_base_protocol_get_immutable_properties (protocol);
      gchar *section_name = g_strdup_printf ("Protocol %s",
          tp_base_protocol_get_name (protocol));
      const gchar * const *ifaces = tp_asv_get_strv (props,
          TP_PROP_PROTOCOL_INTERFACES);
      const gchar * const *c_ifaces = tp_asv_get_strv (props,
          TP_PROP_PROTOCOL_CONNECTION_INTERFACES);

      write_parameters (f, section_name, protocol);
      write_rccs (f, section_name, props);

      g_key_file_set_string_list (f, section_name, "Interfaces",
          ifaces, g_strv_length ((gchar **) ifaces));
      g_key_file_set_string_list (f, section_name, "ConnectionInterfaces",
          c_ifaces, g_strv_length ((gchar **) c_ifaces));

      MAYBE_WRITE_STR (TP_PROP_PROTOCOL_VCARD_FIELD, "VCardField");
      MAYBE_WRITE_STR (TP_PROP_PROTOCOL_ENGLISH_NAME, "EnglishName");
      MAYBE_WRITE_STR (TP_PROP_PROTOCOL_ICON, "Icon");

      g_free (section_name);
      g_hash_table_destroy (props);
      protocols = protocols->next;
    }

  file_data = g_key_file_to_data (f, NULL, error);
  g_key_file_free (f);
  return file_data;
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
