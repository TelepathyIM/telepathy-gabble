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
#include <telepathy-glib/util.h>

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
  PROP_LIMIT,
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

  gboolean xforms;

  /* owned tp_name (gchar *) => owned xmpp_name (gchar *)
   * This mapping contains the fields that are supported by the server so
   * if a tp_name can be mapped to different xmpp_name, the hash table will
   * map to the one supported. */
  GHashTable *tp_to_xmpp;

  /* Array of owned (gchar *) containing all the boolean search terms
   * supported by this server. */
  GPtrArray *boolean_keys;

  GHashTable *results;

  /* TRUE if the channel is ready to be used (we received the keys supported
   * by the server). */
  gboolean ready;

  TpHandleSet *result_handles;
};

/* Human-readable values of GabbleChannelContactSearchState. */
static const gchar *states[] = {
    "not started",
    "in progress",
    "more available",
    "completed",
    "failed",
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

/* FIXME: it's unclear how "first" and "last" should be mapped.
 * http://xmpp.org/registrar/formtypes.html#jabber:iq:search maps
 * "first" with "First Name" and "last" with "Family Name".
 * But Example 7 of XEP-0055 describes "first" as the "Given Name".
 * Maybe we need to add x-first and x-last?
 */

static const FieldNameMapping field_mappings[] = {
  /* Fields specified for non-Data Forms searches */
  { "first",    "x-n-given" },
  { "last",     "x-n-family" },
  { "nick",     "nickname" },
  { "email",    "email" },
  /* Fields observed in implementations of Data Forms searches */
  /* ejabberd */
  { "user",     "x-telepathy-identifier" },
  { "fn",       "fn" },
  { "middle",   "x-n-additional" },
  { "bday",     "bday" },
  { "ctry",     "x-adr-country" },
  { "locality", "x-adr-locality" },
  { "x-gender", "x-gender" },
  { "orgname",  "x-org-name" },
  { "orgunit",  "x-org-unit" },
  { "given",    "x-n-given" },
  { "family",   "x-n-family" },
  { "nickname", "nickname" },
  /* openfire */
  { "search",   "" }, /* one big search box */
  { "Name",     "fn" },
  { "Email",    "email" },
  /* openfire also includes "Username" which is the user part of the jid */
  { NULL, NULL },
};

#define NUM_UNEXTENDED_FIELDS 4

static GHashTable *xmpp_to_tp = NULL;
static GHashTable *unextended_xmpp_to_tp = NULL;

static void
build_mapping_tables (void)
{
  guint i;

  g_return_if_fail (xmpp_to_tp == NULL);

  xmpp_to_tp = g_hash_table_new (g_str_hash, g_str_equal);
  unextended_xmpp_to_tp = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = 0; i < NUM_UNEXTENDED_FIELDS; i++)
    {
      g_hash_table_insert (xmpp_to_tp, field_mappings[i].xmpp_name,
          field_mappings[i].tp_name);
    }

  tp_g_hash_table_update (unextended_xmpp_to_tp, xmpp_to_tp, NULL, NULL);

  for (; field_mappings[i].xmpp_name != NULL; i++)
    {
      g_hash_table_insert (xmpp_to_tp, field_mappings[i].xmpp_name,
          field_mappings[i].tp_name);
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
  chan->priv->ready = TRUE;
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
parse_unextended_field_response (
    GabbleSearchChannel *self,
    LmMessageNode *query_node,
    GError **error)
{
  GPtrArray *search_keys = g_ptr_array_new ();
  NodeIter i;

  for (i = node_iter (query_node); i; i = node_iter_next (i))
    {
      LmMessageNode *field = node_iter_data (i);
      gchar *tp_name;

      if (!strcmp (field->name, "instructions"))
        {
          DEBUG ("server gave us some instructions: %s",
              lm_message_node_get_value (field));
          continue;
        }

      tp_name = g_hash_table_lookup (unextended_xmpp_to_tp, field->name);

      if (tp_name != NULL)
        {
          g_ptr_array_add (search_keys, tp_name);
          g_hash_table_insert (self->priv->tp_to_xmpp, g_strdup (tp_name),
              g_strdup (field->name));
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

static GPtrArray *
parse_data_form (
    GabbleSearchChannel *self,
    LmMessageNode *x_node,
    GError **error)
{
  GPtrArray *search_keys = g_ptr_array_new ();
  gboolean found_form_type_search = FALSE;
  NodeIter i;

  if (tp_strdiff (lm_message_node_get_attribute (x_node, "type"), "form"))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "server is broken: <x> not type='form'");
      goto fail;
    }

  for (i = node_iter (x_node); i; i = node_iter_next (i))
    {
      LmMessageNode *n = node_iter_data (i);
      const gchar *type = lm_message_node_get_attribute (n, "type");
      const gchar *var = lm_message_node_get_attribute (n, "var");
      gchar *tp_name;

      if (!strcmp (n->name, "title") ||
          !strcmp (n->name, "instructions"))
        {
          DEBUG ("ignoring <%s>: %s", n->name, lm_message_node_get_value (n));
          continue;
        }

      if (strcmp (n->name, "field"))
        {
          /* <reported> and <item> don't make sense here, and nothing else is
           * legal.
           */
          DEBUG ("<%s> is not <title>, <instructions> or <field>", n->name);
          continue;
        }

      if (!strcmp (var, "FORM_TYPE"))
        {
          if (node_iter (n) == NULL ||
              strcmp (lm_message_node_get_value (node_iter_data (
                    node_iter (n))), NS_SEARCH))
            {
              DEBUG ("<x> form does not have FORM_TYPE %s", NS_SEARCH);
              g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "server is broken: form lacking FORM_TYPE %s", NS_SEARCH);
              goto fail;
            }

          found_form_type_search = TRUE;
          continue;
        }

      /* Openfire's search plugin has one search box, called "search", and
       * tickyboxes controlling which fields it searches.
       *
       * So: if the only non-tickybox is a field called "search", expose that
       * field as "", and remember the tickyboxes. When submitting the form,
       * tick them all (XXX: or maybe have a whitelist?)
       */
      if (!tp_strdiff (type, "boolean"))
        {
          g_ptr_array_add (self->priv->boolean_keys, g_strdup (var));
          continue;
        }

      tp_name = g_hash_table_lookup (xmpp_to_tp, var);
      if (tp_name != NULL)
        {
          g_ptr_array_add (search_keys, tp_name);
          g_hash_table_insert (self->priv->tp_to_xmpp, g_strdup (tp_name),
              g_strdup (var));
        }
      else
        {
          DEBUG ("Unknown data form field: %s\n", var);
        }
    }

  return search_keys;

fail:
  g_ptr_array_free (search_keys, TRUE);
  return NULL;
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
    {
      chan->priv->xforms = FALSE;
      search_keys = parse_unextended_field_response (chan, query_node, &e);
    }
  else
    {
      chan->priv->xforms = TRUE;
      search_keys = parse_data_form (chan, x_node, &e);
    }

  if (search_keys == NULL)
    {
      supported_field_discovery_failed (chan, e);
      g_error_free (e);
      return;
    }

  DEBUG ("extracted available fields");
  g_ptr_array_add (search_keys, NULL);
  chan->priv->available_search_keys = (gchar **) g_ptr_array_free (search_keys,
      FALSE);

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

static gchar *
get_error_name (TpError e)
{
  gpointer tp_error_tc = g_type_class_ref (TP_TYPE_ERROR);
  GEnumClass *tp_error_ec = G_ENUM_CLASS (tp_error_tc);
  GEnumValue *e_value = g_enum_get_value (tp_error_ec, e);
  const gchar *error_suffix = e_value->value_nick;
  gchar *error_name = g_strdup_printf ("%s.%s", TP_ERROR_PREFIX, error_suffix);

  g_type_class_unref (tp_error_tc);
  return error_name;
}

/**
 * change_search_state:
 * @chan: a search channel
 * @state: the new state for the channel
 * @reason: an error in the TP_ERRORS domain if the search has failed; NULL
 *          otherwise.
 */
static void
change_search_state (GabbleSearchChannel *chan,
                     GabbleChannelContactSearchState state,
                     const GError *reason)
{
  GabbleSearchChannelPrivate *priv = chan->priv;
  GHashTable *details = g_hash_table_new (g_str_hash, g_str_equal);
  gchar *error_name = NULL;
  GValue v = { 0, };

  switch (state)
    {
    case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED:
    /* Gabble shouldn't ever get into state More_Available */
    case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_MORE_AVAILABLE:
      g_assert_not_reached ();
      return;
    case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_IN_PROGRESS:
      g_assert (priv->state == GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED);
      break;
    case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_COMPLETED:
    case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_FAILED:
      g_assert (priv->state == GABBLE_CHANNEL_CONTACT_SEARCH_STATE_IN_PROGRESS);
      break;
    }

  if (state == GABBLE_CHANNEL_CONTACT_SEARCH_STATE_FAILED)
    {
      g_assert (reason != NULL);
      error_name = get_error_name (reason->code);

      g_value_init (&v, G_TYPE_STRING);
      g_value_set_static_string (&v, reason->message);
      g_hash_table_insert (details, "debug-message", &v);
    }
  else
    {
      g_assert (reason == NULL);
    }

  DEBUG ("moving from %s to %s for reason '%s'", states[priv->state],
      states[state], error_name == NULL ? "" : error_name);
  priv->state = state;

  gabble_svc_channel_type_contact_search_emit_search_state_changed (
      chan, state, (error_name == NULL ? "" : error_name), details);

  g_free (error_name);
  g_hash_table_unref (details);
}

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
add_search_result (GabbleSearchChannel *chan,
    TpHandleRepoIface *handles,
    GHashTable *info_map)
{
  GPtrArray *info = g_ptr_array_new ();
  gchar *jid, *first = NULL, *last = NULL;
  TpHandle h = 0;
  GError *e = NULL;
  gpointer key, value;
  GHashTableIter iter;

  jid = ht_lookup_and_remove (info_map, "jid");
  if (jid == NULL)
    {
      DEBUG ("no jid; giving up");
      return;
    }

  h = tp_handle_ensure (handles, jid, NULL, &e);

  if (h == 0)
    {
      DEBUG ("invalid jid: %s", e->message);
      g_error_free (e);
      return;
    }

  tp_handle_set_add (chan->priv->result_handles, h);
  tp_handle_unref (handles, h);

  {
    gchar *components[] = { jid, NULL };
    g_ptr_array_add (info, make_field ("x-telepathy-identifier", components));
  }

  g_hash_table_iter_init (&iter, info_map);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *tp_name;
      gchar *components[] = { value, NULL };

      tp_name = g_hash_table_lookup (xmpp_to_tp, key);
      if (tp_name == NULL)
        {
          DEBUG ("<item> contained field we don't understand (%s); ignoring it:"
              , (const gchar *) key);
          continue;
        }

      if (value == NULL)
        {
          DEBUG ("field %s (%s) doesn't have a value; ignoring it",
              (const gchar *) key, tp_name);
          continue;
        }

      DEBUG ("found field %s (%s): %s", (const gchar *) key, tp_name,
          (const gchar *) value);

      g_ptr_array_add (info, make_field (tp_name, components));

      if (!tp_strdiff (key, "last") ||
          !tp_strdiff (key, "family"))
        last = value;
      else if (!tp_strdiff (key, "first") ||
        !tp_strdiff (key, "given"))
        first = value;
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

  g_hash_table_insert (chan->priv->results, GUINT_TO_POINTER (h), info);
}

static void
parse_result_item (GabbleSearchChannel *chan,
                   TpHandleRepoIface *handles,
                   LmMessageNode *item)
{
  const gchar *jid = lm_message_node_get_attribute (item, "jid");
  GHashTable *info;
  NodeIter i;

  if (jid == NULL)
    {
      DEBUG ("<item> didn't have a jid attribute; skipping");
      return;
    }

  info = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (info, "jid", (gchar *) jid);

  for (i = node_iter (item); i; i = node_iter_next (i))
    {
      LmMessageNode *n = node_iter_data (i);
      gchar *value = (gchar *) lm_message_node_get_value (n);

      g_hash_table_insert (info, n->name, value);
    }

  add_search_result (chan, handles, info);
  g_hash_table_destroy (info);
}

static void
parse_extended_result_item (GabbleSearchChannel *chan,
    TpHandleRepoIface *handles,
    LmMessageNode *item)
{
  GHashTable *info;
  NodeIter i;

  info = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = node_iter (item); i; i = node_iter_next (i))
    {
      LmMessageNode *field = node_iter_data (i);
      LmMessageNode *value_node;
      const gchar *var, *value;

      if (tp_strdiff (field->name, "field"))
        {
          DEBUG ("found <%s/> in <item/> rather than <field/>, skipping",
              field->name);
          continue;
        }

      var = lm_message_node_get_attribute (field, "var");
      if (var == NULL)
        {
          DEBUG ("Ignore <field/> without 'var' attribut");
          continue;
        }

      value_node = lm_message_node_get_child (field, "value");
      if (value_node == NULL)
        {
          DEBUG ("Ignore <field/> without <value/> child");
          continue;
        }

      value = lm_message_node_get_value (value_node);

      g_hash_table_insert (info, (gchar *) var, (gchar *) value);
    }

  if (g_hash_table_lookup (info, "jid") == NULL)
    {
      DEBUG ("<item> didn't have a jid attribute; skipping");
    }
  else
    {
      add_search_result (chan, handles, info);
    }

  g_hash_table_destroy (info);
}

static gboolean
parse_unextended_search_results (GabbleSearchChannel *chan,
    LmMessageNode *query_node,
    GError *error)
{
  TpHandleRepoIface *handles = tp_base_connection_get_handles (
      (TpBaseConnection *) chan->base.conn, TP_HANDLE_TYPE_CONTACT);
  NodeIter i;

  for (i = node_iter (query_node); i; i = node_iter_next (i))
    {
      LmMessageNode *item = node_iter_data (i);

      if (!strcmp (item->name, "item"))
        parse_result_item (chan, handles, item);
      else
        DEBUG ("found <%s/> in <query/> rather than <item/>, skipping",
            item->name);
    }

  return TRUE;
}

static gboolean
parse_extended_search_results (GabbleSearchChannel *chan,
    LmMessageNode *query_node,
    GError *error)
{
  TpHandleRepoIface *handles = tp_base_connection_get_handles (
      (TpBaseConnection *) chan->base.conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *x;
  NodeIter i;

  x = lm_message_node_get_child_with_namespace (query_node, "x", NS_X_DATA);
  if (x == NULL)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "reply doens't contain a <x> node");
      return FALSE;
    }

  for (i = node_iter (x); i; i = node_iter_next (i))
    {
      LmMessageNode *item = node_iter_data (i);

      if (!tp_strdiff (item->name, "item"))
        parse_extended_result_item (chan, handles, item);
      else if (!tp_strdiff (item->name, "reported"))
        /* Ignore <reported> node */
        continue;
      else if (!tp_strdiff (item->name, "title"))
        DEBUG ("title: %s", lm_message_node_get_value (item));
      else
        DEBUG ("found <%s/> in <x/> rather than <item/>, <title/> and "
            "<reported/>, skipping", item->name);
    }

  return TRUE;
}

static gboolean
parse_search_results (GabbleSearchChannel *chan,
    LmMessageNode *query_node,
    GError *error)
{
  if (chan->priv->xforms)
    return parse_extended_search_results (chan, query_node, error);
  else
    return parse_unextended_search_results (chan, query_node, error);
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

  if (chan->priv->state != GABBLE_CHANNEL_CONTACT_SEARCH_STATE_IN_PROGRESS)
    {
      DEBUG ("state is %s, not in progress; ignoring results",
          states[chan->priv->state]);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  query_node = lm_message_node_get_child_with_namespace (reply_msg->node,
      "query", NS_SEARCH);

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      err = gabble_message_get_xmpp_error (reply_msg);

      if (err == NULL)
        {
          err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "%s gave us an error we don't understand", chan->priv->server);
        }
      else
        {
          err->domain = TP_ERRORS;

          switch (err->code)
            {
            case XMPP_ERROR_NOT_AUTHORIZED:
            case XMPP_ERROR_NOT_ACCEPTABLE:
            case XMPP_ERROR_FORBIDDEN:
            case XMPP_ERROR_NOT_ALLOWED:
            case XMPP_ERROR_REGISTRATION_REQUIRED:
            case XMPP_ERROR_SUBSCRIPTION_REQUIRED:
              err->code = TP_ERROR_PERMISSION_DENIED;
              break;
            /* FIXME: other error mappings go here. Maybe we need some kind of
             *        generic GabbleXmppError -> TpError mapping.
             */
            default:
              err->code = TP_ERROR_NOT_AVAILABLE;
            }
        }
    }
  else if (NULL == query_node)
    {
      err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s is broken: its iq reply didn't contain a <query/>",
          chan->priv->server);
    }

  if (err != NULL)
    goto fail;

  if (!parse_search_results (chan, query_node, err))
    goto fail;

  /* fire SearchStateChanged */
  gabble_svc_channel_type_contact_search_emit_search_result_received (chan,
      chan->priv->results);

  change_search_state (chan, GABBLE_CHANNEL_CONTACT_SEARCH_STATE_COMPLETED,
      NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;

fail:
  DEBUG ("Searching failed: %s", err->message);

  change_search_state (chan, GABBLE_CHANNEL_CONTACT_SEARCH_STATE_FAILED, err);
  g_error_free (err);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
validate_terms (GabbleSearchChannel *chan,
                GHashTable *terms,
                GError **error)
{
  const gchar * const *asks =
      (const gchar * const *) chan->priv->available_search_keys;
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, terms);

  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      gchar *field = key;

      if (!tp_strv_contains (asks, field))
        {
          DEBUG ("%s is not in AvailableSearchKeys", field);
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "%s is not in AvailableSearchKeys", field);
          return FALSE;
        }
    }

  return TRUE;
}

static void
build_unextended_query (
    GabbleSearchChannel *self,
    LmMessageNode *query,
    GHashTable *terms)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, terms);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      gchar *xmpp_field = g_hash_table_lookup (self->priv->tp_to_xmpp, key);

      g_assert (xmpp_field != NULL);

      lm_message_node_add_child (query, xmpp_field, value);
    }
}

static void
build_extended_query (GabbleSearchChannel *self,
    LmMessageNode *query,
    GHashTable *terms)
{
  LmMessageNode *x, *field;
  GHashTableIter iter;
  gpointer key, value;

  x = lm_message_node_add_child (query, "x", "");
  lm_message_node_set_attributes (x,
      "type", "submit",
      "xmlns", NS_X_DATA,
      NULL);

  /* add FORM_TYPE */
  field = lm_message_node_add_child (x, "field", "");
  lm_message_node_set_attributes (field,
      "type", "hidden",
      "var", "FORM_TYPE",
      NULL);
  lm_message_node_add_child (field, "value", NS_SEARCH);

  /* Add search terms */
  g_hash_table_iter_init (&iter, terms);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *tp_name = key;
      gchar *xmpp_field = g_hash_table_lookup (self->priv->tp_to_xmpp, tp_name);

      g_assert (xmpp_field != NULL);

      field = lm_message_node_add_child (x, "field", "");
      lm_message_node_set_attribute (field, "var", xmpp_field);
      lm_message_node_add_child (field, "value", value);

      if (!tp_strdiff (tp_name, ""))
        {
          /* Open fire search. Tick all the boolean fields */
          guint i;

          for (i = 0; i < self->priv->boolean_keys->len; i++)
            {
              xmpp_field = g_ptr_array_index (self->priv->boolean_keys, i);

              field = lm_message_node_add_child (x, "field", "");
              lm_message_node_set_attributes (field,
                  "var", xmpp_field,
                  "type", "boolean",
                  NULL);
              lm_message_node_add_child (field, "value", "1");
            }
        }
    }
}

static gboolean
do_search (GabbleSearchChannel *chan,
           GHashTable *terms,
           GError **error)
{
  LmMessage *msg;
  LmMessageNode *query;
  gboolean ret;

  DEBUG ("called");

  if (!validate_terms (chan, terms, error))
    return FALSE;

  msg = lm_message_new_with_sub_type (chan->priv->server, LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_SET);
  query = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (query, "xmlns", NS_SEARCH);

  if (chan->priv->xforms)
    {
      build_extended_query (chan, query, terms);
    }
  else
    {
      build_unextended_query (chan, query, terms);
    }

  DEBUG ("Sending search");

  if (_gabble_connection_send_with_reply (chan->base.conn, msg,
          search_reply_cb, (GObject *) chan, NULL, error))
    {
      ret = TRUE;
      change_search_state (chan,
          GABBLE_CHANNEL_CONTACT_SEARCH_STATE_IN_PROGRESS, NULL);
    }
  else
    {
      ret = FALSE;
    }

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

static void
free_info (GPtrArray *info)
{
  g_boxed_free (GABBLE_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, info);
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

  chan->priv->tp_to_xmpp = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_free);

  chan->priv->boolean_keys = g_ptr_array_new ();

  chan->priv->results = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) free_info);

  request_search_fields (chan);

  return obj;
}

static void
gabble_search_channel_finalize (GObject *obj)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (obj);
  GabbleSearchChannelPrivate *priv = chan->priv;
  guint i;

  ensure_closed (chan);

  g_free (priv->server);

  tp_handle_set_destroy (priv->result_handles);
  g_hash_table_destroy (chan->priv->tp_to_xmpp);

  g_free (chan->priv->available_search_keys);

  for (i = 0; i < priv->boolean_keys->len; i++)
    {
      g_free (g_ptr_array_index (priv->boolean_keys, i));
    }
  g_ptr_array_free (priv->boolean_keys, TRUE);

  g_hash_table_destroy (chan->priv->results);

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
      case PROP_LIMIT:
        g_value_set_uint (value, 0);
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
                GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH, "Limit",
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

  switch (property_id)
    {
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
      { "Limit", "limit", NULL },
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
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_FAILED,
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
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

  param_spec = g_param_spec_uint ("limit", "Result limit",
      "Always 0 for unlimited in Gabble",
      0, 0, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LIMIT,
      param_spec);

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
 * gabble_search_channel_close_async
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_search_channel_close_async (TpSvcChannel *iface,
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

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_search_channel_##x##suffix)
  IMPLEMENT(close,_async);
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
gabble_search_channel_stop (GabbleSvcChannelTypeContactSearch *self,
                            DBusGMethodInvocation *context)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (self);
  GabbleSearchChannelPrivate *priv = chan->priv;

  switch (priv->state)
    {
      case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_IN_PROGRESS:
        {
          GError e = { TP_ERRORS, TP_ERROR_CANCELLED, "Stop() called" };

          change_search_state (chan,
              GABBLE_CHANNEL_CONTACT_SEARCH_STATE_FAILED, &e);
          /* Deliberately falling through to return from the method: */
        }
      case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_COMPLETED:
      case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_FAILED:
        gabble_svc_channel_type_contact_search_return_from_stop (context);
        break;
      case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED:
        {
          GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Search() hasn't been called yet" };

          dbus_g_method_return_error (context, &e);
          break;
        }
      case GABBLE_CHANNEL_CONTACT_SEARCH_STATE_MORE_AVAILABLE:
        g_assert_not_reached ();
    }
}

void
gabble_search_channel_close (GabbleSearchChannel *self)
{
  ensure_closed (self);
}

gboolean
gabble_search_channel_is_ready (GabbleSearchChannel *self)
{
  return self->priv->ready;
}

static void
contact_search_iface_init (gpointer g_iface,
                           gpointer iface_data)
{
  GabbleSvcChannelTypeContactSearchClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_channel_type_contact_search_implement_##x (\
    klass, gabble_search_channel_##x)
  IMPLEMENT(search);
  IMPLEMENT(stop);
#undef IMPLEMENT
}
