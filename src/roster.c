/*
 * roster.c - Source for Gabble roster helper
 *
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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <string.h>

#include "telepathy-interfaces.h"
#include "tp-channel-factory-iface.h"

#define DEBUG_FLAG GABBLE_DEBUG_ROSTER

#include "debug.h"
#include "gabble-connection.h"
#include "gabble-roster-channel.h"
#include "namespaces.h"
#include "roster.h"
#include "util.h"

#define GOOGLE_ROSTER_VERSION "2"

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

/* signal enum */
enum
{
  NICKNAME_UPDATE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct _GabbleRosterPrivate GabbleRosterPrivate;
struct _GabbleRosterPrivate
{
  GabbleConnection *conn;

  LmMessageHandler *iq_cb;
  LmMessageHandler *presence_cb;

  GHashTable *channels;
  GHashTable *items;

  gboolean roster_received;
  gboolean dispose_has_run;
};

typedef enum
{
  GOOGLE_ITEM_TYPE_NORMAL = 0,
  GOOGLE_ITEM_TYPE_BLOCKED,
  GOOGLE_ITEM_TYPE_HIDDEN,
  GOOGLE_ITEM_TYPE_PINNED
} GoogleItemType;

typedef struct _GabbleRosterItem GabbleRosterItem;
struct _GabbleRosterItem
{
  GabbleRosterSubscription subscription;
  gboolean ask_subscribe;
  GoogleItemType google_type;
  gchar *name;
  gchar **groups;
};

static void gabble_roster_factory_iface_init ();
static void gabble_roster_init (GabbleRoster *roster);
static GObject * gabble_roster_constructor (GType type, guint n_props, GObjectConstructParam *props);
static void gabble_roster_dispose (GObject *object);
static void gabble_roster_finalize (GObject *object);
static void gabble_roster_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gabble_roster_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static void _gabble_roster_item_free (GabbleRosterItem *item);

G_DEFINE_TYPE_WITH_CODE (GabbleRoster, gabble_roster, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, gabble_roster_factory_iface_init));

#define GABBLE_ROSTER_GET_PRIVATE(o)     ((GabbleRosterPrivate*) ((o)->priv));

static void
gabble_roster_class_init (GabbleRosterClass *gabble_roster_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roster_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_roster_class, sizeof (GabbleRosterPrivate));

  object_class->constructor = gabble_roster_constructor;

  object_class->dispose = gabble_roster_dispose;
  object_class->finalize = gabble_roster_finalize;

  object_class->get_property = gabble_roster_get_property;
  object_class->set_property = gabble_roster_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "XMPP roster object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_CONNECTION,
                                   param_spec);

  signals[NICKNAME_UPDATE] = g_signal_new (
    "nickname-update",
    G_TYPE_FROM_CLASS (gabble_roster_class),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
gabble_roster_init (GabbleRoster *obj)
{
  GabbleRosterPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_ROSTER, GabbleRosterPrivate);

  obj->priv = priv;

  priv->channels = g_hash_table_new_full (g_direct_hash,
                                          g_direct_equal,
                                          NULL,
                                          g_object_unref);

  priv->items = g_hash_table_new_full (g_direct_hash,
                                       g_direct_equal,
                                       NULL,
                                       (GDestroyNotify) _gabble_roster_item_free);
}

static GObject *
gabble_roster_constructor (GType type, guint n_props,
                           GObjectConstructParam *props)
{
  GObject *obj;
  /* GabbleRosterPrivate *priv; */

  obj = G_OBJECT_CLASS (gabble_roster_parent_class)->
           constructor (type, n_props, props);
  /* priv = GABBLE_ROSTER_GET_PRIVATE (GABBLE_ROSTER (obj)); */

  return obj;
}

void
gabble_roster_dispose (GObject *object)
{
  GabbleRoster *self = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");

  priv->dispose_has_run = TRUE;

  g_assert (priv->iq_cb == NULL);
  g_assert (priv->presence_cb == NULL);

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_roster_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_parent_class)->dispose (object);
}

static void
item_handle_unref_foreach (gpointer key, gpointer data, gpointer user_data)
{
  GabbleHandle handle = (GabbleHandle) key;
  GabbleRosterPrivate *priv = (GabbleRosterPrivate *) user_data;

  gabble_handle_unref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
}

void
gabble_roster_finalize (GObject *object)
{
  GabbleRoster *self = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (self);

  DEBUG ("called with %p", object);

  g_hash_table_foreach (priv->items, item_handle_unref_foreach, priv);
  g_hash_table_destroy (priv->items);

  G_OBJECT_CLASS (gabble_roster_parent_class)->finalize (object);
}

static void
gabble_roster_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GabbleRoster *roster = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_roster_set_property (GObject     *object,
                            guint        property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GabbleRoster *roster = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
_gabble_roster_item_free (GabbleRosterItem *item)
{
  g_assert (item != NULL);

  g_strfreev (item->groups);
  g_free (item->name);
  g_free (item);
}

static const gchar *
_subscription_to_string (GabbleRosterSubscription subscription)
{
  switch (subscription)
    {
      case GABBLE_ROSTER_SUBSCRIPTION_NONE:
        return "none";
      case GABBLE_ROSTER_SUBSCRIPTION_FROM:
        return "from";
      case GABBLE_ROSTER_SUBSCRIPTION_TO:
        return "to";
      case GABBLE_ROSTER_SUBSCRIPTION_BOTH:
        return "both";
      case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
        return "remove";
      default:
        g_assert_not_reached ();
        return NULL;
    }
}

static GabbleRosterSubscription
_parse_item_subscription (LmMessageNode *item_node)
{
  const gchar *subscription;

  g_assert (item_node != NULL);

  subscription = lm_message_node_get_attribute (item_node, "subscription");

  if (NULL == subscription || 0 == strcmp (subscription, "none"))
    return GABBLE_ROSTER_SUBSCRIPTION_NONE;
  else if (0 == strcmp (subscription, "from"))
    return GABBLE_ROSTER_SUBSCRIPTION_FROM;
  else if (0 == strcmp (subscription, "to"))
    return GABBLE_ROSTER_SUBSCRIPTION_TO;
  else if (0 == strcmp (subscription, "both"))
    return GABBLE_ROSTER_SUBSCRIPTION_BOTH;
  else if (0 == strcmp (subscription, "remove"))
    return GABBLE_ROSTER_SUBSCRIPTION_REMOVE;
  else
    {
       NODE_DEBUG (item_node, "got unexpected subscription value");
      return GABBLE_ROSTER_SUBSCRIPTION_NONE;
    }
}

static gchar **
_parse_item_groups (LmMessageNode *item_node)
{
  LmMessageNode *group_node;
  GPtrArray *strv;

  strv = g_ptr_array_new ();

  for (group_node = item_node->children;
      NULL != group_node;
      group_node = group_node->next)
    {
      if (0 != strcmp (group_node->name, "group"))
        continue;

      if (NULL == group_node->value)
        continue;

      g_ptr_array_add (strv, g_strdup (group_node->value));
    }

  g_ptr_array_add (strv, NULL);

  return (gchar **) g_ptr_array_free (strv, FALSE);
}

static const gchar *
_google_item_type_to_string (GoogleItemType google_type)
{
  switch (google_type)
    {
      case GOOGLE_ITEM_TYPE_NORMAL:
        return NULL;
      case GOOGLE_ITEM_TYPE_BLOCKED:
        return "B";
      case GOOGLE_ITEM_TYPE_HIDDEN:
        return "H";
      case GOOGLE_ITEM_TYPE_PINNED:
        return "P";
    }

  g_assert_not_reached ();

  return NULL;
}

static GoogleItemType
_parse_google_item_type (LmMessageNode *item_node)
{
  const gchar *google_type;

  g_assert (item_node != NULL);

  google_type = lm_message_node_get_attribute (item_node, "gr:t");

  if (NULL == google_type)
    return GOOGLE_ITEM_TYPE_NORMAL;
  else if (!g_strdiff (google_type, "B"))
    return GOOGLE_ITEM_TYPE_BLOCKED;
  else if (!g_strdiff (google_type, "H"))
    return GOOGLE_ITEM_TYPE_HIDDEN;
  else if (!g_strdiff (google_type, "P"))
    return GOOGLE_ITEM_TYPE_PINNED;

  NODE_DEBUG (item_node, "got unexpected google contact type value");

  return GOOGLE_ITEM_TYPE_NORMAL;
}

static gboolean
_google_roster_item_should_keep (LmMessageNode *item_node,
                                 GabbleRosterItem *item)
{
  const gchar *attr;

  /* skip automatically subscribed Google roster items */
  attr = lm_message_node_get_attribute (item_node, "gr:autosub");

  if (!g_strdiff (attr, "true"))
    return FALSE;

  /* skip email addresses the user has invited */
  attr = lm_message_node_get_attribute (item_node, "gr:inv");

  if (!g_strdiff (attr, "A"))
    return FALSE;

  /* skip email addresses that replied to an invite */
  attr = lm_message_node_get_attribute (item_node, "gr:alias-for");

  if (attr != NULL)
    return FALSE;

  /* skip hidden items */
  if (item->google_type == GOOGLE_ITEM_TYPE_HIDDEN)
    return FALSE;

  /* allow items that have rejected a subscription */
  attr = lm_message_node_get_attribute (item_node, "gr:rejected");

  if (!g_strdiff (attr, "true"))
    return TRUE;

  /* allow items that we've requested a subscription from */
  if (item->ask_subscribe)
    return TRUE;

  if (item->subscription != GABBLE_ROSTER_SUBSCRIPTION_NONE)
    return TRUE;

  /* discard anything else */
  return FALSE;
}

static GabbleRosterItem *
_gabble_roster_item_get (GabbleRoster *roster,
                         GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (gabble_handle_is_valid (priv->conn->handles,
        TP_HANDLE_TYPE_CONTACT, handle, NULL));

  item = g_hash_table_lookup (priv->items, GINT_TO_POINTER (handle));

  if (NULL == item)
    {
      item = g_new0 (GabbleRosterItem, 1);
      gabble_handle_ref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
      g_hash_table_insert (priv->items, GINT_TO_POINTER (handle), item);
    }

  return item;
}

static void
_gabble_roster_item_remove (GabbleRoster *roster,
                            GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (gabble_handle_is_valid (priv->conn->handles,
        TP_HANDLE_TYPE_CONTACT, handle, NULL));

  g_hash_table_remove (priv->items, GINT_TO_POINTER (handle));
  gabble_handle_unref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
}

static GabbleRosterItem *
_gabble_roster_item_update (GabbleRoster *roster,
                            GabbleHandle handle,
                            LmMessageNode *node,
                            gboolean google_roster_mode)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;
  const gchar *ask, *name;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (gabble_handle_is_valid (priv->conn->handles,
        TP_HANDLE_TYPE_CONTACT, handle, NULL));
  g_assert (node != NULL);

  item = _gabble_roster_item_get (roster, handle);

  item->subscription = _parse_item_subscription (node);

  ask = lm_message_node_get_attribute (node, "ask");
  if (NULL != ask && 0 == strcmp (ask, "subscribe"))
    item->ask_subscribe = TRUE;
  else
    item->ask_subscribe = FALSE;

  if (google_roster_mode)
    {
      item->google_type = _parse_google_item_type (node);

      /* discard roster item if strange */
      if (!_google_roster_item_should_keep (node, item))
        item->subscription = GABBLE_ROSTER_SUBSCRIPTION_REMOVE;
    }

  if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    name = NULL;
  else
    name = lm_message_node_get_attribute (node, "name");

  if (g_strdiff (item->name, name))
    {
      g_free (item->name);
      item->name = g_strdup (name);

      DEBUG ("name for handle %d changed to %s", handle, name);
      g_signal_emit (G_OBJECT (roster), signals[NICKNAME_UPDATE], 0, handle);
    }

  g_strfreev (item->groups);
  item->groups = _parse_item_groups (node);

  return item;
}


#ifdef ENABLE_DEBUG
static gchar *
_gabble_roster_item_dump (GabbleRosterItem *item)
{
  GString *str;

  g_assert (item != NULL);

  str = g_string_new ("subscription: ");

  g_string_append (str, _subscription_to_string (item->subscription));

  if (item->ask_subscribe)
    g_string_append (str, ", ask: subscribe");

  if (item->google_type != GOOGLE_ITEM_TYPE_NORMAL)
    g_string_append_printf (str, ", google_type: %s",
        _google_item_type_to_string (item->google_type));

  if (item->name)
    g_string_append_printf (str, ", name: %s", item->name);

  if (item->groups)
    {
      gchar **tmp;
      g_string_append (str, ", groups: { ");
      for (tmp = item->groups; *tmp; tmp++)
        {
          g_string_append (str, *tmp);
          g_string_append_c (str, ' ');
        }
      g_string_append (str, "}");
    }

  return g_string_free (str, FALSE);
}
#endif /* ENABLE_DEBUG */


static LmMessage *
_gabble_roster_message_new (GabbleRoster *roster,
                            LmMessageSubType sub_type,
                            LmMessageNode **query_return)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessage *message;
  LmMessageNode *query_node;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));

  message = lm_message_new_with_sub_type (NULL,
                                          LM_MESSAGE_TYPE_IQ,
                                          sub_type);

  query_node = lm_message_node_add_child (message->node, "query", NULL);

  if (NULL != query_return)
    *query_return = query_node;

  lm_message_node_set_attribute (query_node, "xmlns", NS_ROSTER);

  if (priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER)
    {
      lm_message_node_set_attributes (query_node,
          "xmlns:gr", NS_GOOGLE_ROSTER,
          "gr:ext", GOOGLE_ROSTER_VERSION,
          "gr:include", "all",
          NULL);
    }

  return message;
}


static LmMessage *
_gabble_roster_item_to_message (GabbleRoster *roster,
                                GabbleHandle handle,
                                LmMessageNode **item_return)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;
  LmMessage *message;
  LmMessageNode *query_node, *item_node;
  const gchar *jid;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (gabble_handle_is_valid (priv->conn->handles,
        TP_HANDLE_TYPE_CONTACT, handle, NULL));

  item = _gabble_roster_item_get (roster, handle);

  message = _gabble_roster_message_new (roster, LM_MESSAGE_SUB_TYPE_SET,
      &query_node);

  item_node = lm_message_node_add_child (query_node, "item", NULL);

  if (NULL != item_return)
    *item_return = item_node;

  jid = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_CONTACT,
      handle);
  lm_message_node_set_attribute (item_node, "jid", jid);

  if (item->subscription != GABBLE_ROSTER_SUBSCRIPTION_NONE)
    {
      const gchar *subscription =  _subscription_to_string (item->subscription);
      lm_message_node_set_attribute (item_node, "subscription", subscription);
    }

  if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    goto DONE;

  if ((priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER) &&
      item->google_type != GOOGLE_ITEM_TYPE_NORMAL)
    lm_message_node_set_attribute (item_node, "gr:t",
        _google_item_type_to_string (item->google_type));

  if (item->ask_subscribe)
    lm_message_node_set_attribute (item_node, "ask", "subscribe");

  if (item->name)
    lm_message_node_set_attribute (item_node, "name", item->name);

  if (item->groups)
    {
      gchar **tmp;

      for (tmp = item->groups; *tmp; tmp++)
        {
          lm_message_node_add_child (item_node, "group", *tmp);
        }
    }

DONE:
  return message;
}

static GabbleRosterChannel *
_gabble_roster_create_channel (GabbleRoster *roster,
                               GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterChannel *chan;
  const char *name;
  char *object_path;

  g_assert (priv->channels != NULL);
  g_assert (g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)) == NULL);

  name = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_LIST, handle);
  object_path = g_strdup_printf ("%s/RosterChannel/%s", priv->conn->object_path, name);
  chan = g_object_new (GABBLE_TYPE_ROSTER_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  DEBUG ("created %s", object_path);
  g_free (object_path);

  g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

  if (priv->roster_received)
    {
      DEBUG ("roster already received, emitting signal for %s list channel",
          name);

      g_signal_emit_by_name (roster, "new-channel", chan);
    }
  else
    {
      DEBUG ("roster not yet received, not emitting signal for %s list channel",
          name);
    }

  return chan;
}

static GabbleRosterChannel *
_gabble_roster_get_channel (GabbleRoster *roster,
                            GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterChannel *chan;

  g_assert (priv->channels != NULL);
  g_assert (gabble_handle_is_valid (priv->conn->handles, TP_HANDLE_TYPE_LIST, handle, NULL));

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

  if (chan == NULL)
    chan = _gabble_roster_create_channel (roster, handle);

  return chan;
}

static void
_gabble_roster_emit_one (gpointer key,
                         gpointer value,
                         gpointer data)
{
  GabbleRoster *roster = GABBLE_ROSTER (data);
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (value);
#ifdef ENABLE_DEBUG
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleHandle handle = GPOINTER_TO_INT (key);
  const gchar *name = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_LIST, handle);

  DEBUG ("roster now received, emitting signal signal for %s list channel",
      name);
#endif

  g_signal_emit_by_name (roster, "new-channel", chan);
}

static void
_gabble_roster_received (GabbleRoster *roster)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  g_assert (priv->channels != NULL);

  if (!priv->roster_received)
    {
      priv->roster_received = TRUE;

      g_hash_table_foreach (priv->channels, _gabble_roster_emit_one, roster);
    }
}

/**
 * gabble_roster_iq_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with roster queries, and allows other handlers
 * if queries other than rosters are received.
 */
static LmHandlerResult
gabble_roster_iq_cb (LmMessageHandler *handler,
                     LmConnection *lmconn,
                     LmMessage *message,
                     gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessageNode *iq_node, *query_node;
  LmMessageSubType sub_type;
  const gchar *from;
  gboolean google_roster = FALSE;

  g_assert (lmconn == priv->conn->lmconn);

  if (priv->channels == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  iq_node = lm_message_get_node (message);
  query_node = lm_message_node_get_child_with_namespace (iq_node, "query",
      NS_ROSTER);

  if (query_node == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  from = lm_message_node_get_attribute (message->node, "from");

  if (from != NULL)
    {
      GabbleHandle sender;

      sender = gabble_handle_for_contact (priv->conn->handles,
          from, FALSE);

      if (sender != priv->conn->self_handle)
        {
           NODE_DEBUG (iq_node, "discarding roster IQ which is not from "
              "ourselves or the server");
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  if (priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER)
    {
      const char *gr_ext;

      gr_ext = lm_message_node_get_attribute (query_node, "gr:ext");

      if (!g_strdiff (gr_ext, GOOGLE_ROSTER_VERSION))
        google_roster = TRUE;
    }

  sub_type = lm_message_get_sub_type (message);

  /* if this is a result, it's from our initial query. if it's a set,
   * it's a roster push. either way, parse the items. */
  switch (sub_type)
    {
      LmMessageNode *item_node;
      GIntSet *empty, *pub_add, *pub_rem,
              *sub_add, *sub_rem, *sub_rp,
              *known_add, *known_rem,
              *deny_add, *deny_rem;
      GabbleHandle handle;
      GabbleRosterChannel *chan;

    case LM_MESSAGE_SUB_TYPE_RESULT:
    case LM_MESSAGE_SUB_TYPE_SET:
      /* asymmetry is because we don't get locally pending subscription
       * requests via <roster>, we get it via <presence> */
      empty = g_intset_new ();
      pub_add = g_intset_new ();
      pub_rem = g_intset_new ();
      sub_add = g_intset_new ();
      sub_rem = g_intset_new ();
      sub_rp = g_intset_new ();
      known_add = g_intset_new ();
      known_rem = g_intset_new ();

      if (google_roster)
        {
          deny_add = g_intset_new ();
          deny_rem = g_intset_new ();
        }
      else
        {
          deny_add = NULL;
          deny_rem = NULL;
        }

      /* get the publish channel first because we need it when processing */
      handle = GABBLE_LIST_HANDLE_PUBLISH;
      chan = _gabble_roster_get_channel (roster, handle);

      /* iterate every sub-node, which we expect to be <item>s */
      for (item_node = query_node->children;
           item_node;
           item_node = item_node->next)
        {
          const char *jid;
          GabbleRosterItem *item;

          if (strcmp (item_node->name, "item"))
            {
               NODE_DEBUG (item_node, "query sub-node is not item, skipping");
              continue;
            }

          jid = lm_message_node_get_attribute (item_node, "jid");
          if (!jid)
            {
               NODE_DEBUG (item_node, "item node has no jid, skipping");
              continue;
            }

          handle = gabble_handle_for_contact (priv->conn->handles, jid, FALSE);
          if (handle == 0)
            {
               NODE_DEBUG (item_node, "item jid is malformed, skipping");
              continue;
            }

          item = _gabble_roster_item_update (roster, handle, item_node,
                                             google_roster);

#ifdef ENABLE_DEBUG
          if (DEBUGGING)
            {
              gchar *dump = _gabble_roster_item_dump (item);
              DEBUG ("jid: %s, %s", jid, dump);
              g_free (dump);
            }
#endif

          /* handle publish list changes */
          switch (item->subscription)
            {
            case GABBLE_ROSTER_SUBSCRIPTION_FROM:
            case GABBLE_ROSTER_SUBSCRIPTION_BOTH:
              g_intset_add (pub_add, handle);
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_NONE:
            case GABBLE_ROSTER_SUBSCRIPTION_TO:
            case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
              /* publish channel is a bit odd, the roster item doesn't tell us
               * if someone is awaiting our approval - we get this via presence
               * type=subscribe, so we have to not remove them if they're
               * already local_pending in our publish channel */
              if (!handle_set_is_member (chan->group.local_pending, handle))
                {
                  g_intset_add (pub_rem, handle);
                }
              break;
            default:
              g_assert_not_reached ();
            }

          /* handle subscribe list changes */
          switch (item->subscription)
            {
            case GABBLE_ROSTER_SUBSCRIPTION_TO:
            case GABBLE_ROSTER_SUBSCRIPTION_BOTH:
              g_intset_add (sub_add, handle);
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_NONE:
            case GABBLE_ROSTER_SUBSCRIPTION_FROM:
              if (item->ask_subscribe)
                g_intset_add (sub_rp, handle);
              else
                g_intset_add (sub_rem, handle);
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
              g_intset_add (sub_rem, handle);
              break;
            default:
              g_assert_not_reached ();
            }

          /* handle known list changes */
          switch (item->subscription)
            {
            case GABBLE_ROSTER_SUBSCRIPTION_NONE:
            case GABBLE_ROSTER_SUBSCRIPTION_TO:
            case GABBLE_ROSTER_SUBSCRIPTION_FROM:
            case GABBLE_ROSTER_SUBSCRIPTION_BOTH:
              g_intset_add (known_add, handle);
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
              g_intset_add (known_rem, handle);
              break;
            default:
              g_assert_not_reached ();
            }

          /* handle deny list changes */
          if (google_roster)
            {
              switch (item->subscription)
                {
                case GABBLE_ROSTER_SUBSCRIPTION_NONE:
                case GABBLE_ROSTER_SUBSCRIPTION_TO:
                case GABBLE_ROSTER_SUBSCRIPTION_FROM:
                case GABBLE_ROSTER_SUBSCRIPTION_BOTH:
                  if (item->google_type == GOOGLE_ITEM_TYPE_BLOCKED)
                    g_intset_add (deny_add, handle);
                  else
                    g_intset_add (deny_rem, handle);
                  break;
                case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
                  g_intset_add (deny_rem, handle);
                  break;
                default:
                  g_assert_not_reached ();
                }
            }

          /* remove item last to avoid dereferencing freed memory */
          if (GABBLE_ROSTER_SUBSCRIPTION_REMOVE == item->subscription)
            _gabble_roster_item_remove (roster, handle);
        }

      /* chan was initialised to the publish channel before the for loop */

      DEBUG ("calling change members on publish channel");
      gabble_group_mixin_change_members (G_OBJECT (chan),
            "", pub_add, pub_rem, empty, empty, 0, 0);

      handle = GABBLE_LIST_HANDLE_SUBSCRIBE;
      chan = _gabble_roster_get_channel (roster, handle);

      DEBUG ("calling change members on subscribe channel");
      gabble_group_mixin_change_members (G_OBJECT (chan),
            "", sub_add, sub_rem, empty, sub_rp, 0, 0);

      handle = GABBLE_LIST_HANDLE_KNOWN;
      chan = _gabble_roster_get_channel (roster, handle);

      DEBUG ("calling change members on known channel");
      gabble_group_mixin_change_members (G_OBJECT (chan),
            "", known_add, known_rem, empty, empty, 0, 0);

      if (google_roster)
        {
          handle = GABBLE_LIST_HANDLE_DENY;
          chan = _gabble_roster_get_channel (roster, handle);

          DEBUG ("calling change members on deny channel");
          gabble_group_mixin_change_members (G_OBJECT (chan),
              "", deny_add, deny_rem, empty, empty, 0, 0);

          g_intset_destroy (deny_add);
          g_intset_destroy (deny_rem);
        }

      g_intset_destroy (empty);
      g_intset_destroy (pub_add);
      g_intset_destroy (pub_rem);
      g_intset_destroy (sub_add);
      g_intset_destroy (sub_rem);
      g_intset_destroy (sub_rp);
      g_intset_destroy (known_add);
      g_intset_destroy (known_rem);
      break;
    default:
       NODE_DEBUG (iq_node, "unhandled roster IQ");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  switch (sub_type)
    {
    case LM_MESSAGE_SUB_TYPE_RESULT:
      /* result means it's a roster push, so the roster is now complete and we
       * can emit signals */
      _gabble_roster_received (roster);
      break;
    case LM_MESSAGE_SUB_TYPE_SET:
      /* acknowledge roster */
      _gabble_connection_acknowledge_set_iq (priv->conn, message);
      break;
    default:
      break;
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


static void
_gabble_roster_send_presence_ack (GabbleRoster *roster,
                                  const gchar *from,
                                  LmMessageSubType sub_type,
                                  gboolean changed)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessage *reply;

  if (!changed)
    {
      DEBUG ("not sending ack to avoid loop with buggy server");
      return;
    }

  switch (sub_type)
    {
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE:
      sub_type = LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED;
      break;
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBED:
      sub_type = LM_MESSAGE_SUB_TYPE_SUBSCRIBE;
      break;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED:
      sub_type = LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE;
      break;
    default:
      g_assert_not_reached();
      return;
    }

  reply = lm_message_new_with_sub_type (from,
      LM_MESSAGE_TYPE_PRESENCE,
      sub_type);

  _gabble_connection_send (priv->conn, reply, NULL);

  lm_message_unref (reply);
}


/**
 * connection_presence_roster_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
gabble_roster_presence_cb (LmMessageHandler *handler,
                           LmConnection *lmconn,
                           LmMessage *message,
                           gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessageNode *pres_node, *child_node;
  const char *from;
  LmMessageSubType sub_type;
  GIntSet *empty, *tmp;
  GabbleHandle handle;
  const gchar *status_message = NULL;
  GabbleRosterChannel *chan = NULL;
  gboolean changed;

  g_assert (lmconn == priv->conn->lmconn);

  if (priv->channels == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  pres_node = lm_message_get_node (message);

  from = lm_message_node_get_attribute (pres_node, "from");

  if (from == NULL)
    {
       NODE_DEBUG (pres_node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (message);

  handle = gabble_handle_for_contact (priv->conn->handles, from, FALSE);

  if (handle == 0)
    {
       NODE_DEBUG (pres_node, "ignoring presence from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (handle == priv->conn->self_handle)
    {
       NODE_DEBUG (pres_node, "ignoring presence from ourselves on another resource");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  g_assert (handle != 0);

  child_node = lm_message_node_get_child (pres_node, "status");
  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  switch (sub_type)
    {
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBE:
      DEBUG ("making %s (handle %u) local pending on the publish channel",
          from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = GABBLE_LIST_HANDLE_PUBLISH;
      chan = _gabble_roster_get_channel (roster, handle);
      gabble_group_mixin_change_members (G_OBJECT (chan), status_message,
          empty, empty, tmp, empty, 0, 0);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE:
      DEBUG ("removing %s (handle %u) from the publish channel",
          from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = GABBLE_LIST_HANDLE_PUBLISH;
      chan = _gabble_roster_get_channel (roster, handle);
      changed = gabble_group_mixin_change_members (G_OBJECT (chan),
          status_message, empty, tmp, empty, empty, 0, 0);

      _gabble_roster_send_presence_ack (roster, from, sub_type, changed);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBED:
      DEBUG ("adding %s (handle %u) to the subscribe channel",
          from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = GABBLE_LIST_HANDLE_SUBSCRIBE;
      chan = _gabble_roster_get_channel (roster, handle);
      changed = gabble_group_mixin_change_members (G_OBJECT (chan),
          status_message, tmp, empty, empty, empty, 0, 0);

      _gabble_roster_send_presence_ack (roster, from, sub_type, changed);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED:
      DEBUG ("removing %s (handle %u) from the subscribe channel",
          from, handle);

      empty = g_intset_new ();
      tmp = g_intset_new ();
      g_intset_add (tmp, handle);

      handle = GABBLE_LIST_HANDLE_SUBSCRIBE;
      chan = _gabble_roster_get_channel (roster, handle);
      changed = gabble_group_mixin_change_members (G_OBJECT (chan),
          status_message, empty, tmp, empty, empty, 0, 0);

      _gabble_roster_send_presence_ack (roster, from, sub_type, changed);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    default:
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
}

static void
gabble_roster_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  DEBUG ("closing channels");

  if (priv->channels)
    {
      g_hash_table_destroy (priv->channels);
      priv->channels = NULL;
    }
}

static void
gabble_roster_factory_iface_connecting (TpChannelFactoryIface *iface)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  DEBUG ("adding callbacks");

  g_assert (priv->iq_cb == NULL);
  g_assert (priv->presence_cb == NULL);

  priv->iq_cb = lm_message_handler_new (gabble_roster_iq_cb,
                                        roster, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
                                          priv->iq_cb,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_HANDLER_PRIORITY_NORMAL);

  priv->presence_cb = lm_message_handler_new (gabble_roster_presence_cb,
                                              roster, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
                                          priv->presence_cb,
                                          LM_MESSAGE_TYPE_PRESENCE,
                                          LM_HANDLER_PRIORITY_LAST);
}

static void
gabble_roster_factory_iface_connected (TpChannelFactoryIface *iface)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessage *message;

  DEBUG ("requesting roster");

  message = _gabble_roster_message_new (roster, LM_MESSAGE_SUB_TYPE_GET, NULL);

  _gabble_connection_send (priv->conn, message, NULL);

  lm_message_unref (message);
}

static void
gabble_roster_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  DEBUG ("removing callbacks");

  g_assert (priv->iq_cb != NULL);
  g_assert (priv->presence_cb != NULL);

  lm_connection_unregister_message_handler (priv->conn->lmconn,
                                            priv->iq_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_cb);
  priv->iq_cb = NULL;

  lm_connection_unregister_message_handler (priv->conn->lmconn,
                                            priv->presence_cb,
                                            LM_MESSAGE_TYPE_PRESENCE);
  lm_message_handler_unref (priv->presence_cb);
  priv->presence_cb = NULL;
}

struct foreach_data {
    TpChannelFunc func;
    gpointer data;
};

static void
_gabble_roster_factory_iface_foreach_one (gpointer key,
                                          gpointer value,
                                          gpointer data)
{
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);
  struct foreach_data *foreach = (struct foreach_data *) data;

  foreach->func (chan, foreach->data);
}

static void
gabble_roster_factory_iface_foreach (TpChannelFactoryIface *iface,
                                     TpChannelFunc func,
                                     gpointer data)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  struct foreach_data foreach;

  foreach.func = func;
  foreach.data = data;

  g_hash_table_foreach (priv->channels,
      _gabble_roster_factory_iface_foreach_one, &foreach);
}

static TpChannelFactoryRequestStatus
gabble_roster_factory_iface_request (TpChannelFactoryIface *iface,
                                     const gchar *chan_type,
                                     TpHandleType handle_type,
                                     guint handle,
                                     TpChannelIface **ret,
                                     GError **error)
{
  GabbleRoster *roster = GABBLE_ROSTER (iface);
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);

  if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_LIST)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!gabble_handle_is_valid (priv->conn->handles,
                               TP_HANDLE_TYPE_LIST,
                               handle,
                               NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  /* disallow "deny" channels if we don't have google:roster support */
  if (handle == GABBLE_LIST_HANDLE_DENY &&
      !(priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (priv->roster_received)
    {
      GabbleRosterChannel *chan;
      chan = _gabble_roster_get_channel (roster, handle);
      *ret = TP_CHANNEL_IFACE (chan);
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE;
    }
  else
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
    }
}

static void
gabble_roster_factory_iface_init (gpointer g_iface,
                                  gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_roster_factory_iface_close_all;
  klass->connecting = gabble_roster_factory_iface_connecting;
  klass->connected = gabble_roster_factory_iface_connected;
  klass->disconnected = gabble_roster_factory_iface_disconnected;
  klass->foreach = gabble_roster_factory_iface_foreach;
  klass->request = gabble_roster_factory_iface_request;
}

GabbleRoster *
gabble_roster_new (GabbleConnection *conn)
{
  g_return_val_if_fail (conn != NULL, NULL);

  return g_object_new (GABBLE_TYPE_ROSTER,
                       "connection", conn,
                       NULL);
}

GabbleRosterSubscription
gabble_roster_handle_get_subscription (GabbleRoster *roster,
                                       GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;

  g_return_val_if_fail (roster != NULL, GABBLE_ROSTER_SUBSCRIPTION_NONE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster),
      GABBLE_ROSTER_SUBSCRIPTION_NONE);
  g_return_val_if_fail (gabble_handle_is_valid (priv->conn->handles,
        TP_HANDLE_TYPE_CONTACT, handle, NULL),
      GABBLE_ROSTER_SUBSCRIPTION_NONE);

  item = g_hash_table_lookup (priv->items, GINT_TO_POINTER (handle));

  if (NULL == item)
    return GABBLE_ROSTER_SUBSCRIPTION_NONE;

  return item->subscription;
}

gboolean
gabble_roster_handle_set_blocked (GabbleRoster *roster,
                                  GabbleHandle handle,
                                  gboolean blocked,
                                  GError **error)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;
  GoogleItemType orig_type;
  LmMessage *message;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (gabble_handle_is_valid (priv->conn->handles,
      TP_HANDLE_TYPE_CONTACT, handle, NULL), FALSE);
  g_return_val_if_fail (priv->conn->features &
      GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER, FALSE);

  item = _gabble_roster_item_get (roster, handle);
  orig_type = item->google_type;

  if (blocked == (orig_type == GOOGLE_ITEM_TYPE_BLOCKED))
    return TRUE;

  /* temporarily set the desired block state and generate a message */
  if (blocked)
    item->google_type = GOOGLE_ITEM_TYPE_BLOCKED;
  else
    item->google_type = GOOGLE_ITEM_TYPE_NORMAL;
  message = _gabble_roster_item_to_message (roster, handle, NULL);
  item->google_type = orig_type;

  ret = _gabble_connection_send (priv->conn, message, error);

  lm_message_unref (message);

  return ret;
}

gboolean
gabble_roster_handle_has_entry (GabbleRoster *roster,
                                GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (gabble_handle_is_valid (priv->conn->handles,
      TP_HANDLE_TYPE_CONTACT, handle, NULL), FALSE);

  item = g_hash_table_lookup (priv->items, GINT_TO_POINTER (handle));

  return (NULL != item);
}

const gchar *
gabble_roster_handle_get_name (GabbleRoster *roster,
                               GabbleHandle handle)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;

  g_return_val_if_fail (roster != NULL, NULL);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), NULL);
  g_return_val_if_fail (gabble_handle_is_valid (priv->conn->handles,
      TP_HANDLE_TYPE_CONTACT, handle, NULL), NULL);

  item = g_hash_table_lookup (priv->items, GINT_TO_POINTER (handle));

  if (NULL == item)
    return NULL;

  return item->name;
}

gboolean
gabble_roster_handle_set_name (GabbleRoster *roster,
                               GabbleHandle handle,
                               const gchar *name,
                               GError **error)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  LmMessage *message;
  LmMessageNode *item_node;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (gabble_handle_is_valid (priv->conn->handles,
      TP_HANDLE_TYPE_CONTACT, handle, NULL), FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  message = _gabble_roster_item_to_message (roster, handle, &item_node);

  lm_message_node_set_attribute (item_node, "name", name);

  ret = _gabble_connection_send (priv->conn, message, error);

  lm_message_unref (message);

  return ret;
}

gboolean
gabble_roster_handle_remove (GabbleRoster *roster,
                             GabbleHandle handle,
                             GError **error)
{
  GabbleRosterPrivate *priv = GABBLE_ROSTER_GET_PRIVATE (roster);
  GabbleRosterItem *item;
  GabbleRosterSubscription subscription;
  LmMessage *message;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (gabble_handle_is_valid (priv->conn->handles,
      TP_HANDLE_TYPE_CONTACT, handle, NULL), FALSE);

  item = _gabble_roster_item_get (roster, handle);
  subscription = item->subscription;
  item->subscription = GABBLE_ROSTER_SUBSCRIPTION_REMOVE;

  message = _gabble_roster_item_to_message (roster, handle, NULL);
  ret = _gabble_connection_send (priv->conn, message, error);
  lm_message_unref (message);

  item->subscription = subscription;

  return ret;
}
