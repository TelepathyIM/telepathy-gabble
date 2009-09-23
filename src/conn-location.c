
#define _GNU_SOURCE
#include "config.h"
#include "conn-location.h"

#include <string.h>
#include <stdlib.h>

#define DEBUG_FLAG GABBLE_DEBUG_LOCATION

#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>

#include "debug.h"
#include "namespaces.h"
#include "pubsub.h"
#include "presence-cache.h"
#include "util.h"

typedef struct
{
  gchar *xmpp_name;
  gchar *tp_name;
  GType type;
} LocationMapping;

static const LocationMapping mappings[] = {
  { "alt", "alt", G_TYPE_DOUBLE },
  { "area", "area", G_TYPE_STRING },
  { "bearing", "bearing", G_TYPE_DOUBLE },
  { "building", "building", G_TYPE_STRING },
  { "country", "country", G_TYPE_STRING },
  { "description", "description", G_TYPE_STRING },
  { "error", "error", G_TYPE_DOUBLE },
  { "floor", "floor", G_TYPE_STRING },
  { "lat", "lat", G_TYPE_DOUBLE },
  { "locality", "locality", G_TYPE_STRING },
  { "lon", "lon", G_TYPE_DOUBLE },
  { "postalcode", "postalcode", G_TYPE_STRING },
  { "region", "region", G_TYPE_STRING },
  { "room", "room", G_TYPE_STRING },
  { "speed", "speed", G_TYPE_DOUBLE },
  { "street", "street", G_TYPE_STRING },
  { "text", "text", G_TYPE_STRING },
  { "timestamp", "timestamp", G_TYPE_INT64 },
  { "uri", "uri", G_TYPE_STRING },
  /* Not (yet?) part of XEP-0080 */
  { "countrycode", "countrycode", G_TYPE_STRING },
  /* language is a special case as it's not mapped on a node but on the
   * xml:lang attribute of the 'geoloc' node. */
  { NULL, NULL },
};

static GHashTable *xmpp_to_tp = NULL;
static GHashTable *tp_to_xmpp = NULL;

static void
build_mapping_tables (void)
{
  guint i;

  if (xmpp_to_tp != NULL)
    return;
  g_assert (tp_to_xmpp == NULL);

  xmpp_to_tp = g_hash_table_new (g_str_hash, g_str_equal);
  tp_to_xmpp = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = 0; mappings[i].xmpp_name != NULL; i++)
    {
      g_hash_table_insert (xmpp_to_tp, mappings[i].xmpp_name,
          (gpointer) &mappings[i]);
      g_hash_table_insert (tp_to_xmpp, mappings[i].tp_name,
          (gpointer) &mappings[i]);
    }
}

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

static GHashTable *
get_cached_location_or_query (GabbleConnection *conn,
    TpHandle contact,
    GError **error)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GHashTable *location;
  const gchar *jid;
  TpHandleRepoIface *contact_repo;

  contact_repo = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  jid = tp_handle_inspect (contact_repo, contact);

  location = gabble_presence_cache_get_location (conn->presence_cache, contact);
  if (location != NULL)
    {
      DEBUG (" - %s: cached", jid);
      return location;
    }

  /* Send a query */
  if (pubsub_query (conn, jid, NS_GEOLOC, pep_reply_cb, error))
    DEBUG (" - %s: requested", jid);

  return NULL;
}

static void
location_get_locations (TpSvcConnectionInterfaceLocation *iface,
                        const GArray *contacts,
                        DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles;
  guint i;
  GError *error = NULL;
  GHashTable *return_locations = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_hash_table_unref);

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
      GHashTable *location;
      TpHandle contact = g_array_index (contacts, TpHandle, i);

      location = get_cached_location_or_query (conn, contact, &error);
      if (error != NULL)
        {
          GError error2 = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Sending PEP location query failed" };

          DEBUG ("Sending PEP location query failed: %s", error->message);
          g_error_free (error);
          dbus_g_method_return_error (context, &error2);
          g_hash_table_unref (return_locations);
          return;
        }

      if (location != NULL)
        g_hash_table_insert (return_locations, GUINT_TO_POINTER (contact),
            location);
    }

  tp_svc_connection_interface_location_return_from_get_locations
      (context, return_locations);
  g_hash_table_unref (return_locations);
}

static gboolean
add_to_geoloc_node (const gchar *tp_name,
    GValue *value,
    LmMessageNode *geoloc,
    GError **err)
{
  LocationMapping *mapping;
  gchar *str = NULL;

  /* Map "language" to the xml:lang attribute. */
  if (!tp_strdiff (tp_name, "language"))
    {
      if (G_VALUE_TYPE (value) != G_TYPE_STRING)
        {
          g_set_error (err, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "expecting string for language value, but got %s",
                  G_VALUE_TYPE_NAME (value));
          return FALSE;
        }

      lm_message_node_set_attribute (
          geoloc, "xml:lang", g_value_get_string (value));
      return TRUE;
    }

  mapping = g_hash_table_lookup (tp_to_xmpp, tp_name);

  if (mapping == NULL)
    {
      DEBUG ("Unknown location key: %s ; skipping", (const gchar *) tp_name);
      /* We don't raise a D-Bus error if the key is unknown to stay backward
       * compatible if new keys are added in a future version of the spec. */
      return TRUE;
    }

  if (G_VALUE_TYPE (value) != mapping->type)
    {
      g_set_error (err, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is supposed to be of type %s but is %s",
          (const char *) tp_name, g_type_name (mapping->type),
          G_VALUE_TYPE_NAME (value));
      return FALSE;
    }

  if (G_VALUE_TYPE (value) == G_TYPE_INT64)
    {
      GTimeVal timeval;

      timeval.tv_sec = CLAMP (g_value_get_int64 (value), 0, G_MAXLONG);
      timeval.tv_usec = 0;
      str = g_time_val_to_iso8601 (&timeval);
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_DOUBLE)
    {
      str = g_strdup_printf ("%.6f", g_value_get_double (value));
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    {
      str = g_value_dup_string (value);
    }
  else
    /* Keys and their type have been checked */
    g_assert_not_reached ();

  lm_message_node_add_child (geoloc, mapping->xmpp_name, str);
  DEBUG ("\t - %s: %s", (gchar *) tp_name, str);
  g_free (str);
  return TRUE;
}

static void
location_set_location (TpSvcConnectionInterfaceLocation *iface,
                       GHashTable *location,
                       DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *msg;
  LmMessageNode *geoloc;
  GHashTableIter iter;
  gpointer key, value;
  GError *err = NULL;

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

  build_mapping_tables ();
  g_hash_table_iter_init (&iter, location);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (!add_to_geoloc_node ((const gchar *) key, (GValue *) value, geoloc,
            &err))
        {
          DEBUG ("%s", err->message);
          dbus_g_method_return_error (context, err);
          g_error_free (err);
          goto out;
        }
    }

  /* XXX: use _ignore_reply */
  if (!_gabble_connection_send (conn, msg, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Failed to send msg" };

      dbus_g_method_return_error (context, &error);
    }
  else
    dbus_g_method_return (context);

out:
  lm_message_unref (msg);
}

void
location_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceLocationClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_location_implement_##x \
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
      TP_IFACE_QUARK_CONNECTION_INTERFACE_LOCATION, FALSE);

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
  const gchar *lang;

  TpHandle contact = tp_handle_lookup (contact_repo, from, NULL, NULL);

  node = lm_message_node_find_child (msg->node, "geoloc");
  if (node == NULL)
    return FALSE;

  DEBUG ("LocationsUpdate for %s:", from);

  lang = lm_message_node_get_attribute (node, "xml:lang");
  if (lang != NULL)
    {
      g_hash_table_insert (location, g_strdup ("language"),
          tp_g_value_slice_new_string (lang));
    }

  build_mapping_tables ();

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *subloc_node = node_iter_data (i);
      GValue *value = NULL;
      gchar *xmpp_name;
      const gchar *str;
      LocationMapping *mapping;

      xmpp_name = subloc_node->name;
      str = lm_message_node_get_value (subloc_node);
      if (str == NULL)
        continue;

      mapping = g_hash_table_lookup (xmpp_to_tp, xmpp_name);
      if (mapping == NULL)
        {
          DEBUG ("Unknown location attribute: %s\n", xmpp_name);
          continue;
        }

      if (mapping->type == G_TYPE_DOUBLE)
        {
          gdouble double_value;
          gchar *end;

          double_value = g_ascii_strtod (str, &end);

          if (end == str)
            continue;

          value = tp_g_value_slice_new_double (double_value);
          DEBUG ("\t - %s: %f", xmpp_name, double_value);
        }
      else if (strcmp (xmpp_name, "timestamp") == 0)
        {
          GTimeVal timeval;
          if (g_time_val_from_iso8601 (str, &timeval))
            {
              value = tp_g_value_slice_new_int64 (timeval.tv_sec);
              DEBUG ("\t - %s: %s", xmpp_name, str);
            }
          else
            {
              DEBUG ("\t - %s: %s: unknown date format", xmpp_name, str);
              continue;
            }
        }
      else if (mapping->type == G_TYPE_STRING)
        {
          value = tp_g_value_slice_new_string (str);
          DEBUG ("\t - %s: %s", xmpp_name, str);
        }
      else
        {
          g_assert_not_reached ();
        }

      g_hash_table_insert (location, g_strdup (mapping->tp_name), value);
    }

  tp_svc_connection_interface_location_emit_location_updated (conn,
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

static void
conn_location_fill_contact_attributes (GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  guint i;
  GabbleConnection *self = GABBLE_CONNECTION(obj);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GHashTable *location;

      location = get_cached_location_or_query (self, handle, NULL);
      if (location != NULL)
        {
          GValue *val = tp_g_value_slice_new_boxed (
              TP_HASH_TYPE_STRING_VARIANT_MAP, location);

          tp_contacts_mixin_set_contact_attribute (attributes_hash,
            handle, TP_IFACE_CONNECTION_INTERFACE_LOCATION"/location",
            val);

          g_hash_table_unref (location);
        }
    }
}

void
conn_location_init (GabbleConnection *conn)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
    TP_IFACE_CONNECTION_INTERFACE_LOCATION,
    conn_location_fill_contact_attributes);
}
