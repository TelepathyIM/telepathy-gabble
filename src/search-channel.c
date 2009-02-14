/*
 * search-channel.c - implementation of ContactSearch channels
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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
#include "search-channel.h"

#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_SEARCH
#include "base-channel.h"
#include "debug.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

#include "extensions/extensions.h"

static const gchar *gabble_search_channel_interfaces[] = {
    NULL
};

/* properties */
enum
{
  PROP_CHANNEL_PROPERTIES = 1,
  PROP_SEARCH_STATE,
  PROP_AVAILABLE_SEARCH_KEYS,
  PROP_SERVER,
  LAST_PROPERTY
};

/* signal enum */
enum
{
    READY_OR_NOT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
struct _GabbleSearchChannelPrivate
{
  GabbleChannelContactSearchState state;
  gchar **available_search_keys;
  gchar *server;

  TpHandleSet *result_handles;
};

/* Human-readable values of GabbleChannelContactSearchState. */
static const gchar *states[] = {
    "not started",
    "in progress",
    "completed"
};

static void channel_iface_init (gpointer, gpointer);
static void contact_search_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleSearchChannel, gabble_search_channel,
    GABBLE_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_CONTACT_SEARCH,
        contact_search_iface_init);
    )

/* Mapping between XEP 0055/misc data forms fields seen in the wild and
 * vCard/Telepathy names.
 */

typedef struct {
    gchar *xmpp_name;
    gchar *tp_name;
} FieldNameMapping;

static const FieldNameMapping field_mappings[] = {
  /* Fields specified for non-Data Forms searches */
  { "first",    "x-n-given" },
  { "last",     "x-n-family" },
  { "nick",     "nickname" },
  { "email",    "email" },
  /* Fields observed in implementations of Data Forms searches */
  { NULL, NULL },
};

#define NUM_UNEXTENDED_FIELDS 4

static GHashTable *xmpp_to_tp = NULL;
static GHashTable *unextended_xmpp_to_tp = NULL;
static GHashTable *tp_to_xmpp = NULL;

static void
build_mapping_tables (void)
{
  guint i;

  g_return_if_fail (xmpp_to_tp == NULL);
  g_return_if_fail (tp_to_xmpp == NULL);

  xmpp_to_tp = g_hash_table_new (g_str_hash, g_str_equal);
  unextended_xmpp_to_tp = g_hash_table_new (g_str_hash, g_str_equal);
  tp_to_xmpp = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = 0; i < NUM_UNEXTENDED_FIELDS; i++)
    {
      g_hash_table_insert (xmpp_to_tp, field_mappings[i].xmpp_name,
          field_mappings[i].tp_name);
      g_hash_table_insert (tp_to_xmpp, field_mappings[i].tp_name,
          field_mappings[i].xmpp_name);
    }

  tp_g_hash_table_update (unextended_xmpp_to_tp, xmpp_to_tp, NULL, NULL);

  for (; field_mappings[i].xmpp_name != NULL; i++)
    {
      g_hash_table_insert (xmpp_to_tp, field_mappings[i].xmpp_name,
          field_mappings[i].tp_name);
      g_hash_table_insert (tp_to_xmpp, field_mappings[i].tp_name,
          field_mappings[i].xmpp_name);
    }
}

/* Misc */

static void
ensure_closed (GabbleSearchChannel *chan)
{
  if (chan->base.closed)
    {
      DEBUG ("Already closed, doing nothing");
    }
  else
    {
      DEBUG ("Emitting Closed");
      chan->base.closed = TRUE;
      tp_svc_channel_emit_closed (chan);
    }
}

/* Supported field */

static void
supported_fields_discovered (GabbleSearchChannel *chan)
{
  DEBUG ("called");

  g_assert (chan->base.closed);

  chan->base.closed = FALSE;
  gabble_base_channel_register ((GabbleBaseChannel *) chan);
  g_signal_emit (chan, signals[READY_OR_NOT], 0, 0, 0, NULL);
}

static void
supported_field_discovery_failed (GabbleSearchChannel *chan,
                                  const GError *error)
{
  DEBUG ("called: %s, %u, %s", g_quark_to_string (error->domain), error->code,
      error->message);

  g_assert (chan->base.closed);

  g_signal_emit (chan, signals[READY_OR_NOT], 0, error->domain, error->code,
      error->message);
}

static GPtrArray *
parse_unextended_field_response (LmMessageNode *query_node,
                                 GError **error)
{
  GPtrArray *search_keys = g_ptr_array_new ();
  LmMessageNode *field;

  for (field = query_node->children; field != NULL; field = field->next)
    {
      gchar *tp_name;

      if (!strcmp (field->name, "instructions"))
        {
          DEBUG ("server gave us some instructions: %s",
              lm_message_node_get_value (field));
          continue;
        }

      tp_name = g_hash_table_lookup (xmpp_to_tp, field->name);

      if (tp_name != NULL)
        {
          g_ptr_array_add (search_keys, tp_name);
        }
      else
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "server is broken: %s is not a field defined in XEP 0055",
              field->name);
          g_ptr_array_free (search_keys, TRUE);
          return NULL;
        }
    }

  return search_keys;
}

static void
parse_search_field_response (GabbleSearchChannel *chan,
                             LmMessageNode *query_node)
{
  LmMessageNode *x_node;
  GPtrArray *search_keys = NULL;
  GError *e = NULL;

  x_node = lm_message_node_get_child_with_namespace (query_node, "x",
      NS_X_DATA);

  if (x_node == NULL)
    search_keys = parse_unextended_field_response (query_node, &e);
  else
    e = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "server uses data forms, which are not yet implemented in Gabble");

  if (search_keys == NULL)
    {
      supported_field_discovery_failed (chan, e);
      g_error_free (e);
      return;
    }

  DEBUG ("extracted available fields");
  g_ptr_array_add (search_keys, NULL);
  chan->priv->available_search_keys = (gchar **) g_ptr_array_free (search_keys, FALSE);

  supported_fields_discovered (chan);
}

static LmHandlerResult
query_reply_cb (GabbleConnection *conn,
                LmMessage *sent_msg,
                LmMessage *reply_msg,
                GObject *object,
                gpointer user_data)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (object);
  LmMessageNode *query_node;
  GError *err = NULL;

  query_node = lm_message_node_get_child_with_namespace (reply_msg->node,
      "query", NS_SEARCH);

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      err = gabble_message_get_xmpp_error (reply_msg);

      if (err == NULL)
        err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "%s gave us an error we don't understand", chan->priv->server);
    }
  else if (NULL == query_node)
    {
      err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s is broken: it replied to our <query> with an empty IQ",
          chan->priv->server);
    }

  if (err != NULL)
    {
      supported_field_discovery_failed (chan, err);
      g_error_free (err);
    }
  else
    {
      parse_search_field_response (chan, query_node);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
request_search_fields (GabbleSearchChannel *chan)
{
  LmMessage *msg;
  LmMessageNode *lm_node;
  GError *error = NULL;

  msg = lm_message_new_with_sub_type (chan->priv->server, LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_GET);
  lm_node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (lm_node, "xmlns", NS_SEARCH);

  if (! _gabble_connection_send_with_reply (chan->base.conn, msg,
            query_reply_cb, (GObject *) chan, NULL, &error))
    {
      supported_field_discovery_failed (chan, error);
      g_error_free (error);
    }

  lm_message_unref (msg);
}

/* Search implementation */

/**
 * make_field:
 * @field_name: name of a vCard field; must be a static string.
 * @values: strv of values for the field.
 *
 * Returns: the Contact_Info_Field (field_name, [], values).
 */
static GValueArray *
make_field (const gchar *field_name,
            gchar **values)
{
  GValueArray *field = g_value_array_new (3);
  GValue *value;
  static const gchar **empty = { NULL };

  g_value_array_append (field, NULL);
  value = g_value_array_get_nth (field, 0);
  g_value_init (value, G_TYPE_STRING);
  g_value_set_static_string (value, field_name);

  g_value_array_append (field, NULL);
  value = g_value_array_get_nth (field, 1);
  g_value_init (value, G_TYPE_STRV);
  g_value_set_static_boxed (value, empty);

  g_value_array_append (field, NULL);
  value = g_value_array_get_nth (field, 2);
  g_value_init (value, G_TYPE_STRV);
  g_value_set_boxed (value, values);

  return field;
}

static gchar *
ht_lookup_and_remove (GHashTable *info_map,
                      gchar *field_name)
{
  gchar *ret = g_hash_table_lookup (info_map, field_name);

  g_hash_table_remove (info_map, field_name);

  return ret;
}

static void
emit_search_result (GabbleSearchChannel *chan,
                    TpHandleRepoIface *handles,
                    GHashTable *info_map)
{
  GPtrArray *info = g_ptr_array_new ();
  gchar *jid, *first, *last, *nick, *email;
  TpHandle h;
  GError *e = NULL;

  jid = ht_lookup_and_remove (info_map, "jid");
  last = ht_lookup_and_remove (info_map, "last");
  first = ht_lookup_and_remove (info_map, "first");
  nick = ht_lookup_and_remove (info_map, "nick");
  email = ht_lookup_and_remove (info_map, "email");

  if (jid == NULL)
    {
      DEBUG ("no jid; giving up");
      goto out;
    }

  h = tp_handle_ensure (handles, jid, NULL, &e);

  if (h == 0)
    {
      DEBUG ("invalid jid: %s", e->message);
      g_error_free (e);
      goto out;
    }

  tp_handle_set_add (chan->priv->result_handles, h);

  {
    gchar *components[] = { jid, NULL };
    g_ptr_array_add (info, make_field ("x-telepathy-identifier", components));
  }

  /* Build 'n' field: Family Name, Given Name, Additional Names, Honorific
   * Prefixes, and Honorific Suffixes.
   */
  if (last != NULL || first != NULL)
    {
      gchar *components[] = {
          (last == NULL ? "" : last),
          (first == NULL ? "" : first),
          "",
          "",
          "",
          NULL
      };
      g_ptr_array_add (info, make_field ("n", components));
    }

  /* Build 'nickname' field. */
  if (nick != NULL)
    {
      gchar *components[] = { nick, NULL };
      g_ptr_array_add (info, make_field ("nickname", components));
    }

  /* Build 'email' field */
  if (email != NULL)
    {
      gchar *components[] = { email, NULL };
      g_ptr_array_add (info, make_field ("email", components));
    }

  if (g_hash_table_size (info_map) > 0)
    DEBUG ("<item> contained fields we don't understand; ignoring them");

  gabble_svc_channel_type_contact_search_emit_search_result_received (chan, h, info);

out:
  {
    GValue v = { 0, };

    g_value_init (&v, GABBLE_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST);
    g_value_take_boxed (&v, info);
    g_value_unset (&v);

    if (h != 0)
      tp_handle_unref (handles, h);
  }
}

static void
parse_result_item (GabbleSearchChannel *chan,
                   TpHandleRepoIface *handles,
                   LmMessageNode *item)
{
  const gchar *jid = lm_message_node_get_attribute (item, "jid");
  GHashTable *info;
  LmMessageNode *n;

  if (jid == NULL)
    {
      DEBUG ("<item> didn't have a jid attribute; skipping");
      return;
    }

  info = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (info, "jid", (gchar *) jid);

  for (n = item->children; n != NULL; n = n->next)
    {
      gchar *value = (gchar *) lm_message_node_get_value (n);

      g_hash_table_insert (info, n->name, value);
    }

  emit_search_result (chan, handles, info);
}

static void
parse_search_results (GabbleSearchChannel *chan,
                      LmMessageNode *query_node)
{
  TpHandleRepoIface *handles = tp_base_connection_get_handles (
      (TpBaseConnection *) chan->base.conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *item;

  for (item = query_node->children; item != NULL; item = item->next)
    {
      if (!strcmp (item->name, "item"))
        parse_result_item (chan, handles, item);
      else
        DEBUG ("found <%s/> in <query/> rather than <item/>, skipping",
            item->name);
    }
}

static LmHandlerResult
search_reply_cb (GabbleConnection *conn,
                 LmMessage *sent_msg,
                 LmMessage *reply_msg,
                 GObject *object,
                 gpointer user_data)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (object);
  LmMessageNode *query_node;
  GError *err = NULL;

  DEBUG ("called");

  query_node = lm_message_node_get_child_with_namespace (reply_msg->node,
      "query", NS_SEARCH);

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      err = gabble_message_get_xmpp_error (reply_msg);

      if (err == NULL)
        err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "%s gave us an error we don't understand", chan->priv->server);
    }
  else if (NULL == query_node)
    {
      err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s is broken: it replied to our <query> with an empty IQ",
          chan->priv->server);
    }

  if (err != NULL)
    {
      DEBUG ("Searching failed: %s", err->message);
      g_error_free (err);
    }
  else
    {
      parse_search_results (chan, query_node);
    }

  g_object_set (chan,
      "search-state", GABBLE_CHANNEL_CONTACT_SEARCH_STATE_COMPLETED,
      NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
do_search (GabbleSearchChannel *chan,
           GHashTable *terms,
           GError **error)
{
  const gchar * const *asks =
      (const gchar * const *) chan->priv->available_search_keys;
  LmMessage *msg;
  LmMessageNode *query;
  GHashTableIter iter;
  gpointer key, value;
  gboolean ret;

  DEBUG ("called");

  msg = lm_message_new_with_sub_type (chan->priv->server, LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_GET);
  query = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (query, "xmlns", NS_SEARCH);

  g_hash_table_iter_init (&iter, terms);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      gchar *field = key;
      gchar *xmpp_field;

      if (!tp_strv_contains (asks, field))
        {
          DEBUG ("%s is not in AvailableSearchKeys", field);
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "%s is not in AvailableSearchKeys", field);
          ret = FALSE;
          goto out;
        }

      xmpp_field = g_hash_table_lookup (tp_to_xmpp, field);
      g_assert (xmpp_field != NULL);

      lm_message_node_add_child (query, xmpp_field, value);
    }

  DEBUG ("Sending search");

  if (_gabble_connection_send_with_reply (chan->base.conn, msg,
          search_reply_cb, (GObject *) chan, NULL, error))
    {
      ret = TRUE;
      g_object_set (chan,
          "search-state", GABBLE_CHANNEL_CONTACT_SEARCH_STATE_IN_PROGRESS,
          NULL);
    }
  else
    {
      ret = FALSE;
    }

out:
  lm_message_unref (msg);
  return ret;
}

/* GObject implementation */

static void
gabble_search_channel_init (GabbleSearchChannel *self)
{
  GabbleSearchChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_SEARCH_CHANNEL, GabbleSearchChannelPrivate);

  self->priv = priv;
}

static GObject *
gabble_search_channel_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleSearchChannel *chan;
  GabbleBaseChannel *base;
  TpBaseConnection *conn;
  gchar *escaped;

  obj = G_OBJECT_CLASS (gabble_search_channel_parent_class)->constructor (
      type, n_props, props);
  chan = GABBLE_SEARCH_CHANNEL (obj);
  base = GABBLE_BASE_CHANNEL (obj);
  conn = (TpBaseConnection *) base->conn;

  base->channel_type = GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH;
  base->interfaces = gabble_search_channel_interfaces;
  base->target_type = TP_HANDLE_TYPE_NONE;
  base->target = 0;
  base->initiator = conn->self_handle;

  escaped = tp_escape_as_identifier (chan->priv->server);
  base->object_path = g_strdup_printf ("%s/SearchChannel_%s_%p",
      conn->object_path, escaped, obj);
  g_free (escaped);

  chan->priv->result_handles = tp_handle_set_new (
      tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT));

  /* The channel only "opens" when it's found out that the server really does
   * speak XEP 0055 and knows which fields are supported.
   */
  base->closed = TRUE;

  request_search_fields (chan);

  return obj;
}

static void
gabble_search_channel_finalize (GObject *obj)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (obj);

  ensure_closed (chan);

  g_free (chan->priv->server);

  tp_handle_set_destroy (chan->priv->result_handles);

  if (G_OBJECT_CLASS (gabble_search_channel_parent_class)->finalize)
    G_OBJECT_CLASS (gabble_search_channel_parent_class)->finalize (obj);
}

static void
gabble_search_channel_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (object);

  switch (property_id)
    {
      case PROP_SEARCH_STATE:
        g_value_set_uint (value, chan->priv->state);
        break;
      case PROP_AVAILABLE_SEARCH_KEYS:
        g_value_set_boxed (value, chan->priv->available_search_keys);
        break;
      case PROP_SERVER:
        g_value_set_string (value, chan->priv->server);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_take_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                TP_IFACE_CHANNEL, "Requested",
                TP_IFACE_CHANNEL, "Interfaces",
                GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH, "AvailableSearchKeys",
                GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH, "Server",
                NULL));
      break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_search_channel_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (object);
  GabbleSearchChannelPrivate *priv = chan->priv;

  switch (property_id)
    {
      case PROP_SEARCH_STATE:
        {
          GabbleChannelContactSearchState state = g_value_get_uint (value);

          g_return_if_fail (state < NUM_GABBLE_CHANNEL_CONTACT_SEARCH_STATES);
          /* The search state can only go forward because it can't find
           * reverse
           */
          g_return_if_fail (state > priv->state);

          DEBUG ("moving from %s to %s", states[priv->state], states[state]);
          priv->state = state;
          gabble_svc_channel_type_contact_search_emit_search_state_changed (
              chan, state);
          break;
        }
      case PROP_SERVER:
        chan->priv->server = g_value_dup_string (value);
        g_assert (chan->priv->server != NULL);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_search_channel_class_init (GabbleSearchChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl search_channel_props[] = {
      { "SearchState", "search-state", NULL },
      { "AvailableSearchKeys", "available-search-keys", NULL },
      { "Server", "server", NULL },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (GabbleSearchChannelPrivate));

  object_class->constructor = gabble_search_channel_constructor;
  object_class->finalize = gabble_search_channel_finalize;

  object_class->get_property = gabble_search_channel_get_property;
  object_class->set_property = gabble_search_channel_set_property;

  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_uint ("search-state", "Search state",
      "The current state of the search represented by this channel",
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED,
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_COMPLETED,
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SEARCH_STATE,
      param_spec);

  param_spec = g_param_spec_boxed ("available-search-keys",
      "Available search keys",
      "The set of search keys supported by this channel",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AVAILABLE_SEARCH_KEYS,
      param_spec);

  param_spec = g_param_spec_string ("server", "Search server",
      "The user directory server used by this search",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SERVER, param_spec);

  /* Emitted when we get a reply from the server about which search keys it
   * supports.  Its three arguments are the components of a GError.  If the
   * server gave us a set of search keys, and they were sane, all components
   * will be 0 or %NULL, indicating that this channel can be announced and
   * used; if the server doesn't actually speak XEP 0055 or is full of bees,
   * they'll be an error in either the GABBLE_XMPP_ERROR or the TP_ERRORS
   * domain.
   */
  signals[READY_OR_NOT] =
    g_signal_new ("ready-or-not",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  tp_dbus_properties_mixin_implement_interface (object_class,
      GABBLE_IFACE_QUARK_CHANNEL_TYPE_CONTACT_SEARCH,
      tp_dbus_properties_mixin_getter_gobject_properties, NULL,
      search_channel_props);

  build_mapping_tables ();
}

/**
 * gabble_search_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_search_channel_close (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (iface);

  ensure_closed (chan);

  tp_svc_channel_return_from_close (context);
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_search_channel_##x)
  IMPLEMENT(close);
#undef IMPLEMENT
}

static void
gabble_search_channel_search (GabbleSvcChannelTypeContactSearch *self,
                              GHashTable *terms,
                              DBusGMethodInvocation *context)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (self);
  GabbleSearchChannelPrivate *priv = chan->priv;
  GError *error = NULL;

  if (priv->state != GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "SearchState is %s", states[priv->state]);
      goto err;
    }

  if (do_search (chan, terms, &error))
    {
      gabble_svc_channel_type_contact_search_return_from_search (context);
      return;
    }

err:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

static void
contact_search_iface_init (gpointer g_iface,
                           gpointer iface_data)
{
  GabbleSvcChannelTypeContactSearchClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_channel_type_contact_search_implement_##x (\
    klass, gabble_search_channel_##x)
  IMPLEMENT(search);
#undef IMPLEMENT
}
