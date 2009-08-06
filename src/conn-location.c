
#define _GNU_SOURCE
#include "config.h"
#include "conn-location.h"

#include <string.h>
#include <stdlib.h>

#define DEBUG_FLAG GABBLE_DEBUG_LOCATION

#include "debug.h"
#include "extensions/extensions.h"
#include "namespaces.h"
#include "pubsub.h"
#include "presence-cache.h"
#include "util.h"

static gboolean update_location_from_msg (GabbleConnection *conn,
    const gchar *from, LmMessage *msg);

static LmHandlerResult
pep_reply_cb (GabbleConnection *conn,
              LmMessage *sent_msg,
              LmMessage *reply_msg,
              GObject *object,
              gpointer user_data)
{
  const gchar *from;

  from = lm_message_node_get_attribute (reply_msg->node, "from");

  if (from != NULL)
    update_location_from_msg (conn, from, reply_msg);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
location_get_locations (GabbleSvcConnectionInterfaceLocation *iface,
                        const GArray *contacts,
                        DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles;
  guint i;
  GError *error = NULL;
  GHashTable *return_locations = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_hash_table_destroy);

  DEBUG ("GetLocation for contacts:");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_GEOLOCATION);

  /* Validate contacts */
  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (contact_handles, contacts, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      g_hash_table_unref (return_locations);
      return;
    }


  for (i = 0; i < contacts->len; i++)
    {
      TpHandleRepoIface *contact_repo;
      const gchar *jid;
      GHashTable *location;
      TpHandle contact = g_array_index (contacts, TpHandle, i);

      contact_repo = tp_base_connection_get_handles (base,
          TP_HANDLE_TYPE_CONTACT);
      jid = tp_handle_inspect (contact_repo, contact);

      location = gabble_presence_cache_get_location (conn->presence_cache,
          contact);
      if (location != NULL)
        {
          DEBUG (" - %s: cached", jid);
          g_hash_table_insert (return_locations, GUINT_TO_POINTER (contact),
              location);
        }
      else if (!pubsub_query (conn, jid, NS_GEOLOC, pep_reply_cb, NULL))
        {
          GError error2 = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Sending PEP location query failed" };

          dbus_g_method_return_error (context, &error2);
          g_hash_table_unref (return_locations);

          return;
        } else
           DEBUG (" - %s: requested", jid);
    }

  gabble_svc_connection_interface_location_return_from_get_locations
      (context, return_locations);
  g_hash_table_unref (return_locations);

}

static void
create_msg_foreach (gpointer key,
                    gpointer value,
                    gpointer user_data)
{
  LmMessageNode *geoloc = (LmMessageNode *) user_data;

  if (G_VALUE_TYPE (value) == G_TYPE_INT64)
    {
      GTimeVal timeval;
      gchar *str;

      timeval.tv_sec = CLAMP (g_value_get_int64 (value), 0, G_MAXLONG);
      timeval.tv_usec = 0;
      str = g_time_val_to_iso8601 (&timeval);

      lm_message_node_add_child (geoloc, key, str);
      DEBUG ("\t - %s: %s", (gchar *) key, str);
      g_free (str);
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_DOUBLE)
    {
      gchar *str;
      str = g_strdup_printf ("%.6f", g_value_get_double (value));
      lm_message_node_add_child (geoloc, key, str);
      DEBUG ("\t - %s: %s", (gchar *) key, str);
      g_free (str);
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    {
      const gchar *str = g_value_get_string (value);

      if (!tp_strdiff (key, "language"))
        {
          /* Set the xml:lang */
          lm_message_node_set_attribute (geoloc, "xml:lang", str);
        }
      else
        {
          lm_message_node_add_child (geoloc, key, str);
        }
      DEBUG ("\t - %s: %s", (gchar *) key, str);
    }
  else
    DEBUG ("\t - Unknown key dropped: %s", (gchar *) key);


}

static void
location_set_location (GabbleSvcConnectionInterfaceLocation *iface,
                       GHashTable *location,
                       DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *msg;
  LmMessageNode *geoloc;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED ((TpBaseConnection *) conn,
    context);

  if (!(conn->features & GABBLE_CONNECTION_FEATURES_PEP))
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Server does not support PEP, cannot publish geolocation" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_GEOLOCATION);
  msg = pubsub_make_publish_msg (NULL, NS_GEOLOC, NS_GEOLOC, "geoloc",
      &geoloc);

  DEBUG ("SetLocation to");

  g_hash_table_foreach (location, create_msg_foreach, geoloc);

  /* XXX: use _ignore_reply */
  if (!_gabble_connection_send (conn, msg, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Failed to send msg" };

      dbus_g_method_return_error (context, &error);
    }
  else
    dbus_g_method_return (context);

  lm_message_unref (msg);
}

void
location_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcConnectionInterfaceLocationClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_connection_interface_location_implement_##x \
  (klass, location_##x)
  IMPLEMENT(get_locations);
  IMPLEMENT(set_location);
#undef IMPLEMENT
}

void
conn_location_properties_getter (GObject *object,
                                 GQuark interface,
                                 GQuark name,
                                 GValue *value,
                                 gpointer getter_data)
{
  if (!tp_strdiff (g_quark_to_string (name), "LocationAccessControlTypes"))
    {
      guint access_control_type =
        TP_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST;
      GArray *access_control = g_array_sized_new (FALSE, FALSE,
          sizeof (guint), 1);

      g_array_append_val (access_control, access_control_type);
      g_value_take_boxed (value, access_control);
    }
  else if (!tp_strdiff (g_quark_to_string (name), "LocationAccessControl"))
    {
      GValueArray *access_control = g_value_array_new (2);
      GValue type = {0,};
      GValue variant = {0,};
      GValue *allocated_value;

      /* G_TYPE_UINT is the D-Bus type of TpRichPresenceAccessControlType */
      g_value_init (&type, G_TYPE_UINT);
      g_value_set_uint (&type,
          TP_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST);
      g_value_array_append (access_control, &type);
      g_value_unset (&type);

      g_value_init (&variant, G_TYPE_VALUE);
      /* For Publish_List, the variant isn't used, so we set a dummy value,
       * (guint) 0 */
      allocated_value = tp_g_value_slice_new (G_TYPE_UINT);
      g_value_set_uint (allocated_value, 0);
      g_value_set_boxed (&variant, allocated_value);
      g_value_array_append (access_control, &variant);
      g_value_unset (&variant);
      tp_g_value_slice_free (allocated_value);

      g_value_take_boxed (value, access_control);
    }
  else
    {
      g_assert_not_reached ();
    }
}

gboolean
conn_location_properties_setter (GObject *object,
                                GQuark interface,
                                GQuark name,
                                const GValue *value,
                                gpointer setter_data,
                                GError **error)
{
  GValueArray *access_control;
  GValue *access_control_type_value;
  TpRichPresenceAccessControlType access_control_type;
  g_return_val_if_fail (interface ==
      GABBLE_IFACE_QUARK_CONNECTION_INTERFACE_LOCATION, FALSE);

  /* There is only one property with write access. So TpDBusPropertiesMixin
   * already checked this. */
  g_assert (name == g_quark_from_static_string ("LocationAccessControl"));

  access_control = g_value_get_boxed (value);

  /* TpDBusPropertiesMixin already checked this */
  g_assert (access_control->n_values == 2);

  access_control_type_value = g_value_array_get_nth (access_control, 0);

  /* TpDBusPropertiesMixin already checked this */
  g_assert (G_VALUE_TYPE (access_control_type_value) == G_TYPE_UINT);

  access_control_type = g_value_get_uint (access_control_type_value);

  if (access_control_type !=
      TP_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Access control type not implemented");
      return FALSE;
    }

  return TRUE;
}

static gboolean
update_location_from_msg (GabbleConnection *conn,
                          const gchar *from,
                          LmMessage *msg)
{
  LmMessageNode *node;
  GHashTable *location = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      g_free, (GDestroyNotify) tp_g_value_slice_free);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  NodeIter i;

  TpHandle contact = tp_handle_lookup (contact_repo, from, NULL, NULL);

  node = lm_message_node_find_child (msg->node, "geoloc");
  if (node == NULL)
    return FALSE;

  DEBUG ("LocationsUpdate for %s:", from);

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *subloc_node = node_iter_data (i);
      GValue *value = NULL;
      gchar *key;
      const gchar *str;

      key = subloc_node->name;
      str = lm_message_node_get_value (subloc_node);
      if (str == NULL)
        continue;

      if ((strcmp (key, "lat") == 0 ||
           strcmp (key, "lon") == 0 ||
           strcmp (key, "alt") == 0 ||
           strcmp (key, "accuracy") == 0))
        {
          gdouble double_value;
          gchar *end;

          double_value = g_ascii_strtod (str, &end);

          if (end == str)
            continue;

          value = g_slice_new0 (GValue);
          g_value_init (value, G_TYPE_DOUBLE);
          g_value_set_double (value, double_value);
          DEBUG ("\t - %s: %f", key, double_value);
        }
      else if (strcmp (key, "timestamp") == 0)
        {
          GTimeVal timeval;
          if (g_time_val_from_iso8601 (str, &timeval))
            {
              value = g_slice_new0 (GValue);
              g_value_init (value, G_TYPE_INT64);
              g_value_set_int64 (value, timeval.tv_sec);
              DEBUG ("\t - %s: %s", key, str);
            }
          else
            {
              DEBUG ("\t - %s: %s: unknown date format", key, str);
              continue;
            }
        }
      else
        {
          value = g_slice_new0 (GValue);
          g_value_init (value, G_TYPE_STRING);
          g_value_set_string (value, str);
          DEBUG ("\t - %s: %s", key, str);
        }

      g_hash_table_insert (location, g_strdup (key), value);
    }

  gabble_svc_connection_interface_location_emit_location_updated (conn,
      contact, location);
  gabble_presence_cache_update_location (conn->presence_cache, contact,
      location);

  return TRUE;
}

gboolean
geolocation_event_handler (GabbleConnection *conn,
                           LmMessage *msg,
                           TpHandle handle)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  const gchar *from;

  if (handle == base->self_handle)
    /* Ignore echoed pubsub notifications */
    return TRUE;

  from = lm_message_node_get_attribute (msg->node, "from");

  return update_location_from_msg (conn, from, msg);
}

