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

#include "config.h"
#include "roster.h"

#define DBUS_API_SUBJECT_TO_CHANGE

#include <string.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_ROSTER

#include "caps-channel-manager.h"
#include "conn-aliasing.h"
#include "conn-presence.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "roster-channel.h"
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

struct _GabbleRosterPrivate
{
  GabbleConnection *conn;
  gulong status_changed_id;

  LmMessageHandler *iq_cb;
  LmMessageHandler *presence_cb;

  GHashTable *list_channels;
  GHashTable *group_channels;
  GHashTable *items;

  /* borrowed TpExportableChannel * => GSList of gpointer (request tokens)
   * that will be satisfied when it's ready. The requests are in reverse
   * chronological order */
  GHashTable *queued_requests;

  gboolean roster_received;
  gboolean dispose_has_run;
};

typedef enum
{
  GOOGLE_ITEM_TYPE_INVALID = -1,
  GOOGLE_ITEM_TYPE_NORMAL = 0,
  GOOGLE_ITEM_TYPE_BLOCKED,
  GOOGLE_ITEM_TYPE_HIDDEN,
  GOOGLE_ITEM_TYPE_PINNED,
} GoogleItemType;

typedef struct _GabbleRosterItemEdit GabbleRosterItemEdit;
struct _GabbleRosterItemEdit
{
  /* if these are ..._INVALID, that means don't edit */
  GabbleRosterSubscription new_subscription;
  GoogleItemType new_google_type;
  /* owned by the item; if NULL, that means don't edit */
  gchar *new_name;
  TpHandleSet *add_to_groups;
  TpHandleSet *remove_from_groups;
};

typedef struct _GabbleRosterItem GabbleRosterItem;
struct _GabbleRosterItem
{
  GabbleRosterSubscription subscription;
  gboolean ask_subscribe;
  GoogleItemType google_type;
  gchar *name;
  gchar *alias_for;
  TpHandleSet *groups;
  /* if not NULL, an edit attempt is already "in-flight" so instead of
   * sending off another, store required edits here until the one we
   * already sent is acknowledged - this prevents some race conditions
   */
  GabbleRosterItemEdit *unsent_edits;

  /* If non-zero, the GSource id for a call to flicker_prevention_timeout. */
  guint flicker_prevention_id;
};

typedef struct _FlickerPreventionCtx FlickerPreventionCtx;
struct _FlickerPreventionCtx
{
  GabbleRoster *roster;
  TpHandle handle;
  GabbleRosterItem *item;
};

static void channel_manager_iface_init (gpointer, gpointer);
static void gabble_roster_init (GabbleRoster *roster);
static GObject * gabble_roster_constructor (GType type, guint n_props,
    GObjectConstructParam *props);
static void gabble_roster_dispose (GObject *object);
static void gabble_roster_finalize (GObject *object);
static void gabble_roster_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gabble_roster_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static void roster_item_cancel_flicker_timeout (GabbleRosterItem *item);
static void _gabble_roster_item_free (GabbleRosterItem *item);
static void item_edit_free (GabbleRosterItemEdit *edits);
static void gabble_roster_close_all (GabbleRoster *roster);

G_DEFINE_TYPE_WITH_CODE (GabbleRoster, gabble_roster, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL));

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
      "Gabble connection object that owns this XMPP roster object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
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
  GabbleRosterPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      GABBLE_TYPE_ROSTER, GabbleRosterPrivate);

  obj->priv = priv;

  priv->list_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  priv->group_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  priv->items = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) _gabble_roster_item_free);

  priv->queued_requests = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, NULL);
}

void
gabble_roster_dispose (GObject *object)
{
  GabbleRoster *self = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");

  priv->dispose_has_run = TRUE;

  g_assert (priv->iq_cb == NULL);
  g_assert (priv->presence_cb == NULL);

  gabble_roster_close_all (self);
  g_assert (priv->group_channels == NULL);
  g_assert (priv->list_channels == NULL);
  g_assert (priv->queued_requests == NULL);

  if (G_OBJECT_CLASS (gabble_roster_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_parent_class)->dispose (object);
}

static void
item_handle_unref_foreach (gpointer key, gpointer data, gpointer user_data)
{
  TpHandle handle = GPOINTER_TO_UINT (key);
  GabbleRosterPrivate *priv = (GabbleRosterPrivate *) user_data;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  tp_handle_unref (contact_repo, handle);
}

void
gabble_roster_finalize (GObject *object)
{
  GabbleRoster *self = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = self->priv;

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
  GabbleRosterPrivate *priv = roster->priv;

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
  GabbleRosterPrivate *priv = roster->priv;

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

  tp_handle_set_destroy (item->groups);
  item_edit_free (item->unsent_edits);
  g_free (item->name);
  g_free (item->alias_for);

  roster_item_cancel_flicker_timeout (item);

  g_slice_free (GabbleRosterItem, item);
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

static TpHandleSet *
_parse_item_groups (LmMessageNode *item_node, TpBaseConnection *conn)
{
  TpHandleRepoIface *group_repo = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_GROUP);
  TpHandleSet *groups = tp_handle_set_new (group_repo);
  TpHandle handle;
  NodeIter i;

  for (i = node_iter (item_node); i; i = node_iter_next (i))
    {
      LmMessageNode *group_node = node_iter_data (i);
      const gchar *value;

      if (0 != strcmp (group_node->name, "group"))
        continue;

      value = lm_message_node_get_value (group_node);
      if (NULL == value)
        continue;

      handle = tp_handle_ensure (group_repo, value, NULL, NULL);
      if (!handle)
        continue;
      tp_handle_set_add (groups, handle);
      tp_handle_unref (group_repo, handle);
    }

  return groups;
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
      case GOOGLE_ITEM_TYPE_INVALID:
        g_assert_not_reached ();
        return NULL;
    }

  g_assert_not_reached ();

  return NULL;
}

static GoogleItemType
_parse_google_item_type (LmMessageNode *item_node)
{
  const gchar *google_type;

  g_assert (item_node != NULL);

  google_type = lm_message_node_get_attribute_with_namespace (item_node, "t",
      NS_GOOGLE_ROSTER);

  if (NULL == google_type)
    return GOOGLE_ITEM_TYPE_NORMAL;
  else if (!tp_strdiff (google_type, "B"))
    return GOOGLE_ITEM_TYPE_BLOCKED;
  else if (!tp_strdiff (google_type, "H"))
    return GOOGLE_ITEM_TYPE_HIDDEN;
  else if (!tp_strdiff (google_type, "P"))
    return GOOGLE_ITEM_TYPE_PINNED;

  NODE_DEBUG (item_node, "got unexpected google contact type value");

  return GOOGLE_ITEM_TYPE_NORMAL;
}

static gchar *
_extract_google_alias_for (LmMessageNode *item_node)
{
  return g_strdup (lm_message_node_get_attribute_with_namespace (item_node,
        "alias-for", NS_GOOGLE_ROSTER));
}

static gboolean
_google_roster_item_should_keep (const gchar *jid,
    GabbleRosterItem *item)
{
  /* hide hidden items */
  if (item->google_type == GOOGLE_ITEM_TYPE_HIDDEN)
    {
      DEBUG ("hiding %s: gr:t='H'", jid);
      return FALSE;
    }

  /* allow items that we've requested a subscription from */
  if (item->ask_subscribe)
    return TRUE;

  if (item->subscription != GABBLE_ROSTER_SUBSCRIPTION_NONE)
    return TRUE;

  /* discard anything else */
  DEBUG ("hiding %s: no subscription", jid);
  return FALSE;
}

static GabbleRosterItem *
_gabble_roster_item_get (GabbleRoster *roster,
                         TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *group_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_GROUP);
  GabbleRosterItem *item;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (tp_handle_is_valid (contact_repo, handle, NULL));

  item = g_hash_table_lookup (priv->items, GUINT_TO_POINTER (handle));

  if (NULL == item)
    {
      GabbleConnectionAliasSource source;
      gchar *alias = NULL;

      source = _gabble_connection_get_cached_alias (priv->conn, handle, &alias);

      g_assert (source < GABBLE_CONNECTION_ALIAS_FROM_ROSTER);

      if (source <= GABBLE_CONNECTION_ALIAS_FROM_JID)
        {
          g_free (alias);
          alias = NULL;
        }

      item = g_slice_new0 (GabbleRosterItem);
      item->name = alias;
      item->groups = tp_handle_set_new (group_repo);
      tp_handle_ref (contact_repo, handle);
      g_hash_table_insert (priv->items, GUINT_TO_POINTER (handle), item);
    }

  return item;
}

static void
_gabble_roster_item_remove (GabbleRoster *roster,
                            TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (tp_handle_is_valid (contact_repo, handle, NULL));

  g_hash_table_remove (priv->items, GUINT_TO_POINTER (handle));
  tp_handle_unref (contact_repo, handle);
}

/* FIXME: we have _get_channel, _create_channel, request_channel and
 * create_channel - this is confusing, and surely we ought to be able to
 * simplify the non-API code? */

/* the TpHandleType must be GROUP or LIST */
static GabbleRosterChannel *_gabble_roster_get_channel (GabbleRoster *,
    TpHandleType, TpHandle, gboolean *created, gpointer request_token);

static void gabble_roster_associate_request (GabbleRoster *self,
    GabbleRosterChannel *channel, gpointer request);

typedef struct
{
  TpHandleRepoIface *contact_repo;
  TpHandleRepoIface *group_repo;
  /* TpHandle borrowed from GroupMembershipUpdate => GroupMembershipUpdate */
  GHashTable *group_mem_updates;
  /* borrowed from the GabbleRosterItem */
  guint contact_handle;
} GroupsUpdateContext;

typedef struct
{
  TpHandleRepoIface *group_repo;
  TpHandleSet *contacts_added;
  TpHandleSet *contacts_removed;
  /* referenced */
  guint group_handle;
} GroupMembershipUpdate;

static GroupMembershipUpdate *
group_mem_update_ensure (GroupsUpdateContext *ctx,
                         TpHandle group_handle)
{
  GroupMembershipUpdate *update = g_hash_table_lookup (ctx->group_mem_updates,
      GUINT_TO_POINTER (group_handle));

  if (update != NULL)
    return update;

  DEBUG ("Creating new hash table entry for group#%u", group_handle);
  update = g_slice_new0 (GroupMembershipUpdate);
  update->group_repo = ctx->group_repo;
  tp_handle_ref (update->group_repo, group_handle);
  update->group_handle = group_handle;
  update->contacts_added = tp_handle_set_new (ctx->contact_repo);
  update->contacts_removed = tp_handle_set_new (ctx->contact_repo);
  g_hash_table_insert (ctx->group_mem_updates,
                       GUINT_TO_POINTER (group_handle),
                       update);
  return update;
}

static void
_update_add_to_group (guint group_handle, gpointer user_data)
{
  GroupsUpdateContext *ctx = (GroupsUpdateContext *) user_data;
  GroupMembershipUpdate *update = group_mem_update_ensure (ctx, group_handle);

  DEBUG ("- contact#%u added to group#%u", ctx->contact_handle,
         group_handle);
  tp_handle_set_add (update->contacts_added, ctx->contact_handle);
}

static void
_update_remove_from_group (guint group_handle, gpointer user_data)
{
  GroupsUpdateContext *ctx = (GroupsUpdateContext *) user_data;
  GroupMembershipUpdate *update = group_mem_update_ensure (ctx, group_handle);

  DEBUG ("- contact#%u removed from group#%u", ctx->contact_handle,
         group_handle);
  tp_handle_set_add (update->contacts_removed, ctx->contact_handle);
}

static GabbleRosterItem *
_gabble_roster_item_update (GabbleRoster *roster,
                            TpHandle contact_handle,
                            LmMessageNode *node,
                            GHashTable *group_updates,
                            gboolean google_roster_mode)
{
  GabbleRosterPrivate *priv = roster->priv;
  GabbleRosterItem *item;
  const gchar *ask, *name;
  TpIntSet *new_groups, *added_to, *removed_from, *removed_from2;
  TpHandleSet *new_groups_handle_set;
  GroupsUpdateContext ctx = { NULL, NULL, group_updates,
      contact_handle };

  ctx.contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  ctx.group_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_GROUP);

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (tp_handle_is_valid (ctx.contact_repo, contact_handle, NULL));
  g_assert (node != NULL);

  item = _gabble_roster_item_get (roster, contact_handle);

  item->subscription = _parse_item_subscription (node);

  ask = lm_message_node_get_attribute (node, "ask");
  if (NULL != ask && 0 == strcmp (ask, "subscribe"))
    item->ask_subscribe = TRUE;
  else
    item->ask_subscribe = FALSE;

  if (google_roster_mode)
    {
      item->google_type = _parse_google_item_type (node);
      g_free (item->alias_for);
      item->alias_for = _extract_google_alias_for (node);
    }

  if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    name = NULL;
  else
    name = lm_message_node_get_attribute (node, "name");

  if (tp_strdiff (item->name, name))
    {
      g_free (item->name);
      item->name = g_strdup (name);

      DEBUG ("name for contact#%u changed to %s", contact_handle,
          name);
      g_signal_emit (G_OBJECT (roster), signals[NICKNAME_UPDATE], 0,
          contact_handle);
    }

  new_groups_handle_set = _parse_item_groups (node,
      (TpBaseConnection *) priv->conn);
  new_groups = tp_handle_set_peek (new_groups_handle_set);

  removed_from = tp_intset_difference (tp_handle_set_peek (item->groups),
      new_groups);
  added_to = tp_handle_set_update (item->groups, new_groups);
  removed_from2 = tp_handle_set_difference_update (item->groups, removed_from);

  DEBUG ("Checking which groups contact#%u was just added to:",
      contact_handle);
  tp_intset_foreach (added_to, _update_add_to_group, &ctx);
  DEBUG ("Checking which groups contact#%u was just removed from:",
      contact_handle);
  tp_intset_foreach (removed_from, _update_remove_from_group, &ctx);

  tp_intset_destroy (added_to);
  tp_intset_destroy (removed_from);
  tp_intset_destroy (removed_from2);
  new_groups = NULL;
  tp_handle_set_destroy (new_groups_handle_set);

  return item;
}


#ifdef ENABLE_DEBUG
static void
_gabble_roster_item_dump_group (guint handle, gpointer user_data)
{
  g_string_append_printf ((GString *) user_data, "group#%u ", handle);
}

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
      tp_intset_foreach (tp_handle_set_peek (item->groups),
                         _gabble_roster_item_dump_group, str);
    }

  return g_string_free (str, FALSE);
}
#endif /* ENABLE_DEBUG */


static LmMessage *
_gabble_roster_message_new (GabbleRoster *roster,
                            LmMessageSubType sub_type,
                            LmMessageNode **query_return)
{
  GabbleRosterPrivate *priv = roster->priv;
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


struct _ItemToMessageContext {
    TpBaseConnection *conn;
    LmMessageNode *item_node;
};

static void
_gabble_roster_item_put_group_in_message (guint handle, gpointer user_data)
{
  struct _ItemToMessageContext *ctx =
    (struct _ItemToMessageContext *)user_data;
  TpHandleRepoIface *group_repo = tp_base_connection_get_handles (
      ctx->conn, TP_HANDLE_TYPE_GROUP);
  const char *name = tp_handle_inspect (group_repo, handle);

  lm_message_node_add_child (ctx->item_node, "group", name);
}

/* Return a message representing the current state of the item for contact
 * @handle on the roster @roster.
 *
 * If item_return is not NULL, populate it with the <item/> node.
 *
 * If item is not NULL, it represents the state we would like the contact's
 * roster item to have - use it instead of the contact's actual roster item
 * when composing the message.
 */
static LmMessage *
_gabble_roster_item_to_message (GabbleRoster *roster,
                                TpHandle handle,
                                LmMessageNode **item_return,
                                GabbleRosterItem *item)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  LmMessage *message;
  LmMessageNode *query_node, *item_node;
  const gchar *jid;
  struct _ItemToMessageContext ctx = {
      (TpBaseConnection *) priv->conn,
  };

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (tp_handle_is_valid (contact_repo, handle, NULL));

  if (!item)
    item = _gabble_roster_item_get (roster, handle);

  message = _gabble_roster_message_new (roster, LM_MESSAGE_SUB_TYPE_SET,
      &query_node);

  item_node = lm_message_node_add_child (query_node, "item", NULL);
  ctx.item_node = item_node;

  if (NULL != item_return)
    *item_return = item_node;

  jid = tp_handle_inspect (contact_repo, handle);
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
      tp_intset_foreach (tp_handle_set_peek (item->groups),
                         _gabble_roster_item_put_group_in_message,
                         (void *)&ctx);
    }

DONE:
  return message;
}


static void
gabble_roster_emit_new_channel (GabbleRoster *self,
                                GabbleRosterChannel *channel)
{
  GabbleRosterPrivate *priv = self->priv;
  GSList *requests_satisfied;

  requests_satisfied = g_hash_table_lookup (priv->queued_requests, channel);
  g_hash_table_steal (priv->queued_requests, channel);
  requests_satisfied = g_slist_reverse (requests_satisfied);

  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (channel), requests_satisfied);
  g_slist_free (requests_satisfied);
}


static void
roster_channel_closed_cb (GabbleRosterChannel *channel,
                          gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (user_data);
  guint handle_type, handle;
  GHashTable *channels;

  DEBUG ("%p, channel %p", self, channel);

  g_object_get (channel,
      "handle-type", &handle_type,
      "handle", &handle,
      NULL);

  g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
            handle_type == TP_HANDLE_TYPE_GROUP);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (channel));

  channels = (handle_type == TP_HANDLE_TYPE_LIST
                          ? self->priv->list_channels
                          : self->priv->group_channels);

  if (channels != NULL)
    {
      DEBUG ("removing channel with handle (type %u) #%u", handle_type,
          handle);
      g_hash_table_remove (channels, GUINT_TO_POINTER (handle));
    }
}


static GabbleRosterChannel *
_gabble_roster_create_channel (GabbleRoster *roster,
                               guint handle_type,
                               TpHandle handle,
                               gpointer request_token)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (conn,
      handle_type);
  GabbleRosterChannel *chan;
  const char *name;
  char *mangled_name;
  char *object_path;
  GHashTable *channels = (handle_type == TP_HANDLE_TYPE_LIST
                          ? priv->list_channels
                          : priv->group_channels);

  /* if this assertion succeeds, we know we have the right handle repo */
  g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
            handle_type == TP_HANDLE_TYPE_GROUP);
  g_assert (channels != NULL);
  g_assert (g_hash_table_lookup (channels, GUINT_TO_POINTER (handle)) == NULL);

  name = tp_handle_inspect (handle_repo, handle);
  DEBUG ("Instantiating channel %u:%u \"%s\"", handle_type, handle, name);
  mangled_name = tp_escape_as_identifier (name);
  object_path = g_strdup_printf ("%s/RosterChannel/%s/%s",
                                 conn->object_path,
                                 handle_type == TP_HANDLE_TYPE_LIST ? "List"
                                                                    : "Group",
                                 mangled_name);
  g_free (mangled_name);
  mangled_name = NULL;

  chan = g_object_new (GABBLE_TYPE_ROSTER_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "handle-type", handle_type,
                       NULL);

  DEBUG ("created %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) roster_channel_closed_cb,
      roster);

  g_hash_table_insert (channels, GUINT_TO_POINTER (handle), chan);

  if (priv->roster_received)
    {
      DEBUG ("roster already received, emitting signal for %s",
             object_path);

      if (request_token != NULL)
        gabble_roster_associate_request (roster, chan, request_token);

      gabble_roster_emit_new_channel (roster, chan);
    }
  else
    {
      /* Not associating the request with the channel; gabble_roster_request
       * does that for all requests except (channel newly created && roster
       * already recieved).
       */
      DEBUG ("roster not yet received, not emitting signal for %s list "
          "channel", name);
    }
  g_free (object_path);

  return chan;
}

static GabbleRosterChannel *
_gabble_roster_get_channel (GabbleRoster *roster,
                            guint handle_type,
                            TpHandle handle,
                            gboolean *created,
                            gpointer request_token)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, handle_type);
  GabbleRosterChannel *chan;
  GHashTable *channels = (handle_type == TP_HANDLE_TYPE_LIST
                          ? priv->list_channels
                          : priv->group_channels);

  /* if this assertion succeeds, we know we have the right handle repos */
  g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
            handle_type == TP_HANDLE_TYPE_GROUP);
  g_assert (channels != NULL);
  g_assert (tp_handle_is_valid (handle_repo, handle, NULL));

  DEBUG ("Looking up channel %u:%u \"%s\"", handle_type, handle,
         tp_handle_inspect (handle_repo, handle));
  chan = g_hash_table_lookup (channels, GUINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      if (created)
        *created = TRUE;
      chan = _gabble_roster_create_channel (roster, handle_type, handle,
          request_token);
    }
  else
    {
      if (created)
        *created = FALSE;
    }

  return chan;
}

struct _EmitOneData {
    GabbleRoster *roster;
    guint handle_type;    /* must be GROUP or LIST */
};


static void
_gabble_roster_emit_one (gpointer key,
                         gpointer value,
                         gpointer data)
{
  struct _EmitOneData *data_struct = (struct _EmitOneData *)data;
  GabbleRoster *roster = data_struct->roster;
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (value);
#ifdef ENABLE_DEBUG
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, data_struct->handle_type);
  TpHandle handle = GPOINTER_TO_UINT (key);
  const gchar *name;

  g_assert (data_struct->handle_type == TP_HANDLE_TYPE_GROUP ||
      data_struct->handle_type == TP_HANDLE_TYPE_LIST);
  g_assert (handle_repo != NULL);
  name = tp_handle_inspect (handle_repo, handle);

  DEBUG ("roster now received, emitting signal for %s list channel", name);
#endif

  gabble_roster_emit_new_channel (roster, chan);
}

static void
_gabble_roster_received (GabbleRoster *roster)
{
  GabbleRosterPrivate *priv = roster->priv;

  g_assert (priv->list_channels != NULL);

  if (!priv->roster_received)
    {
      struct _EmitOneData data = { roster, TP_HANDLE_TYPE_LIST };

      priv->roster_received = TRUE;

      g_hash_table_foreach (priv->list_channels, _gabble_roster_emit_one,
          &data);
      data.handle_type = TP_HANDLE_TYPE_GROUP;
      g_hash_table_foreach (priv->group_channels, _gabble_roster_emit_one,
          &data);
    }
}

static void
_group_mem_update_destroy (GroupMembershipUpdate *update)
{
  tp_handle_set_destroy (update->contacts_added);
  tp_handle_set_destroy (update->contacts_removed);
  tp_handle_unref (update->group_repo, update->group_handle);
  g_slice_free (GroupMembershipUpdate, update);
}

static gboolean
_update_group (gpointer key,
               gpointer value,
               gpointer user_data)
{
  guint group_handle = GPOINTER_TO_UINT (key);
  GabbleRoster *roster = GABBLE_ROSTER (user_data);
  GroupMembershipUpdate *update = value;
  GabbleRosterChannel *group_channel = _gabble_roster_get_channel (
      roster, TP_HANDLE_TYPE_GROUP, group_handle, NULL, NULL);
  TpIntSet *empty = tp_intset_new ();

#ifdef ENABLE_DEBUG
  g_assert (group_handle == update->group_handle);
#endif

  DEBUG ("Updating group channel %u now message has been received",
      group_handle);
  tp_group_mixin_change_members ((GObject *) group_channel,
      "", tp_handle_set_peek (update->contacts_added),
      tp_handle_set_peek (update->contacts_removed), empty, empty,
      0, 0);

  tp_intset_destroy (empty);

  return TRUE;
}

static FlickerPreventionCtx *
flicker_prevention_ctx_new (GabbleRoster *roster,
    TpHandle handle,
    GabbleRosterItem *item)
{
  FlickerPreventionCtx *ret = g_slice_new (FlickerPreventionCtx);

  /* Not taking a ref to the roster. The context is owned by the Item (well, by
   * its timeout) which is owned by the roster.
   */
  ret->roster = roster;
  /* Not taking a ref to the handle; we borrow the roster's ref, which is
   * released after the GabbleRosterItem is freed, at which point this context
   * will be destroyed.
   */
  ret->handle = handle;
  ret->item = item;

  return ret;
}

static void
flicker_prevention_ctx_free (gpointer ctx_)
{
  FlickerPreventionCtx *ctx = ctx_;

  ctx->roster = NULL;
  ctx->handle = 0;
  ctx->item = NULL;

  g_slice_free (FlickerPreventionCtx, ctx);
}

/* As described in roster/test-google-roster.py, we work around a Google Talk
 * server bug to avoid contacts flickering off and onto
 * subscribe:remote-pending when you try to subscribe to someone's presence.
 *
 * When we see a roster item with subscription=none/from and ask=subscribe:
 *  * if no call to this function is scheduled, we schedule a call
 *  * if one is already scheduled, we cancel it.
 *
 * When we see a roster item with subscription=none/from and no ask=subscribe:
 *  * if a call to this timeout is scheduled, do nothing, in case the contact
 *    flickers back to ask=subscribe before this fires;
 *  * if a call to this timeout is not scheduled, remove the contact from the
 *    subscribe list.
 *
 * This way, our subscription being cancelled or our subscription requests
 * being rescinded will show up on the subscribe list, albeit with a slight lag
 * in certain situations in case we're just seeing the Google talk server bug.
 */
static gboolean
flicker_prevention_timeout (gpointer ctx_)
{
  FlickerPreventionCtx *ctx = ctx_;
  GabbleRosterItem *item = ctx->item;

  DEBUG ("called for %u", ctx->handle);

  if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_NONE
      && !item->ask_subscribe)
    {
      GabbleRosterChannel *sub_chan = _gabble_roster_get_channel (ctx->roster,
          TP_HANDLE_TYPE_LIST, GABBLE_LIST_HANDLE_SUBSCRIBE, NULL, NULL);
      TpIntSet *rem = tp_intset_new_containing (ctx->handle);

      DEBUG ("removing %u from subscribe", ctx->handle);
      tp_group_mixin_change_members ((GObject *) sub_chan, "", NULL, rem, NULL,
          NULL, 0, 0);

      tp_intset_destroy (rem);
    }
  else
    {
      DEBUG ("subscription=%s and ask_subscribe=%s, nothing to do",
          _subscription_to_string (item->subscription),
          item->ask_subscribe ? "true" : "false");
    }

  ctx->item->flicker_prevention_id = 0;

  return FALSE;
}

static void
roster_item_ensure_flicker_timeout (GabbleRoster *roster,
    TpHandle handle,
    GabbleRosterItem *item)
{
  if (item->flicker_prevention_id == 0)
    {
      FlickerPreventionCtx *ctx = flicker_prevention_ctx_new (roster, handle,
          item);

      item->flicker_prevention_id = g_timeout_add_seconds_full (
          G_PRIORITY_DEFAULT, 1, flicker_prevention_timeout, ctx,
          flicker_prevention_ctx_free);
    }
}

static void
roster_item_cancel_flicker_timeout (GabbleRosterItem *item)
{
  if (item->flicker_prevention_id != 0)
    {
      g_source_remove (item->flicker_prevention_id);
      item->flicker_prevention_id = 0;
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
got_roster_iq (GabbleRoster *roster,
    LmMessage *message)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *iq_node, *query_node;
  LmMessageSubType sub_type;
  const gchar *from;
  gboolean google_roster = FALSE;

  if (priv->list_channels == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  iq_node = lm_message_get_node (message);
  query_node = lm_message_node_get_child_with_namespace (iq_node, "query",
      NS_ROSTER);

  if (query_node == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  from = lm_message_node_get_attribute (message->node, "from");

  if (from != NULL)
    {
      TpHandle sender;

      sender = tp_handle_lookup (contact_repo, from, NULL, NULL);

      if (sender != conn->self_handle)
        {
           NODE_DEBUG (iq_node, "discarding roster IQ which is not from "
              "ourselves or the server");
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  if (priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER)
    {
      const char *gr_ext;

      gr_ext = lm_message_node_get_attribute_with_namespace (query_node, "ext",
          NS_GOOGLE_ROSTER);

      if (!tp_strdiff (gr_ext, GOOGLE_ROSTER_VERSION))
        google_roster = TRUE;
    }

  sub_type = lm_message_get_sub_type (message);

  /* if this is a result, it's from our initial query. if it's a set,
   * it's a roster push. either way, parse the items. */
  switch (sub_type)
    {
      TpIntSet *pub_add, *pub_rem,
               *sub_add, *sub_rem, *sub_rp,
               *stored_add, *stored_rem,
               *deny_add, *deny_rem;
      TpHandleSet *referenced_handles;
      GArray *removed;
      TpHandle handle;
      GabbleRosterChannel *pub_chan, *sub_chan, *chan;
      GHashTable *group_update_table;
      guint i;
      NodeIter j;

    case LM_MESSAGE_SUB_TYPE_RESULT:
    case LM_MESSAGE_SUB_TYPE_SET:
      /* asymmetry is because we don't get locally pending subscription
       * requests via <roster>, we get it via <presence> */
      pub_add = tp_intset_new ();
      pub_rem = tp_intset_new ();
      sub_add = tp_intset_new ();
      sub_rem = tp_intset_new ();
      sub_rp = tp_intset_new ();
      stored_add = tp_intset_new ();
      stored_rem = tp_intset_new ();
      group_update_table = g_hash_table_new_full (NULL, NULL, NULL,
          (GDestroyNotify)_group_mem_update_destroy);
      removed = g_array_new (FALSE, FALSE, sizeof (TpHandle));
      referenced_handles = tp_handle_set_new (contact_repo);

      if (google_roster)
        {
          deny_add = tp_intset_new ();
          deny_rem = tp_intset_new ();
        }
      else
        {
          deny_add = NULL;
          deny_rem = NULL;
        }

      /* we need these for preserving "fragile" local/remote pending states */
      pub_chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST,
          GABBLE_LIST_HANDLE_PUBLISH, NULL, NULL);
      sub_chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST,
          GABBLE_LIST_HANDLE_SUBSCRIBE, NULL, NULL);

      /* iterate every sub-node, which we expect to be <item>s */
      for (j = node_iter (query_node); j; j = node_iter_next (j))
        {
          const char *jid;
          GabbleRosterItem *item;
          LmMessageNode *item_node = node_iter_data (j);

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

          if (strchr (jid, '/') != NULL)
            {
              /* Avoid fd.o #12791 */
              NODE_DEBUG (item_node,
                  "item node has resource in jid, skipping");
              continue;
            }

          handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);
          if (handle == 0)
            {
              NODE_DEBUG (item_node, "item jid is malformed, skipping");
              continue;
            }
          /* transfer ownership of the reference to referenced_handles */
          tp_handle_set_add (referenced_handles, handle);
          tp_handle_unref (contact_repo, handle);

          item = _gabble_roster_item_update (roster, handle, item_node,
                                             group_update_table,
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
              if (google_roster && !_google_roster_item_should_keep (jid, item))
                tp_intset_add (pub_rem, handle);
              else
                tp_intset_add (pub_add, handle);
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_NONE:
            case GABBLE_ROSTER_SUBSCRIPTION_TO:
            case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
              /* publish channel is a bit odd, the roster item doesn't tell us
               * if someone is awaiting our approval - we get this via presence
               * type=subscribe, so we have to not remove them if they're
               * already local_pending in our publish channel */
              if (!tp_handle_set_is_member (pub_chan->group.local_pending,
                    handle))
                {
                  tp_intset_add (pub_rem, handle);
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
              if (google_roster && !_google_roster_item_should_keep (jid, item))
                tp_intset_add (sub_rem, handle);
              else
                tp_intset_add (sub_add, handle);

              roster_item_cancel_flicker_timeout (item);

              break;
            case GABBLE_ROSTER_SUBSCRIPTION_NONE:
            case GABBLE_ROSTER_SUBSCRIPTION_FROM:
              if (item->ask_subscribe)
                {
                  if (tp_handle_set_is_member (sub_chan->group.members, handle))
                    {
                      DEBUG("not letting gtalk demote member %u to pending",
                          handle);
                    }
                  else
                    {
                      if (item->flicker_prevention_id == 0)
                        roster_item_ensure_flicker_timeout (roster, handle, item);
                      else
                        roster_item_cancel_flicker_timeout (item);

                      tp_intset_add (sub_rp, handle);
                    }
                }
              else if (item->flicker_prevention_id == 0)
                {
                  /* We're not expecting this contact's ask=subscribe to
                   * flicker off and on again, so let's remove them immediately.
                   */
                  tp_intset_add (sub_rem, handle);
                }
              else
                {
                  DEBUG ("delaying removal of %s from pending", jid);
                }
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
              tp_intset_add (sub_rem, handle);
              break;
            default:
              g_assert_not_reached ();
            }

          /* handle stored list changes */
          switch (item->subscription)
            {
            case GABBLE_ROSTER_SUBSCRIPTION_NONE:
            case GABBLE_ROSTER_SUBSCRIPTION_TO:
            case GABBLE_ROSTER_SUBSCRIPTION_FROM:
            case GABBLE_ROSTER_SUBSCRIPTION_BOTH:
              if (google_roster &&
                  /* Don't hide contacts from stored if they're remote pending.
                   * This works around Google Talk flickering ask="subscribe"
                   * when you try to subscribe to someone; see
                   * test-google-roster.py.
                   */
                  !tp_handle_set_is_member (sub_chan->group.remote_pending,
                      handle) &&
                  !_google_roster_item_should_keep (jid, item))
                tp_intset_add (stored_rem, handle);
              else
                tp_intset_add (stored_add, handle);
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
              tp_intset_add (stored_rem, handle);
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
                    tp_intset_add (deny_add, handle);
                  else
                    tp_intset_add (deny_rem, handle);
                  break;
                case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
                  tp_intset_add (deny_rem, handle);
                  break;
                default:
                  g_assert_not_reached ();
                }
            }

          /* delay removing items from roster until signals have been emitted;
           * otherwise handles go out of scope!
           * FIXME: this probably isn't true any more because of
           * referenced_handles */
          if (GABBLE_ROSTER_SUBSCRIPTION_REMOVE == item->subscription)
            g_array_append_val (removed, handle);
        }

      DEBUG ("calling change members on publish channel");
      tp_group_mixin_change_members ((GObject *) pub_chan,
            "", pub_add, pub_rem, NULL, NULL, 0, 0);

      DEBUG ("calling change members on subscribe channel");
      tp_group_mixin_change_members ((GObject *) sub_chan,
            "", sub_add, sub_rem, NULL, sub_rp, 0, 0);

      handle = GABBLE_LIST_HANDLE_STORED;
      chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST, handle,
          NULL, NULL);

      DEBUG ("calling change members on stored channel");
      tp_group_mixin_change_members ((GObject *) chan,
            "", stored_add, stored_rem, NULL, NULL, 0, 0);

      DEBUG ("calling change members on any group channels");
      g_hash_table_foreach_remove (group_update_table, _update_group, roster);

      if (google_roster)
        {
          handle = GABBLE_LIST_HANDLE_DENY;
          chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST,
              handle, NULL, NULL);

          DEBUG ("calling change members on deny channel");
          tp_group_mixin_change_members ((GObject *) chan,
              "", deny_add, deny_rem, NULL, NULL, 0, 0);

          tp_intset_destroy (deny_add);
          tp_intset_destroy (deny_rem);
        }

      for (i = 0; i < removed->len; i++)
          _gabble_roster_item_remove (roster,
              g_array_index (removed, TpHandle, i));

      if (sub_type == LM_MESSAGE_SUB_TYPE_RESULT)
        {
          /* We are handling the response to our initial roster request. */
          GArray *members;

          tp_group_mixin_get_members ((GObject *) pub_chan, &members, NULL);
          /* If somebody's on our roster, and we haven't received presence
           * from them, we know they're offline. Let clients know that.
           */
          conn_presence_emit_presence_update (priv->conn, members);
        }

      tp_intset_destroy (pub_add);
      tp_intset_destroy (pub_rem);
      tp_intset_destroy (sub_add);
      tp_intset_destroy (sub_rem);
      tp_intset_destroy (sub_rp);
      tp_intset_destroy (stored_add);
      tp_intset_destroy (stored_rem);
      g_array_free (removed, TRUE);
      g_hash_table_destroy (group_update_table);
      tp_handle_set_destroy (referenced_handles);
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

static LmHandlerResult
gabble_roster_iq_cb (LmMessageHandler *handler,
                     LmConnection *lmconn,
                     LmMessage *message,
                     gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);
  GabbleRosterPrivate *priv = roster->priv;

  g_assert (lmconn == priv->conn->lmconn);

  return got_roster_iq (roster, message);
}

static void
_gabble_roster_send_presence_ack (GabbleRoster *roster,
                                  const gchar *from,
                                  LmMessageSubType sub_type,
                                  gboolean changed)
{
  GabbleRosterPrivate *priv = roster->priv;
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
      g_assert_not_reached ();
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
  GabbleRosterPrivate *priv = roster->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *pres_node, *child_node;
  const char *from;
  LmMessageSubType sub_type;
  TpIntSet *tmp;
  TpHandle handle, list_handle;
  const gchar *status_message = NULL;
  GabbleRosterChannel *chan = NULL;
  gboolean changed;
  LmHandlerResult ret;

  g_assert (lmconn == priv->conn->lmconn);

  if (priv->list_channels == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  pres_node = lm_message_get_node (message);

  from = lm_message_node_get_attribute (pres_node, "from");

  if (from == NULL)
    {
       NODE_DEBUG (pres_node, "presence stanza without from attribute, "
           "ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (message);

  handle = tp_handle_ensure (contact_repo, from, NULL, NULL);

  if (handle == 0)
    {
       NODE_DEBUG (pres_node, "ignoring presence from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (handle == conn->self_handle)
    {
      NODE_DEBUG (pres_node, "ignoring presence from ourselves on another "
          "resource");
      ret = LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
      goto OUT;
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

      tmp = tp_intset_new ();
      tp_intset_add (tmp, handle);

      list_handle = GABBLE_LIST_HANDLE_PUBLISH;
      chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST,
          list_handle, NULL, NULL);
      tp_group_mixin_change_members ((GObject *) chan, status_message,
          NULL, NULL, tmp, NULL, 0, 0);

      tp_intset_destroy (tmp);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE:
      DEBUG ("removing %s (handle %u) from the publish channel",
          from, handle);

      tmp = tp_intset_new ();
      tp_intset_add (tmp, handle);

      list_handle = GABBLE_LIST_HANDLE_PUBLISH;
      chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST,
          list_handle, NULL, NULL);
      changed = tp_group_mixin_change_members ((GObject *) chan,
          status_message, NULL, tmp, NULL, NULL, 0, 0);

      _gabble_roster_send_presence_ack (roster, from, sub_type, changed);

      tp_intset_destroy (tmp);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBED:
      DEBUG ("adding %s (handle %u) to the subscribe channel",
          from, handle);

      tmp = tp_intset_new ();
      tp_intset_add (tmp, handle);

      list_handle = GABBLE_LIST_HANDLE_SUBSCRIBE;
      chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST,
          list_handle, NULL, NULL);
      changed = tp_group_mixin_change_members ((GObject *) chan,
          status_message, tmp, NULL, NULL, NULL, 0, 0);

      _gabble_roster_send_presence_ack (roster, from, sub_type, changed);

      tp_intset_destroy (tmp);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED:
      DEBUG ("removing %s (handle %u) from the subscribe channel",
          from, handle);

      tmp = tp_intset_new ();
      tp_intset_add (tmp, handle);

      list_handle = GABBLE_LIST_HANDLE_SUBSCRIBE;
      chan = _gabble_roster_get_channel (roster, TP_HANDLE_TYPE_LIST,
          list_handle, NULL, NULL);
      changed = tp_group_mixin_change_members ((GObject *) chan,
          status_message, NULL, tmp, NULL, NULL, 0, 0);

      _gabble_roster_send_presence_ack (roster, from, sub_type, changed);

      tp_intset_destroy (tmp);

      ret = LM_HANDLER_RESULT_REMOVE_MESSAGE;
      break;
    default:
      ret = LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

OUT:
  tp_handle_unref (contact_repo, handle);
  return ret;
}

static gboolean
cancel_queued_requests (gpointer k,
                        gpointer v,
                        gpointer d)
{
  GabbleRoster *self = GABBLE_ROSTER (d);
  GSList *requests_satisfied = v;
  GSList *iter;

  requests_satisfied = g_slist_reverse (requests_satisfied);

  for (iter = requests_satisfied; iter != NULL; iter = iter->next)
    {
      tp_channel_manager_emit_request_failed (self,
          iter->data, TP_ERRORS, TP_ERROR_DISCONNECTED,
          "Unable to complete this channel request, we're disconnecting!");
    }

  g_slist_free (requests_satisfied);

  return TRUE;
}


static void
gabble_roster_close_all (GabbleRoster *self)
{
  GabbleRosterPrivate *priv = self->priv;

  DEBUG ("closing channels");

  if (priv->queued_requests != NULL)
    {
      g_hash_table_foreach_steal (priv->queued_requests,
          cancel_queued_requests, self);
      g_hash_table_destroy (priv->queued_requests);
      priv->queued_requests = NULL;
    }

  if (self->priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (self->priv->conn,
          self->priv->status_changed_id);
      self->priv->status_changed_id = 0;
    }

  /* Use a temporary variable because we don't want
   * roster_channel_closed_cb to remove the channel from the hash table a
   * second time */
  if (priv->group_channels != NULL)
    {
      GHashTable *t = priv->group_channels;
      priv->group_channels = NULL;
      g_hash_table_destroy (t);
    }

  if (priv->list_channels != NULL)
    {
      GHashTable *t = priv->list_channels;
      priv->list_channels = NULL;
      g_hash_table_destroy (t);
    }

  if (self->priv->iq_cb != NULL)
    {
      DEBUG ("removing callbacks");
      g_assert (self->priv->presence_cb != NULL);

      lm_connection_unregister_message_handler (self->priv->conn->lmconn,
          self->priv->iq_cb, LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (self->priv->iq_cb);
      self->priv->iq_cb = NULL;

      lm_connection_unregister_message_handler (self->priv->conn->lmconn,
          self->priv->presence_cb, LM_MESSAGE_TYPE_PRESENCE);
      lm_message_handler_unref (self->priv->presence_cb);
      self->priv->presence_cb = NULL;
    }
}

static LmHandlerResult
roster_received_cb (GabbleConnection *conn,
    LmMessage *sent_msg,
    LmMessage *reply_msg,
    GObject *roster_obj,
    gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);

  return got_roster_iq (roster, reply_msg);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleRoster *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      DEBUG ("adding callbacks");
      g_assert (self->priv->iq_cb == NULL);
      g_assert (self->priv->presence_cb == NULL);

      self->priv->iq_cb = lm_message_handler_new (gabble_roster_iq_cb,
          self, NULL);
      lm_connection_register_message_handler (self->priv->conn->lmconn,
          self->priv->iq_cb, LM_MESSAGE_TYPE_IQ,
          LM_HANDLER_PRIORITY_NORMAL);

      self->priv->presence_cb = lm_message_handler_new (
          gabble_roster_presence_cb, self, NULL);
      lm_connection_register_message_handler (self->priv->conn->lmconn,
          self->priv->presence_cb, LM_MESSAGE_TYPE_PRESENCE,
          LM_HANDLER_PRIORITY_LAST);

      break;

    case TP_CONNECTION_STATUS_CONNECTED:
        {
          LmMessage *message;

          DEBUG ("requesting roster");

          message = _gabble_roster_message_new (self, LM_MESSAGE_SUB_TYPE_GET,
              NULL);
          _gabble_connection_send_with_reply (self->priv->conn, message,
              roster_received_cb, G_OBJECT (self), self, NULL);
          lm_message_unref (message);
        }
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_roster_close_all (self);
      break;
    }
}


static GObject *
gabble_roster_constructor (GType type, guint n_props,
                           GObjectConstructParam *props)
{
  GObject *obj = G_OBJECT_CLASS (gabble_roster_parent_class)->
           constructor (type, n_props, props);
  GabbleRoster *self = GABBLE_ROSTER (obj);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

  return obj;
}


struct foreach_data {
    TpExportableChannelFunc func;
    gpointer data;
};

static void
_gabble_roster_foreach_channel_helper (gpointer key,
                                       gpointer value,
                                       gpointer data)
{
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);
  struct foreach_data *foreach = (struct foreach_data *) data;

  foreach->func (chan, foreach->data);
}

static void
gabble_roster_foreach_channel (TpChannelManager *manager,
                               TpExportableChannelFunc func,
                               gpointer data)
{
  GabbleRoster *roster = GABBLE_ROSTER (manager);
  GabbleRosterPrivate *priv = roster->priv;
  struct foreach_data foreach;

  foreach.func = func;
  foreach.data = data;

  g_hash_table_foreach (priv->group_channels,
      _gabble_roster_foreach_channel_helper, &foreach);
  g_hash_table_foreach (priv->list_channels,
      _gabble_roster_foreach_channel_helper, &foreach);
}


static void
gabble_roster_associate_request (GabbleRoster *self,
                                 GabbleRosterChannel *channel,
                                 gpointer request)
{
  GabbleRosterPrivate *priv = self->priv;
  GSList *list = g_hash_table_lookup (priv->queued_requests, channel);

  g_hash_table_steal (priv->queued_requests, channel);
  list = g_slist_prepend (list, request);
  g_hash_table_insert (priv->queued_requests, channel, list);
}


GabbleRoster *
gabble_roster_new (GabbleConnection *conn)
{
  g_return_val_if_fail (conn != NULL, NULL);

  return g_object_new (GABBLE_TYPE_ROSTER,
                       "connection", conn,
                       NULL);
}

static GabbleRosterItemEdit *
item_edit_new (void)
{
  GabbleRosterItemEdit *self = g_slice_new0 (GabbleRosterItemEdit);
  self->new_subscription = GABBLE_ROSTER_SUBSCRIPTION_INVALID;
  self->new_google_type = GOOGLE_ITEM_TYPE_INVALID;
  return self;
}

static void
item_edit_free (GabbleRosterItemEdit *edits)
{
  if (!edits)
    return;

  if (edits->add_to_groups)
    tp_handle_set_destroy (edits->add_to_groups);
  if (edits->remove_from_groups)
    tp_handle_set_destroy (edits->remove_from_groups);
  g_free (edits->new_name);
  g_slice_free (GabbleRosterItemEdit, edits);
}

static LmHandlerResult roster_edited_cb (GabbleConnection *conn,
                                         LmMessage *sent_msg,
                                         LmMessage *reply_msg,
                                         GObject *roster_obj,
                                         gpointer user_data);

/* Apply the unsent edits to the given roster item.
 *
 * \param roster The roster
 * \param contact The contact handle
 * \param item contact's roster item on roster
 */
static void
roster_item_apply_edits (GabbleRoster *roster,
                         TpHandle contact,
                         GabbleRosterItem *item)
{
  gboolean altered = FALSE, ret;
  GabbleRosterItem edited_item;
  TpIntSet *intset;
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *group_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_GROUP);
  GabbleRosterItemEdit *edits = item->unsent_edits;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  LmMessage *message;

  DEBUG ("Applying edits to contact#%u", contact);

  g_return_if_fail (item->unsent_edits);

  memcpy (&edited_item, item, sizeof (GabbleRosterItem));

#ifdef ENABLE_DEBUG
  if (DEBUGGING)
    {
      gchar *dump = _gabble_roster_item_dump (&edited_item);
      DEBUG ("Before, contact#%u: %s", contact, dump);
      g_free (dump);
    }
#endif

  if (edits->new_subscription != GABBLE_ROSTER_SUBSCRIPTION_INVALID
      && edits->new_subscription != item->subscription)
    {
      DEBUG ("Changing subscription from %d to %d",
             item->subscription, edits->new_subscription);
      altered = TRUE;
      edited_item.subscription = edits->new_subscription;
    }

  if (edits->new_name != NULL && tp_strdiff (item->name, edits->new_name))
    {
      DEBUG ("Changing name from %s to %s", item->name, edits->new_name);
      altered = TRUE;
      edited_item.name = edits->new_name;
    }

  if (edits->new_google_type != GOOGLE_ITEM_TYPE_INVALID
      && edits->new_google_type != item->google_type)
    {
      DEBUG ("Changing Google type from %d to %d", item->google_type,
             edits->new_google_type);
      altered = TRUE;
      edited_item.google_type = edits->new_google_type;
    }

  if (edits->add_to_groups || edits->remove_from_groups)
    {
#ifdef ENABLE_DEBUG
      if (DEBUGGING)
        {
          if (edits->add_to_groups)
            {
              GString *str = g_string_new ("Adding to groups: ");
              tp_intset_foreach (tp_handle_set_peek (edits->add_to_groups),
                                 _gabble_roster_item_dump_group, str);
              DEBUG("%s", g_string_free (str, FALSE));
            }
          else
            {
              DEBUG ("Not adding to any groups");
            }
          if (edits->remove_from_groups)
            {
              GString *str = g_string_new ("Removing from groups: ");
              tp_intset_foreach (tp_handle_set_peek (edits->remove_from_groups),
                                 _gabble_roster_item_dump_group, str);
              DEBUG("%s", g_string_free (str, FALSE));
            }
          else
            {
              DEBUG ("Not removing from any groups");
            }
        }
#endif
      edited_item.groups = tp_handle_set_new (group_repo);
      intset = tp_handle_set_update (edited_item.groups,
          tp_handle_set_peek (item->groups));
      tp_intset_destroy (intset);

      if (edits->add_to_groups)
        {
          intset = tp_handle_set_update (edited_item.groups,
              tp_handle_set_peek (edits->add_to_groups));
          if (tp_intset_size (intset) > 0)
            {
              altered = TRUE;
            }
          tp_intset_destroy (intset);
        }

      if (edits->remove_from_groups)
        {
          intset = tp_handle_set_difference_update (edited_item.groups,
              tp_handle_set_peek (edits->remove_from_groups));
          if (tp_intset_size (intset) > 0)
            {
              altered = TRUE;
            }
          tp_intset_destroy (intset);
        }
    }

#ifdef ENABLE_DEBUG
  if (DEBUGGING)
    {
      gchar *dump = _gabble_roster_item_dump (&edited_item);
      DEBUG ("After, contact#%u: %s", contact, dump);
      g_free (dump);
    }
#endif

  if (!altered)
    {
      DEBUG ("Contact#%u not actually changed - nothing to do", contact);
      item_edit_free (item->unsent_edits);
      item->unsent_edits = NULL;
      return;
    }

  DEBUG ("Contact#%u did change, sending message", contact);

  /* keep the handle valid until roster_edited_cb runs; it will do the unref */
  tp_handle_ref (contact_repo, contact);
  message = _gabble_roster_item_to_message (roster, contact, NULL,
      &edited_item);
  ret = _gabble_connection_send_with_reply (priv->conn,
      message, roster_edited_cb, G_OBJECT (roster),
      GUINT_TO_POINTER(contact), NULL);
  if (ret)
    {
      /* assume everything will be OK */
      item_edit_free (item->unsent_edits);
      item->unsent_edits = NULL;
    }
  else
    {
      /* FIXME: somehow have another try at it later? leave the
       * edits in unsent_edits for this purpose, anyway
       */
    }

  if (edited_item.groups != item->groups)
    {
      tp_handle_set_destroy (edited_item.groups);
    }
}

/* Called when an edit to the roster item has either succeeded or failed. */
static LmHandlerResult
roster_edited_cb (GabbleConnection *conn,
                  LmMessage *sent_msg,
                  LmMessage *reply_msg,
                  GObject *roster_obj,
                  gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (roster_obj);
  TpHandle contact = GPOINTER_TO_UINT (user_data);
  GabbleRosterItem *item = _gabble_roster_item_get (roster, contact);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);

  if (item->unsent_edits)
    {
      /* more edits have been queued since we sent this batch */
      roster_item_apply_edits (roster, contact, item);
    }

  tp_handle_unref (contact_repo, contact);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

GabbleRosterSubscription
gabble_roster_handle_get_subscription (GabbleRoster *roster,
                                       TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_val_if_fail (roster != NULL, GABBLE_ROSTER_SUBSCRIPTION_NONE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster),
      GABBLE_ROSTER_SUBSCRIPTION_NONE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      GABBLE_ROSTER_SUBSCRIPTION_NONE);

  item = g_hash_table_lookup (priv->items, GUINT_TO_POINTER (handle));

  if (NULL == item)
    return GABBLE_ROSTER_SUBSCRIPTION_NONE;

  return item->subscription;
}

gboolean
gabble_roster_handle_set_blocked (GabbleRoster *roster,
                                  TpHandle handle,
                                  gboolean blocked,
                                  GError **error)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;
  GoogleItemType orig_type;
  LmMessage *message;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);
  g_return_val_if_fail (priv->conn->features &
      GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER, FALSE);

  item = _gabble_roster_item_get (roster, handle);
  orig_type = item->google_type;

  if (item->unsent_edits)
    {
      DEBUG ("queue edit to contact#%u - change subscription to blocked=%d",
             handle, blocked);
      /* an edit is pending - make the change afterwards and
       * assume it'll be OK
       */
      if (blocked)
        {
          item->unsent_edits->new_google_type = GOOGLE_ITEM_TYPE_BLOCKED;
        }
      else
        {
          item->unsent_edits->new_google_type = GOOGLE_ITEM_TYPE_NORMAL;
        }
      return TRUE;
    }
  else
    {
      item->unsent_edits = item_edit_new ();
    }

  if (blocked == (orig_type == GOOGLE_ITEM_TYPE_BLOCKED))
    return TRUE;

  /* temporarily set the desired block state and generate a message */
  if (blocked)
    item->google_type = GOOGLE_ITEM_TYPE_BLOCKED;
  else
    item->google_type = GOOGLE_ITEM_TYPE_NORMAL;
  message = _gabble_roster_item_to_message (roster, handle, NULL, NULL);
  item->google_type = orig_type;

  /* keep the handle valid until roster_edited_cb runs; it will do the unref */
  tp_handle_ref (contact_repo, handle);
  ret = _gabble_connection_send_with_reply (priv->conn,
      message, roster_edited_cb, G_OBJECT (roster),
      GUINT_TO_POINTER(handle), error);

  lm_message_unref (message);

  if (blocked)
      gabble_presence_cache_really_remove (priv->conn->presence_cache, handle);

  return ret;
}

gboolean
gabble_roster_handle_has_entry (GabbleRoster *roster,
                                TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  item = g_hash_table_lookup (priv->items, GUINT_TO_POINTER (handle));

  return (NULL != item);
}

const gchar *
gabble_roster_handle_get_name (GabbleRoster *roster,
                               TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_val_if_fail (roster != NULL, NULL);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), NULL);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      NULL);

  item = g_hash_table_lookup (priv->items, GUINT_TO_POINTER (handle));

  if (NULL == item)
    return NULL;

  return item->name;
}

gboolean
gabble_roster_handle_set_name (GabbleRoster *roster,
                               TpHandle handle,
                               const gchar *name,
                               GError **error)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;
  LmMessage *message;
  LmMessageNode *item_node;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  item = _gabble_roster_item_get (roster, handle);
  g_return_val_if_fail (item != NULL, FALSE);

  if (item->unsent_edits)
    {
      DEBUG ("queue edit to contact#%u - change name to \"%s\"",
             handle, name);
      /* an edit is pending - make the change afterwards and
       * assume it'll be OK
       */
      g_free (item->unsent_edits->new_name);
      item->unsent_edits->new_name = g_strdup (name);
      return TRUE;
    }
  else
    {
      DEBUG ("immediate edit to contact#%u - change name to \"%s\"",
             handle, name);
      item->unsent_edits = item_edit_new ();
    }

  message = _gabble_roster_item_to_message (roster, handle, &item_node, NULL);

  lm_message_node_set_attribute (item_node, "name", name);

  /* keep the handle valid until roster_edited_cb runs; it will do the unref */
  tp_handle_ref (contact_repo, handle);
  ret = _gabble_connection_send_with_reply (priv->conn,
      message, roster_edited_cb, G_OBJECT (roster),
      GUINT_TO_POINTER(handle), error);

  lm_message_unref (message);

  return ret;
}

gboolean
gabble_roster_handle_remove (GabbleRoster *roster,
                             TpHandle handle,
                             GError **error)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;
  GabbleRosterSubscription subscription;
  LmMessage *message;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  item = _gabble_roster_item_get (roster, handle);

  if (item->unsent_edits)
    {
      DEBUG ("queue edit to contact#%u - change subscription to REMOVE",
             handle);
      /* an edit is pending - make the change afterwards and
       * assume it'll be OK
       */
      item->unsent_edits->new_subscription = GABBLE_ROSTER_SUBSCRIPTION_REMOVE;
      return TRUE;
    }
  else
    {
      DEBUG ("immediate edit to contact#%u - change subscription to REMOVE",
             handle);
      item->unsent_edits = item_edit_new ();
    }

  subscription = item->subscription;
  item->subscription = GABBLE_ROSTER_SUBSCRIPTION_REMOVE;

  /* keep the handle valid until roster_edited_cb runs; it will do the unref */
  tp_handle_ref (contact_repo, handle);
  message = _gabble_roster_item_to_message (roster, handle, NULL, NULL);
  ret = _gabble_connection_send_with_reply (priv->conn,
      message, roster_edited_cb, G_OBJECT (roster),
      GUINT_TO_POINTER(handle), error);
  lm_message_unref (message);

  item->subscription = subscription;

  return ret;
}

gboolean
gabble_roster_handle_add (GabbleRoster *roster,
                          TpHandle handle,
                          GError **error)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;
  LmMessage *message;
  gboolean do_add = FALSE;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  if (!gabble_roster_handle_has_entry (roster, handle))
      do_add = TRUE;

  item = _gabble_roster_item_get (roster, handle);

  if (item->google_type == GOOGLE_ITEM_TYPE_HIDDEN)
    do_add = TRUE;

  if (!do_add)
      return TRUE;

  if (item->unsent_edits)
    {
      DEBUG ("queue edit to contact#%u - change google type to NORMAL",
             handle);
      /* an edit is pending - make the change afterwards and
       * assume it'll be OK.
       */
      item->unsent_edits->new_google_type = GOOGLE_ITEM_TYPE_NORMAL;
      return TRUE;
    }
  else
    {
      DEBUG ("immediate edit to contact#%u - change google type to NORMAL",
             handle);
      if (item->google_type == GOOGLE_ITEM_TYPE_HIDDEN)
        item->google_type = GOOGLE_ITEM_TYPE_NORMAL;
      item->unsent_edits = item_edit_new ();
    }

  /* keep the handle valid until roster_edited_cb runs; it will do the unref */
  tp_handle_ref (contact_repo, handle);
  message = _gabble_roster_item_to_message (roster, handle, NULL, NULL);
  ret = _gabble_connection_send_with_reply (priv->conn,
      message, roster_edited_cb, G_OBJECT (roster),
      GUINT_TO_POINTER(handle), error);
  lm_message_unref (message);

  return ret;
}

gboolean
gabble_roster_handle_add_to_group (GabbleRoster *roster,
                                   TpHandle handle,
                                   TpHandle group,
                                   GError **error)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *group_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_GROUP);
  GabbleRosterItem *item;
  LmMessage *message;
  gboolean ret;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);
  g_return_val_if_fail (tp_handle_is_valid (group_repo, group, NULL),
      FALSE);

  item = _gabble_roster_item_get (roster, handle);

  if (item->unsent_edits)
    {
      DEBUG ("queue edit to contact#%u - add to group#%u", handle, group);
      /* an edit is pending - make the change afterwards and
       * assume it'll be OK
       */
      if (!item->unsent_edits->add_to_groups)
        {
          item->unsent_edits->add_to_groups = tp_handle_set_new (group_repo);
        }
      tp_handle_set_add (item->unsent_edits->add_to_groups, group);
      if (item->unsent_edits->remove_from_groups)
        {
          tp_handle_set_remove (item->unsent_edits->remove_from_groups, group);
        }
      return TRUE;
    }
  else
    {
      DEBUG ("immediate edit to contact#%u - add to group#%u", handle, group);
      item->unsent_edits = item_edit_new ();
    }

  tp_handle_set_add (item->groups, group);
  message = _gabble_roster_item_to_message (roster, handle, NULL, NULL);
  NODE_DEBUG (message->node, "Roster item as message");
  tp_handle_set_remove (item->groups, group);

  /* keep the handle valid until roster_edited_cb runs; it will do the unref */
  tp_handle_ref (contact_repo, handle);
  ret = _gabble_connection_send_with_reply (priv->conn,
      message, roster_edited_cb, G_OBJECT (roster),
      GUINT_TO_POINTER(handle), error);
  lm_message_unref (message);

  return ret;
}

gboolean
gabble_roster_handle_remove_from_group (GabbleRoster *roster,
                                        TpHandle handle,
                                        TpHandle group,
                                        GError **error)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *group_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_GROUP);
  GabbleRosterItem *item;
  LmMessage *message;
  gboolean ret, was_in_group;

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);
  g_return_val_if_fail (tp_handle_is_valid (group_repo, group, NULL),
      FALSE);

  item = _gabble_roster_item_get (roster, handle);

  if (item->unsent_edits)
    {
      DEBUG ("queue edit to contact#%u - remove from group#%u", handle, group);
      /* an edit is pending - make the change afterwards and
       * assume it'll be OK
       */
      if (!item->unsent_edits->remove_from_groups)
        {
          item->unsent_edits->remove_from_groups = tp_handle_set_new (
              group_repo);
        }
      tp_handle_set_add (item->unsent_edits->remove_from_groups, group);
      if (item->unsent_edits->add_to_groups)
        {
          tp_handle_set_remove (item->unsent_edits->add_to_groups, group);
        }
      return TRUE;
    }
  else
    {
      DEBUG ("immediate edit to contact#%u - remove from group#%u", handle,
          group);
      item->unsent_edits = item_edit_new ();
    }

  /* temporarily remove the handle from the set (taking a reference),
   * make the message, and put it back afterwards
   */
  tp_handle_ref (group_repo, group);
  was_in_group = tp_handle_set_remove (item->groups, group);
  message = _gabble_roster_item_to_message (roster, handle, NULL, NULL);
  if (was_in_group)
    tp_handle_set_add (item->groups, group);
  tp_handle_unref (group_repo, group);

  /* keep the handle valid until roster_edited_cb runs; it will do the unref */
  tp_handle_ref (contact_repo, handle);
  ret = _gabble_connection_send_with_reply (priv->conn,
      message, roster_edited_cb, G_OBJECT (roster),
      GUINT_TO_POINTER(handle), error);
  lm_message_unref (message);

  return ret;
}


static const gchar * const list_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};
static const gchar * const *group_channel_fixed_properties =
    list_channel_fixed_properties;

static const gchar * const list_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};
static const gchar * const *group_channel_allowed_properties =
    list_channel_allowed_properties;



static void
gabble_roster_foreach_channel_class (TpChannelManager *manager,
                                     TpChannelManagerChannelClassFunc func,
                                     gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value, *handle_type_value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

  handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  /* no uint value yet - we'll change it for each channel class */
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      handle_type_value);

  g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_GROUP);
  func (manager, table, group_channel_allowed_properties, user_data);

  /* FIXME: should these actually be in RequestableChannelClasses? You can't
   * usefully call CreateChannel on them, although EnsureChannel would be
   * OK. */
  /* FIXME: since we have a finite set of possible values for TargetHandle,
   * should we enumerate them all as separate channel classes? */
  g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_LIST);
  func (manager, table, list_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}


static gboolean
gabble_roster_request (GabbleRoster *self,
                       gpointer request_token,
                       GHashTable *request_properties,
                       gboolean require_new)
{
  gboolean created;
  GabbleRosterChannel *channel;
  TpHandleType handle_type;
  TpHandle handle;
  GError *error = NULL;
  TpHandleRepoIface *handle_repo;
  const gchar * const *fixed;
  const gchar * const *allowed;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
    return FALSE;

  handle_type = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL);

  if (handle_type != TP_HANDLE_TYPE_LIST &&
      handle_type != TP_HANDLE_TYPE_GROUP)
    return FALSE;

  handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, handle_type);

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  if (!tp_handle_is_valid (handle_repo, handle, &error))
    goto error;

  if (handle_type == TP_HANDLE_TYPE_LIST)
    {
      fixed = list_channel_fixed_properties;
      allowed = list_channel_allowed_properties;
    }
  else /* handle_type == TP_HANDLE_TYPE_GROUP */
    {
      fixed = group_channel_fixed_properties;
      allowed = group_channel_allowed_properties;
    }

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          fixed, allowed, &error))
    goto error;

  /* disallow "deny" channels if we don't have google:roster support */
  if (handle_type == TP_HANDLE_TYPE_LIST &&
      handle == GABBLE_LIST_HANDLE_DENY &&
      !(self->priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER))
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "This server does not have Google roster extensions, so there's "
          "no deny list");
      goto error;
    }

  channel = _gabble_roster_get_channel (self, handle_type, handle,
      &created, request_token);

  if (require_new && !created)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "That contact list has already been created (or requested)");
      goto error;
    }

  if (self->priv->roster_received)
    {
      if (!created)
        tp_channel_manager_emit_request_already_satisfied (self,
            request_token, TP_EXPORTABLE_CHANNEL (channel));
    }
  else
    {
      gabble_roster_associate_request (self, channel, request_token);
    }

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
gabble_roster_create_channel (TpChannelManager *manager,
                              gpointer request_token,
                              GHashTable *request_properties)
{
  GabbleRoster *self = GABBLE_ROSTER (manager);

  /* FIXME: the channel will come out with Requested=FALSE... is this
   * reasonable? Or should we just deny all attempts to CreateChannel() on this
   * factory? */

  return gabble_roster_request (self, request_token, request_properties,
      TRUE);
}


static gboolean
gabble_roster_request_channel (TpChannelManager *manager,
                               gpointer request_token,
                               GHashTable *request_properties)
{
  GabbleRoster *self = GABBLE_ROSTER (manager);

  return gabble_roster_request (self, request_token, request_properties,
      FALSE);
}


static gboolean
gabble_roster_ensure_channel (TpChannelManager *manager,
                              gpointer request_token,
                              GHashTable *request_properties)
{
  GabbleRoster *self = GABBLE_ROSTER (manager);

  return gabble_roster_request (self, request_token, request_properties,
      FALSE);
}


static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_roster_foreach_channel;
  iface->foreach_channel_class = gabble_roster_foreach_channel_class;
  iface->request_channel = gabble_roster_request_channel;
  iface->create_channel = gabble_roster_create_channel;
  iface->ensure_channel = gabble_roster_ensure_channel;
}
