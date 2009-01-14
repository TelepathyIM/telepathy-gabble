
#define _GNU_SOURCE
#include "config.h"
#include "conn-location.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#define DEBUG_FLAG GABBLE_DEBUG_LOCATION

#include "debug.h"
#include "extensions/extensions.h"
#include "namespaces.h"
#include "pubsub.h"
#include "presence-cache.h"

static gboolean update_location_from_msg (GabbleConnection *conn,
    const gchar *from, LmMessage *msg);

static gboolean
validate_contacts (TpBaseConnection *base,
                   DBusGMethodInvocation *context,
                   const GArray *contacts)
{
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;

  if (!tp_handles_are_valid (contact_handles, contacts, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
lm_message_node_get_double (LmMessageNode *node,
                            gdouble *d)
{
  const gchar *value;
  gchar *end;

  value = lm_message_node_get_value (node);

  if (value == NULL)
    return FALSE;

  *d = strtod (value, &end);

  if (end == value)
    return FALSE;

  return TRUE;
}

static gboolean
lm_message_node_get_string (LmMessageNode *node,
                            gchar **s)
{
  const gchar *value;

  value = lm_message_node_get_value (node);

  if (value == NULL)
    return FALSE;

  *s = g_strdup (value);
  return TRUE;
}

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
  guint i;
  GHashTable *return_locations = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_hash_table_destroy);

  DEBUG ("GetLocation for contacts:");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_GEOLOCATION);
  if (!validate_contacts (base, context, contacts))
    return;

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
          g_hash_table_insert (return_locations, GINT_TO_POINTER (contact),
              location);
        }
      else if (!pubsub_query (conn, jid, NS_GEOLOC, pep_reply_cb, NULL))
        {
          GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Sending PEP location query failed" };

          dbus_g_method_return_error (context, &error);
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

  if (G_VALUE_TYPE (value) == G_TYPE_UINT64)
    {
      time_t stamp = g_value_get_uint64 (value);
      struct tm *ptm = gmtime (&stamp);
      gchar str[30];

      if (strftime (str, sizeof(str), "%Y%m%dT%TZ", ptm) == 0)
        return;

      lm_message_node_add_child (geoloc, key, str);
      DEBUG ("\t - %s: %s", (gchar*) key, str);
      g_free (str);
    }
  if (G_VALUE_TYPE (value) == G_TYPE_DOUBLE)
    {
      gchar *str;
      str = g_strdup_printf ("%.6f", g_value_get_double (value));
      lm_message_node_add_child (geoloc, key, str);
      DEBUG ("\t - %s: %s", (gchar*) key, str);
      g_free (str);
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    {
      lm_message_node_add_child (geoloc, key, g_value_get_string(value));
      DEBUG ("\t - %s: %s", (gchar*) key, g_value_get_string (value));
    }
  else
    DEBUG ("\t - Unknown key dropped: %s", (gchar*) key);


}

static void
location_set_location (GabbleSvcConnectionInterfaceLocation *iface,
                       GHashTable *location,
                       DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *msg;
  LmMessageNode *geoloc;

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
      GabbleRichPresenceAccessControlType access_control_type =
        GABBLE_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST;
      GArray *access_control = g_array_sized_new (FALSE, FALSE,
          sizeof (GabbleRichPresenceAccessControlType), 1);

      g_array_append_val (access_control, access_control_type);
      g_value_take_boxed (value, access_control);
    }
  else if (!tp_strdiff (g_quark_to_string (name), "LocationAccessControl"))
    {
      GValueArray *access_control = g_value_array_new (2);
      GValue type = {0,};
      GValue variant = {0,};
      GValue *allocated_value;

      /* G_TYPE_UINT is the type of GabbleRichPresenceAccessControlType */
      g_value_init (&type, G_TYPE_UINT);
      g_value_set_uint (&type,
          GABBLE_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST);
      g_value_array_append (access_control, &type);
      g_value_unset (&type);

      g_value_init (&variant, G_TYPE_VALUE);
      /* G_TYPE_UINT is a random type, it is not used */
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
  GabbleRichPresenceAccessControlType access_control_type;
  g_return_val_if_fail (interface ==
      GABBLE_IFACE_QUARK_CONNECTION_INTERFACE_LOCATION, FALSE);

  g_assert (name == g_quark_from_static_string ("LocationAccessControl"));

  access_control = g_value_get_boxed (value);
  
  access_control_type_value = g_value_array_get_nth (access_control, 0);
  access_control_type = g_value_get_uint (access_control_type_value);

  if (access_control_type !=
      GABBLE_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST)
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
  LmMessageNode *node, *subloc_node;
  GHashTable *location = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      g_free, (GDestroyNotify) tp_g_value_slice_free);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);

  TpHandle contact = tp_handle_lookup (contact_repo, from, NULL, NULL);

  node = lm_message_node_find_child (msg->node, "geoloc");
  if (node == NULL)
    return FALSE;

  DEBUG ("LocationsUpdate for %s:", from);

  for (subloc_node = node->children; subloc_node != NULL;
      subloc_node = subloc_node->next)
    {
      GValue *value = NULL;
      gdouble double_value;
      gchar *key, *str;

      key = subloc_node->name;

      if ((strcmp (key, "lat") == 0 ||
           strcmp (key, "lon") == 0 ||
           strcmp (key, "alt") == 0 ||
           strcmp (key, "accuracy") == 0) &&
          lm_message_node_get_double (subloc_node, &double_value))
        {
          value = g_slice_new0 (GValue);
          g_value_init (value, G_TYPE_DOUBLE);
          g_value_set_double (value, double_value);
          DEBUG ("\t - %s: %f", key, double_value);
        }
      else if (lm_message_node_get_string (subloc_node, &str))
        {
          if (strcmp (key, "timestamp") == 0)
            {
              struct tm ptm;
              gchar * p = strptime (str, "%Y%m%dT%T", &ptm);
              if (p != NULL)
                {
                  guint64 stamp = mktime (&ptm);
                  value = g_slice_new0 (GValue);
                  g_value_init (value, G_TYPE_UINT64);
                  g_value_set_uint64 (value, stamp);
                  DEBUG ("\t - %s: %" G_GUINT64_FORMAT, key, stamp);
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
              g_value_take_string (value, str);
              DEBUG ("\t - %s: %s", key, str);
            }
        }
      else
        {
          DEBUG ("Unable to read the key %s from the location of %s",
              key, from);
          continue;
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

