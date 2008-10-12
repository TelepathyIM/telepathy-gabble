
#include "config.h"
#include "conn-location.h"

#include <stdlib.h>

#include "namespaces.h"
#include "pubsub.h"

struct request_location_ctx
{
  DBusGMethodInvocation *call;
  guint pending_replies;
  GHashTable *results;
};

/* XXX: similar to conn-olpc.c's inspect_contact(), except that it assumes
 * that the handle is valid. (Does tp_handle_inspect check validity anyway?)
 * Reduce duplication.
 */
static const gchar *
inspect_contact (TpBaseConnection *base,
                 guint contact)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_CONTACT);

  return tp_handle_inspect (contact_repo, contact);
}

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

static LmHandlerResult
pep_reply_cb (GabbleConnection *conn,
              LmMessage *sent_msg,
              LmMessage *reply_msg,
              GObject *object,
              gpointer user_data)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *geoloc;
  LmMessageNode *lat_node;
  LmMessageNode *lon_node;
  GHashTable *result = NULL;
  struct request_location_ctx *ctx = user_data;
  const gchar *from;
  gdouble lat;
  gdouble lon;
  guint contact;

  ctx->pending_replies--;
  result = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  from = lm_message_node_get_attribute (reply_msg->node, "from");

  if (from == NULL)
    goto FAIL;

  contact = tp_handle_lookup (contact_repo, from, NULL, NULL);
  /* XXX: ref all the handles */
  g_assert (contact);
  geoloc = lm_message_node_find_child (reply_msg->node, "geoloc");

  if (geoloc == NULL)
    goto FAIL;

  lat_node = lm_message_node_find_child (geoloc, "lat");
  lon_node = lm_message_node_find_child (geoloc, "lon");

  if (lat_node == NULL &&
      lon_node == NULL)
    goto FAIL;

  if (lat_node != NULL && lm_message_node_get_double (lat_node, &lat))
    {
      GValue *value = g_slice_new0 (GValue);

      g_value_init (value, G_TYPE_DOUBLE);
      g_value_set_double (value, lat);
      g_hash_table_insert (result, g_strdup ("lat"), value);
    }

  if (lon_node != NULL && lm_message_node_get_double (lon_node, &lon))
    {
      GValue *value = g_slice_new0 (GValue);

      g_value_init (value, G_TYPE_DOUBLE);
      g_value_set_double (value, lon);
      g_hash_table_insert (result, g_strdup ("lon"), value);
    }

  g_hash_table_insert (ctx->results, GINT_TO_POINTER (contact), result);
  goto END;

FAIL:
  g_hash_table_destroy (result);

END:
  if (ctx->pending_replies == 0)
    {
      gabble_svc_location_return_from_request_locations (ctx->call, ctx->results);
      g_hash_table_destroy (ctx->results);
      g_slice_free (struct request_location_ctx, ctx);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
location_request_locations (GabbleSvcLocation *iface,
                        const GArray *contacts,
                        DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  struct request_location_ctx *ctx = NULL;
  guint i;

  if (!validate_contacts (base, context, contacts))
    return;

  ctx = g_slice_new0 (struct request_location_ctx);
  ctx->call = context;
  ctx->pending_replies = contacts->len;
  ctx->results = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_hash_table_destroy);

  for (i = 0; i < contacts->len; i++)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Sending PEP location query failed" };
      guint contact = g_array_index (contacts, guint, i);
      const gchar *jid = inspect_contact (base, contact);

      if (!pubsub_query (conn, jid, NS_GEOLOC, pep_reply_cb, ctx))
        {
          dbus_g_method_return_error (context, &error);
          g_hash_table_destroy (ctx->results);
          g_slice_free (struct request_location_ctx, ctx);
          return;
        }
    }
}

static void
location_set_location (GabbleSvcLocation *iface,
                       GHashTable *location,
                       DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *msg;
  LmMessageNode *geoloc;
  GValue *lat_val;
  GValue *lon_val;

  msg = pubsub_make_publish_msg (NULL, NS_GEOLOC, NS_GEOLOC, "geoloc",
      &geoloc);

  lat_val = g_hash_table_lookup (location, "lat");
  lon_val = g_hash_table_lookup (location, "lon");

  if (lat_val && G_VALUE_TYPE (lat_val) == G_TYPE_DOUBLE)
    {
      gchar *lat_str;

      lat_str = g_strdup_printf ("%.6f", g_value_get_double (lat_val));
      lm_message_node_add_child (geoloc, "lat", lat_str);
      g_free (lat_str);
    }

  if (lon_val && G_VALUE_TYPE (lon_val) == G_TYPE_DOUBLE)
    {
      gchar *lon_str;

      lon_str = g_strdup_printf ("%.6f", g_value_get_double (lon_val));
      lm_message_node_add_child (geoloc, "lon", lon_str);
      g_free (lon_str);
    }

  /* XXX: use _ignore_reply */
  if (!_gabble_connection_send (conn, msg, NULL))
    /* XXX: return error */
    return;

  dbus_g_method_return (context);
}

void
location_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcLocationClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_location_implement_##x (klass, location_##x)
  IMPLEMENT(request_locations);
  IMPLEMENT(set_location);
#undef IMPLEMENT
}

