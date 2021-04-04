/*
 * roster.c - Source for Gabble roster helper
 *
 * Copyright © 2006–2010 Collabora Ltd.
 * Copyright © 2006–2010 Nokia Corporation
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

#include <string.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>
#include <wocky/wocky.h>

#define DEBUG_FLAG GABBLE_DEBUG_ROSTER

#include "gabble/caps-channel-manager.h"
#include "conn-aliasing.h"
#include "conn-presence.h"
#include "conn-util.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "roster-cache.h"
#include "util.h"

#define GOOGLE_ROSTER_VERSION "2"

/* signal enum */
enum
{
  NICKNAMES_UPDATE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _GabbleRosterPrivate
{
  GabbleConnection *conn;
  gulong porter_available_id;
  gulong status_changed_id;
  GCancellable *cancel_on_disconnect;

  guint iq_cb;
  guint presence_cb;

  GHashTable *items;
  /* Used as a set of own (gchar *) */
  GHashTable *groups;

  /* set of contacts whose subscription requests will automatically be
   * accepted during this session */
  TpHandleSet *pre_authorized;

  gboolean received;
  GString *version;
  RosterCache *rcache;
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

typedef enum
{
  GABBLE_ROSTER_SUBSCRIPTION_NONE = 0,
  GABBLE_ROSTER_SUBSCRIPTION_FROM,
  GABBLE_ROSTER_SUBSCRIPTION_TO,
  GABBLE_ROSTER_SUBSCRIPTION_BOTH,
  GABBLE_ROSTER_SUBSCRIPTION_REMOVE,
  GABBLE_ROSTER_SUBSCRIPTION_INVALID,
} GabbleRosterSubscription;

typedef struct _GabbleRosterItemEdit GabbleRosterItemEdit;
struct _GabbleRosterItemEdit
{
  TpHandleRepoIface *contact_repo;
  TpHandle handle;

  /* if TRUE, we must create this roster item, so send the IQ even if we
   * don't appear to be changing anything */
  gboolean create;

  /* list of reffed GSimpleAsyncResult */
  GSList *results;

  /* if these are ..._INVALID, that means don't edit */
  GabbleRosterSubscription new_subscription;
  GoogleItemType new_google_type;

  /* owned by the GabbleRosterItemEdit. If NULL, that means don't edit... */
  gchar *new_name;
  /* if TRUE, disregard new_name and remove name='' from the roster item */
  gboolean remove_name;

  GHashTable *add_to_groups;
  GHashTable *remove_from_groups;
  gboolean remove_from_all_other_groups;
};

typedef struct _GabbleRosterItem GabbleRosterItem;
struct _GabbleRosterItem
{
  GabbleRosterSubscription subscription;
  gboolean ask_subscribe;
  GoogleItemType google_type;
  gchar *name;
  gchar *alias_for;
  GHashTable *groups;
  /* if TRUE, an edit attempt is already "in-flight" so we can't send off
   * edits immediately - instead, store them in unsent_edits */
  gboolean edits_in_flight;
  GabbleRosterItemEdit *unsent_edits;

  /* Might not match @subscription and @ask_subscribe exactly, in cases where
   * we're working around server breakage */
  TpSubscriptionState subscribe;
  TpSubscriptionState publish;
  gchar *publish_request;
  gboolean stored;
  gboolean blocked;

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

static void roster_item_cancel_flicker_timeout (GabbleRosterItem *item);
static void _gabble_roster_item_free (GabbleRosterItem *item);
static void item_edit_free (GabbleRosterItemEdit *edits);
static void gabble_roster_close_all (GabbleRoster *roster);

static void mutable_iface_init (TpMutableContactListInterface *iface);
static void blockable_iface_init (TpBlockableContactListInterface *iface);
static void contact_groups_iface_init (TpContactGroupListInterface *iface);
static void mutable_contact_groups_iface_init (
    TpMutableContactGroupListInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GabbleRoster, gabble_roster,
    TP_TYPE_BASE_CONTACT_LIST,
    G_IMPLEMENT_INTERFACE (TP_TYPE_MUTABLE_CONTACT_LIST,
      mutable_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CONTACT_GROUP_LIST,
      contact_groups_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_MUTABLE_CONTACT_GROUP_LIST,
      mutable_contact_groups_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_BLOCKABLE_CONTACT_LIST,
      blockable_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL))

static void
gabble_roster_init (GabbleRoster *obj)
{
  GabbleRosterPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      GABBLE_TYPE_ROSTER, GabbleRosterPrivate);

  obj->priv = priv;

  priv->items = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) _gabble_roster_item_free);
}

static void
gabble_roster_dispose (GObject *object)
{
  GabbleRoster *self = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");

  priv->dispose_has_run = TRUE;

  g_assert (priv->iq_cb == 0);
  g_assert (priv->presence_cb == 0);

  gabble_roster_close_all (self);
  g_assert (priv->groups == NULL);
  g_assert (priv->pre_authorized == NULL);

  if (G_OBJECT_CLASS (gabble_roster_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_parent_class)->dispose (object);
}

static void
gabble_roster_finalize (GObject *object)
{
  GabbleRoster *self = GABBLE_ROSTER (object);
  GabbleRosterPrivate *priv = self->priv;

  DEBUG ("called with %p", object);

  g_hash_table_unref (priv->items);

  if (priv->version != NULL)
    g_string_free (priv->version, TRUE);

  g_clear_object (&priv->rcache);

  G_OBJECT_CLASS (gabble_roster_parent_class)->finalize (object);
}

static void
_gabble_roster_item_free (GabbleRosterItem *item)
{
  g_assert (item != NULL);

  g_hash_table_unref (item->groups);
  item_edit_free (item->unsent_edits);
  g_free (item->name);
  g_free (item->alias_for);
  g_free (item->publish_request);

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
_parse_item_subscription (WockyNode *item_node)
{
  const gchar *subscription;

  g_assert (item_node != NULL);

  subscription = wocky_node_get_attribute (item_node, "subscription");

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

static GHashTable *
_parse_item_groups (WockyNode *item_node, TpBaseConnection *conn)
{
  GHashTable *groups = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, NULL);
  WockyNodeIter i;
  WockyNode *group_node;

  wocky_node_iter_init (&i, item_node, "group", NULL);
  while (wocky_node_iter_next (&i, &group_node))
    {
      const gchar *value = group_node->content;

      if (NULL == value)
        continue;

      g_hash_table_add (groups, g_strdup (value));
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
_parse_google_item_type (WockyNode *item_node)
{
  const gchar *google_type;

  g_assert (item_node != NULL);

  google_type = wocky_node_get_attribute_ns (item_node, "t", NS_GOOGLE_ROSTER);

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
_extract_google_alias_for (WockyNode *item_node)
{
  return g_strdup (wocky_node_get_attribute_ns (item_node,
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

  if (item->subscription != GABBLE_ROSTER_SUBSCRIPTION_NONE &&
      item->subscription != GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    return TRUE;

  /* discard anything else */
  DEBUG ("hiding %s: no subscription", jid);
  return FALSE;
}

static GabbleRosterItem *
_gabble_roster_item_lookup (GabbleRoster *roster,
    TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));

  return g_hash_table_lookup (priv->items, GUINT_TO_POINTER (handle));
}

static GabbleRosterItem *
_gabble_roster_item_ensure (GabbleRoster *roster,
    TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  item = _gabble_roster_item_lookup (roster, handle);

  g_assert (tp_handle_is_valid (contact_repo, handle, NULL));

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
      /* We may want it to be on the roster, but it's not there yet, so the
       * most accurate description of how to reach this state is "remove".
       * We'll keep this transient roster item for as long as it has edits
       * pending, or some other reason to stay around (a pending publish
       * request, for instance). */
      item->subscription = GABBLE_ROSTER_SUBSCRIPTION_REMOVE;
      item->subscribe = TP_SUBSCRIPTION_STATE_NO;
      item->publish = TP_SUBSCRIPTION_STATE_NO;
      item->name = alias;
      item->groups = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, NULL);
      g_hash_table_insert (priv->items, GUINT_TO_POINTER (handle), item);
    }

  return item;
}

static gboolean
_gabble_roster_item_maybe_remove (GabbleRoster *roster,
    TpHandle handle)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (tp_handle_is_valid (contact_repo, handle, NULL));

  item = _gabble_roster_item_lookup (roster, handle);
  g_return_val_if_fail (item != NULL, FALSE);

  /* don't remove items that are really on our server-side roster */
  if (item->subscription != GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    {
      DEBUG ("contact#%u is still on the roster", handle);
      return FALSE;
    }

  /* don't remove items that have edits in flight */
  if (item->edits_in_flight)
    {
      DEBUG ("contact#%u has edits in flight", handle);
      return FALSE;
    }

  /* don't remove transient items that represent publish/subscribe state */
  if (item->publish != TP_SUBSCRIPTION_STATE_NO)
    {
      DEBUG ("contact#%u has publish=%u", handle, item->publish);
      return FALSE;
    }

  if (item->subscribe != TP_SUBSCRIPTION_STATE_NO)
    {
      DEBUG ("contact#%u has subscribe=%u", handle, item->subscribe);
      return FALSE;
    }

  DEBUG ("removing contact#%u", handle);
  item = NULL;
  g_hash_table_remove (priv->items, GUINT_TO_POINTER (handle));
  return TRUE;
}

/* Add all the groups from @set to @add using @copy to duplicate them into
 * @set.
 * Returns (transfer full) the groups which have been actually added.
 */
static GHashTable *
group_set_update (GHashTable *set,
    GHashTable *add)
{
  GHashTable *added = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
  GHashTableIter iter;
  gpointer k;

  g_hash_table_iter_init (&iter, add);
  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      if (!g_hash_table_contains (set, k))
        {
          g_hash_table_add (set, g_strdup (k));
          g_hash_table_add (added, g_strdup (k));
        }
    }

  return added;
}

static void
group_set_difference_update (GHashTable *set,
    GHashTable *other)
{
  GHashTableIter iter;
  gpointer k;

  g_hash_table_iter_init (&iter, other);
  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      g_hash_table_remove (set, k);
    }
}

/* Returns (transfer full) the set of groups which are in @left and not
 * in @right */
static GHashTable *
group_set_difference (GHashTable *left,
    GHashTable *right)
{
  GHashTable *diff = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
  GHashTableIter iter;
  gpointer k;

  g_hash_table_iter_init (&iter, left);
  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      if (!g_hash_table_contains (right, k))
        g_hash_table_add (diff, g_strdup (k));
    }

  return diff;
}

static gboolean
group_set_is_equal (GHashTable *left,
    GHashTable *right)
{
  GHashTableIter iter;
  gpointer k;

  if (g_hash_table_size (left) != g_hash_table_size (right))
    return FALSE;

  g_hash_table_iter_init (&iter, left);
  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      if (!g_hash_table_contains (right, k))
        return FALSE;
    }

  return TRUE;
}

static GabbleRosterItem *
_gabble_roster_item_update (GabbleRoster *roster,
                            TpHandle contact_handle,
                            WockyNode *node,
                            gboolean google_roster_mode,
                            gboolean *nickname_updated)
{
  GabbleRosterPrivate *priv = roster->priv;
  GabbleRosterItem *item;
  const gchar *ask, *name;
  GHashTable *new_groups, *removed_from, *added_to;
  TpBaseContactList *base = (TpBaseContactList *) roster;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (tp_handle_is_valid (contact_repo, contact_handle, NULL));
  g_assert (node != NULL);

  item = _gabble_roster_item_ensure (roster, contact_handle);

  item->subscription = _parse_item_subscription (node);

  ask = wocky_node_get_attribute (node, "ask");
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
    name = wocky_node_get_attribute (node, "name");

  if (tp_strdiff (item->name, name))
    {
      g_free (item->name);
      item->name = g_strdup (name);

      DEBUG ("name for contact#%u changed to %s", contact_handle,
          name);

      *nickname_updated = TRUE;
    }
  else
    {
      *nickname_updated = FALSE;
    }

  new_groups = _parse_item_groups (node,
      (TpBaseConnection *) priv->conn);

  removed_from = group_set_difference (item->groups, new_groups);
  added_to = group_set_update (item->groups, new_groups);
  group_set_difference_update (item->groups, removed_from);

  if (roster->priv->groups != NULL)
    {
      GHashTable *created_groups;

      created_groups = group_set_update (roster->priv->groups, new_groups);

      /* we don't need to do this work if TpBaseContactList will just be
       * ignoring it, as it will before we've received the roster */
      if (tp_base_contact_list_get_state ((TpBaseContactList *) roster,
            NULL) == TP_CONTACT_LIST_STATE_SUCCESS &&
          g_hash_table_size (created_groups) > 0)
        {
          GPtrArray *strv = g_ptr_array_sized_new (g_hash_table_size (
                created_groups));
          GHashTableIter iter;
          gpointer k;

          g_hash_table_iter_init (&iter, created_groups);
          while (g_hash_table_iter_next (&iter, &k, NULL))
            {
              const gchar *group_name = k;

              DEBUG ("Group was just created: '%s'", group_name);
              g_ptr_array_add (strv, (gchar *) group_name);
            }

          tp_base_contact_list_groups_created ((TpBaseContactList *) roster,
              (const gchar * const *) strv->pdata, strv->len);

          g_ptr_array_unref (strv);
        }

      tp_clear_pointer (&created_groups, g_hash_table_unref);
    }

  /* We emit one GroupsChanged signal per contact, because that's most natural
   * for XMPP, where we usually get a roster push for a single contact.
   *
   * The exception is when we're receiving our initial roster, but until we do
   * that we don't need to emit GroupsChanged anyway; TpBaseContactList will
   * recover state. */
  if (tp_base_contact_list_get_state (base, NULL) ==
      TP_CONTACT_LIST_STATE_SUCCESS &&
      (g_hash_table_size (added_to) > 0 ||
       g_hash_table_size (removed_from) > 0))
    {
      GPtrArray *added_names = g_ptr_array_sized_new (
          g_hash_table_size (added_to));
      GPtrArray *removed_names = g_ptr_array_sized_new (
          g_hash_table_size (removed_from));
      TpHandleSet *the_contact = tp_handle_set_new (contact_repo);
      GHashTableIter iter;
      gpointer k;

      tp_handle_set_add (the_contact, contact_handle);

      g_hash_table_iter_init (&iter, added_to);
      while (g_hash_table_iter_next (&iter, &k, NULL))
        {
          const gchar *group_name = k;

          DEBUG ("Contact #%u added to group '%s'", contact_handle, group_name);
          g_ptr_array_add (added_names, (gchar *) group_name);
        }

      g_hash_table_iter_init (&iter, removed_from);
      while (g_hash_table_iter_next (&iter, &k, NULL))
        {
          const gchar *group_name = k;

          DEBUG ("Contact #%u removed from group '%s'", contact_handle,
              group_name);
          g_ptr_array_add (removed_names, (gchar *) group_name);
        }

      tp_base_contact_list_groups_changed ((TpBaseContactList *) roster,
          the_contact,
          (const gchar * const *) added_names->pdata, added_names->len,
          (const gchar * const *) removed_names->pdata, removed_names->len);
    }

  g_hash_table_unref (added_to);
  g_hash_table_unref (removed_from);
  g_hash_table_unref (new_groups);

  return item;
}

static void
_gabble_roster_item_dump_group (gpointer k,
    gpointer v,
    gpointer user_data)
{
  const gchar *group = k;

  g_string_append_printf ((GString *) user_data, "group '%s'", group);
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
      g_hash_table_foreach (item->groups, _gabble_roster_item_dump_group, str);
    }

  return g_string_free (str, FALSE);
}

static WockyStanza *
_gabble_roster_message_new (GabbleRoster *roster,
                            WockyStanzaSubType sub_type,
                            WockyNode **query_return)
{
  GabbleRosterPrivate *priv = roster->priv;
  WockyStanza *message;
  WockyNode *query_node;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));

  message = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      sub_type, NULL, NULL,
        '(', "query",
          ':', WOCKY_XMPP_NS_ROSTER,
          '*', &query_node,
        ')',
      NULL);

  if (query_return != NULL)
    *query_return = query_node;

  if (priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER)
    {
      GQuark gr = g_quark_from_static_string (NS_GOOGLE_ROSTER);

      wocky_node_attribute_ns_set_prefix (gr, "gr");
      wocky_node_set_attribute_ns (query_node, "ext", GOOGLE_ROSTER_VERSION,
          NS_GOOGLE_ROSTER);
      wocky_node_set_attribute_ns (query_node, "include", "all",
          NS_GOOGLE_ROSTER);
    }

  if (priv->rcache && sub_type == WOCKY_STANZA_SUB_TYPE_GET)
      wocky_node_set_attribute (query_node, "ver",
        ((priv->version) ? priv->version->str : ""));

  return message;
}


static void
_gabble_roster_item_put_group_in_message (gpointer k,
    gpointer v,
    gpointer user_data)
{
  const char *name = k;
  WockyNode *item_node = user_data;

  wocky_node_add_child_with_content (item_node, "group", name);
}

/*
 * _gabble_roster_item_to_message:
 * @roster: the roster
 * @item: the state we would like the contact's roster item to have (*not*
 *  the state it currently has!)
 * @handle: a contact
 *
 * Returns: the necessary IQ to change @handle's state to match that of @item
 */
static WockyStanza *
_gabble_roster_item_to_message (GabbleRoster *roster,
                                TpHandle handle,
                                GabbleRosterItem *item)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  WockyStanza *message;
  WockyNode *query_node, *item_node;
  const gchar *jid;

  g_assert (roster != NULL);
  g_assert (GABBLE_IS_ROSTER (roster));
  g_assert (tp_handle_is_valid (contact_repo, handle, NULL));
  g_assert (item != NULL);

  message = _gabble_roster_message_new (roster, WOCKY_STANZA_SUB_TYPE_SET,
      &query_node);

  item_node = wocky_node_add_child (query_node, "item");

  jid = tp_handle_inspect (contact_repo, handle);
  wocky_node_set_attribute (item_node, "jid", jid);

  if (item->subscription != GABBLE_ROSTER_SUBSCRIPTION_NONE)
    {
      const gchar *subscription =  _subscription_to_string (item->subscription);
      wocky_node_set_attribute (item_node, "subscription", subscription);
    }

  if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    goto DONE;

  if ((priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER) &&
      item->google_type != GOOGLE_ITEM_TYPE_NORMAL)
    {
      GQuark gr = g_quark_from_static_string (NS_GOOGLE_ROSTER);

      wocky_node_attribute_ns_set_prefix (gr, "gr");
      wocky_node_set_attribute_ns (item_node, "t",
          _google_item_type_to_string (item->google_type), NS_GOOGLE_ROSTER);
    }

  if (item->ask_subscribe)
    wocky_node_set_attribute (item_node, "ask", "subscribe");

  if (item->name)
    wocky_node_set_attribute (item_node, "name", item->name);

  if (item->groups)
    {
      g_hash_table_foreach (item->groups,
          _gabble_roster_item_put_group_in_message, item_node);
    }

DONE:
  return message;
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

  if ((item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE ||
        item->subscription == GABBLE_ROSTER_SUBSCRIPTION_NONE)
      && !item->ask_subscribe)
    {
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) ctx->roster->priv->conn,
          TP_HANDLE_TYPE_CONTACT);
      TpHandleSet *rem = tp_handle_set_new (contact_repo);

      DEBUG ("removing %u from subscribe", ctx->handle);
      item->subscribe = TP_SUBSCRIPTION_STATE_NO;
      tp_handle_set_add (rem, ctx->handle);
      tp_base_contact_list_contacts_changed ((TpBaseContactList *) ctx->roster,
          rem, NULL);
      tp_handle_set_destroy (rem);
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

static gboolean
roster_item_set_publish (GabbleRosterItem *item,
    TpSubscriptionState publish,
    const gchar *request)
{
  gboolean changed = FALSE;

  g_assert (publish == TP_SUBSCRIPTION_STATE_ASK || request == NULL);

  if (item->publish != publish)
    changed = TRUE;

  item->publish = publish;

  if (tp_strdiff (item->publish_request, request))
    {
      changed = TRUE;
      g_free (item->publish_request);
      item->publish_request = g_strdup (request);
    }

  return changed;
}

static gboolean
roster_item_set_subscribe (GabbleRosterItem *item,
    TpSubscriptionState subscribe)
{
  if (item->subscribe != subscribe)
    {
      item->subscribe = subscribe;
      return TRUE;
    }

  return FALSE;
}

static gboolean
is_google_roster_push (
    GabbleRoster *roster,
    WockyNode *query_node)
{
  if (roster->priv->conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER)
    {
      const char *gr_ext = wocky_node_get_attribute_ns (
          query_node, "ext", NS_GOOGLE_ROSTER);

      if (!tp_strdiff (gr_ext, GOOGLE_ROSTER_VERSION))
        return TRUE;
    }

  return FALSE;
}

/**
 * validate_roster_item:
 * @contact_repo: the handle repository for contacts
 * @item_node: an <item> child of a <query xmlns='jabber:iq:roster'>
 * @jid_out: location at which to store the roster item's jid, borrowed from
 *           @item_node, if the item is valid.
 *
 * Returns: a reference to a handle for the roster item, or 0 if the item seems
 *          to be malformed.
 */
static TpHandle
validate_roster_item (
    TpHandleRepoIface *contact_repo,
    WockyNode *item_node,
    const gchar **jid_out)
{
  const gchar *jid;
  TpHandle handle;

  jid = wocky_node_get_attribute (item_node, "jid");
  if (!jid)
    {
      NODE_DEBUG (item_node, "item node has no jid, skipping");
      return 0;
    }

  if (strchr (jid, '/') != NULL)
    {
      /* Avoid fd.o #12791 */
      NODE_DEBUG (item_node,
          "item node has resource in jid, skipping");
      return 0;
    }

  handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);
  if (handle == 0)
    {
      NODE_DEBUG (item_node, "item jid is malformed, skipping");
      return 0;
    }

  *jid_out = jid;
  return handle;
}

/*
 * process_roster:
 * @roster: a roster object
 * @query_node: a &lt;query xmlns='jabber:iq:roster'/&gt; node
 *
 * Processes an incoming roster push.
 */
static void
process_roster (
    GabbleRoster *roster,
    WockyNode *query_node)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  GArray *updated_nicknames = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  const gchar *ver;

  /* asymmetry is because we don't get locally pending subscription
   * requests via <roster>, we get it via <presence> */
  TpHandleSet *changed = tp_handle_set_new (contact_repo);
  TpHandleSet *removed = tp_handle_set_new (contact_repo);
  /* We may not have a deny list */
  TpHandleSet *blocking_changed;
  TpHandleSet *referenced_handles = tp_handle_set_new (contact_repo);

  gboolean google_roster = is_google_roster_push (roster, query_node);
  WockyNodeIter j;
  WockyNode *item_node;

  if (DEBUGGING)
    {
      gchar *dump = wocky_node_to_string (query_node);
      DEBUG ("Re-serialized roster: %s", dump);
      g_free (dump);
    }

  ver = wocky_node_get_attribute (query_node, "ver");
  if (ver != NULL)
    {
      DEBUG("Received roster version %s", ver);
      if (priv->version == NULL)
        priv->version = g_string_new (ver);
      else
        priv->version = g_string_assign (priv->version, ver);
    }

  if (google_roster)
    blocking_changed = tp_handle_set_new (contact_repo);
  else
    blocking_changed = NULL;

  /* iterate every <item> sub-node */
  wocky_node_iter_init (&j, query_node, "item", NULL);
  while (wocky_node_iter_next (&j, &item_node))
    {
      const char *jid;
      TpHandle handle;
      GabbleRosterItem *item;
      gboolean nickname_updated;

      handle = validate_roster_item (contact_repo, item_node, &jid);

      if (handle == 0)
        continue;

      /* transfer ownership of the reference to referenced_handles */
      tp_handle_set_add (referenced_handles, handle);

      item = _gabble_roster_item_update (roster, handle, item_node,
                                         google_roster, &nickname_updated);

      if (DEBUGGING)
        {
          gchar *dump = _gabble_roster_item_dump (item);
          DEBUG ("jid: %s, %s", jid, dump);
          g_free (dump);
        }

      if (nickname_updated)
        g_array_append_val (updated_nicknames, handle);

      /* handle publish list changes */
      switch (item->subscription)
        {
        case GABBLE_ROSTER_SUBSCRIPTION_FROM:
        case GABBLE_ROSTER_SUBSCRIPTION_BOTH:
          if (google_roster && !_google_roster_item_should_keep (jid, item))
            {
              if (roster_item_set_publish (item, TP_SUBSCRIPTION_STATE_NO, NULL))
                tp_handle_set_add (changed, handle);
            }
          else
            {
              if (roster_item_set_publish (item, TP_SUBSCRIPTION_STATE_YES, NULL))
                tp_handle_set_add (changed, handle);
            }
          break;
        case GABBLE_ROSTER_SUBSCRIPTION_NONE:
        case GABBLE_ROSTER_SUBSCRIPTION_TO:
        case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
          /* publish channel is a bit odd, the roster item doesn't tell us
           * if someone is awaiting our approval - we get this via presence
           * type=subscribe, so we have to not remove them if they're
           * already local_pending in our publish channel. NO -> NO is a
           * no-op, so YES -> NO is the only case left. */
          if (item->publish == TP_SUBSCRIPTION_STATE_YES)
            {
              tp_handle_set_add (changed, handle);
              roster_item_set_publish (item, TP_SUBSCRIPTION_STATE_NO, NULL);
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
            {
              if (roster_item_set_subscribe (item, TP_SUBSCRIPTION_STATE_NO))
                tp_handle_set_add (changed, handle);
            }
          else
            {
              if (roster_item_set_subscribe (item, TP_SUBSCRIPTION_STATE_YES))
                tp_handle_set_add (changed, handle);
            }

          roster_item_cancel_flicker_timeout (item);

          break;
        case GABBLE_ROSTER_SUBSCRIPTION_NONE:
        case GABBLE_ROSTER_SUBSCRIPTION_FROM:
          if (item->ask_subscribe)
            {
              if (item->subscribe == TP_SUBSCRIPTION_STATE_YES)
                {
                  DEBUG ("not letting gtalk demote member %u to pending",
                      handle);
                }
              else
                {
                  if (item->flicker_prevention_id == 0)
                    roster_item_ensure_flicker_timeout (roster, handle, item);
                  else
                    roster_item_cancel_flicker_timeout (item);

                  if (roster_item_set_subscribe (item, TP_SUBSCRIPTION_STATE_ASK))
                    tp_handle_set_add (changed, handle);
                }
            }
          else if (item->flicker_prevention_id == 0)
            {
              /* We're not expecting this contact's ask=subscribe to
               * flicker off and on again, so let's remove them immediately.
               */
              if (roster_item_set_subscribe (item, TP_SUBSCRIPTION_STATE_NO))
                tp_handle_set_add (changed, handle);
            }
          else
            {
              DEBUG ("delaying removal of %s from pending", jid);
            }
          break;
        case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
          if (roster_item_set_subscribe (item, TP_SUBSCRIPTION_STATE_NO))
            tp_handle_set_add (changed, handle);

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
              /* Don't hide contacts from stored if they're pending.
               * This works around two Google Talk issues:
               * - When you try to subscribe to someone, you get a flickering
               *   ask="subscribe";
               * - When somebody tries to subscribe to you, you get a presence
               *   with type="subscribe" followed by a roster update with
               *   subscribe="none".
               * See test-google-roster.py for more details.
               */
              item->subscribe != TP_SUBSCRIPTION_STATE_ASK &&
              item->publish != TP_SUBSCRIPTION_STATE_ASK &&
              !_google_roster_item_should_keep (jid, item))
            {
              tp_handle_set_remove (changed, handle);
              tp_handle_set_add (removed, handle);
              item->stored = FALSE;
            }
          else
            {
              if (!item->stored)
                tp_handle_set_add (changed, handle);

              item->stored = TRUE;
            }
          break;
        case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
          tp_handle_set_remove (changed, handle);

          if (item->stored)
            tp_handle_set_add (removed, handle);

          item->stored = FALSE;
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
                {
                  if (!item->blocked)
                    tp_handle_set_add (blocking_changed, handle);

                  item->blocked = TRUE;
                }
              else
                {
                  if (item->blocked)
                    tp_handle_set_add (blocking_changed, handle);

                  item->blocked = FALSE;
                }
              break;
            case GABBLE_ROSTER_SUBSCRIPTION_REMOVE:
              if (item->blocked)
                tp_handle_set_add (blocking_changed, handle);

              item->blocked = FALSE;
              break;
            default:
              g_assert_not_reached ();
            }
        }

      _gabble_roster_item_maybe_remove (roster, handle);
    }

  if (updated_nicknames->len > 0)
    g_signal_emit (roster, signals[NICKNAMES_UPDATE], 0, updated_nicknames);

  tp_base_contact_list_contacts_changed ((TpBaseContactList *) roster,
      changed, removed);

  if (google_roster)
    {
      tp_base_contact_list_contact_blocking_changed (
          (TpBaseContactList *) roster, blocking_changed);
      tp_handle_set_destroy (blocking_changed);
    }

  g_array_unref (updated_nicknames);
  tp_handle_set_destroy (changed);
  tp_handle_set_destroy (removed);
  tp_handle_set_destroy (referenced_handles);
}

static void roster_item_apply_edits (GabbleRoster *roster, TpHandle contact,
    GabbleRosterItem *item);

/**
 * got_roster_iq:
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with roster queries, and allows other handlers
 * if queries other than rosters are received.
 *
 * Returns: %TRUE if handled, %FALSE to allow more handlers
 */
static gboolean
got_roster_iq (GabbleRoster *roster,
    WockyStanza *message)
{
  GabbleRosterPrivate *priv = roster->priv;
  WockyNode *iq_node, *query_node;
  WockyStanzaSubType sub_type;

  if (priv->conn == NULL)
    return FALSE;

  iq_node = wocky_stanza_get_top_node (message);
  query_node = wocky_node_get_child_ns (iq_node, "query",
      WOCKY_XMPP_NS_ROSTER);

  if (query_node == NULL && roster->priv->version == NULL)
    return FALSE;

  wocky_stanza_get_type_info (message, NULL, &sub_type);

  /* if this is a result, it's from our initial query. if it's a set,
   * it's a roster push. otherwise, it's not for us. */
  if (sub_type != WOCKY_STANZA_SUB_TYPE_RESULT &&
      sub_type != WOCKY_STANZA_SUB_TYPE_SET)
    {
      NODE_DEBUG (iq_node, "unhandled roster IQ");
      return FALSE;
    }

  if (sub_type == WOCKY_STANZA_SUB_TYPE_RESULT && priv->received)
    {
      /* <https://bugs.freedesktop.org/show_bug.cgi?id=42186>: some super-buggy
       * XMPP server running on vk.com sends its reply to our roster query twice.
       */
      DEBUG ("The server sent replied to our roster query more than once! "
          "Ignoring this reply");
      return FALSE;
    }

  if (query_node)
    {
      const gchar *ver = wocky_node_get_attribute (query_node, "ver");
      process_roster (roster, query_node);

      if (roster->priv->rcache != NULL && ver != NULL && strlen (ver) != 0)
        {
          const gchar *user = conn_util_get_bare_self_jid (priv->conn);
          DEBUG ("updating roster cache for %s", user);
          if (!roster_cache_update_roster (roster->priv->rcache, user, query_node))
            WARNING ("Roster cache update failed."
                     " Check integrity of the cache storage and db.");
        }
    }

  if (sub_type == WOCKY_STANZA_SUB_TYPE_RESULT)
    {
      /* We are handling the response to our initial roster request. */
      GHashTableIter iter;
      gpointer k, v;
      GArray *members = g_array_sized_new (FALSE, FALSE, sizeof (guint),
          g_hash_table_size (roster->priv->items));
      GSList *edited_items = NULL;

      /* If we're subscribed to somebody (subscription=to or =both),
       * and we haven't received presence from them,
       * we know they're offline. Let clients know that.
       */
      g_hash_table_iter_init (&iter, roster->priv->items);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          GabbleRosterItem *item = v;
          TpHandle contact = GPOINTER_TO_UINT (k);
          GabblePresence *presence = gabble_presence_cache_get (
              priv->conn->presence_cache, contact);

          if (item->subscribe == TP_SUBSCRIPTION_STATE_YES &&
              (presence == NULL || presence->status == GABBLE_PRESENCE_UNKNOWN))
            {
              /* The contact might be in the presence cache with UNKNOWN
               * presence if we've received a message from them before the
               * roster arrived: an item is forcibly added to stash the
               * nickname which might have been included in the <message/> in
               * the presence cache. (This seems like a rather illogical place
               * to stash such nicknames—if anything, they should live in
               * GabbleImFactory—but there we go.)
               *
               * So if this is the case, we flip their status to OFFLINE. We
               * don't use gabble_presence_update() because we want to signal
               * all the unknown→offline transitions together.
               */
              if (presence != NULL)
                presence->status = GABBLE_PRESENCE_OFFLINE;

              g_array_append_val (members, contact);
            }

          if (item->unsent_edits != NULL)
            edited_items = g_slist_prepend (edited_items, item);
        }

      conn_presence_emit_presence_update (priv->conn, members);
      g_array_unref (members);

      /* The roster is now complete and we can emit signals... */
      tp_base_contact_list_set_list_received ((TpBaseContactList *) roster);
      priv->received = TRUE;

      /* ... and carry out any pending edits */
      for (;
          edited_items != NULL;
          edited_items = g_slist_delete_link (edited_items, edited_items))
        {
          GabbleRosterItem *item = edited_items->data;

          roster_item_apply_edits (roster, item->unsent_edits->handle, item);
        }
    }
  else /* WOCKY_STANZA_SUB_TYPE_SET */
    {
      /* acknowledge roster */
      _gabble_connection_acknowledge_set_iq (priv->conn, message);
    }

  return TRUE;
}

static gboolean
gabble_roster_iq_cb (WockyPorter *porter,
                     WockyStanza *message,
                     gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);

  return got_roster_iq (roster, message);
}

static void
_gabble_roster_send_presence_ack (GabbleRoster *roster,
                                  const gchar *from,
                                  WockyStanzaSubType sub_type,
                                  gboolean changed)
{
  GabbleRosterPrivate *priv = roster->priv;
  WockyStanza *reply;

  if (!changed)
    {
      DEBUG ("not sending ack to avoid loop with buggy server");
      return;
    }

  switch (sub_type)
    {
    case WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBE:
      sub_type = WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBED;
      break;
    case WOCKY_STANZA_SUB_TYPE_SUBSCRIBED:
      sub_type = WOCKY_STANZA_SUB_TYPE_SUBSCRIBE;
      break;
    case WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBED:
      sub_type = WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBE;
      break;
    default:
      g_assert_not_reached ();
      return;
    }

  reply = wocky_stanza_build (WOCKY_STANZA_TYPE_PRESENCE, sub_type,
      NULL, from,
      NULL); /* no content */

  _gabble_connection_send (priv->conn, reply, NULL);

  g_object_unref (reply);
}

static gboolean gabble_roster_handle_subscribed (GabbleRoster *roster,
    TpHandle handle, const gchar *message, GError **error);

static gboolean
gabble_roster_presence_cb (WockyPorter *porter,
                           WockyStanza *message,
                           gpointer user_data)
{
  GabbleRoster *roster = GABBLE_ROSTER (user_data);
  GabbleRosterPrivate *priv = roster->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  WockyNode *pres_node, *child_node;
  const char *from;
  WockyStanzaSubType sub_type;
  TpHandleSet *tmp;
  TpHandle handle;
  const gchar *status_message = NULL;
  gboolean ret;
  GabbleRosterItem *item;

  if (priv->conn == NULL)
    return FALSE;

  from = wocky_stanza_get_from (message);
  pres_node = wocky_stanza_get_top_node (message);

  if (from == NULL)
    {
       NODE_DEBUG (pres_node, "presence stanza without from attribute, "
           "ignoring");
      return FALSE;
    }

  wocky_stanza_get_type_info (message, NULL, &sub_type);

  handle = tp_handle_ensure (contact_repo, from, NULL, NULL);

  if (handle == 0)
    {
       NODE_DEBUG (pres_node, "ignoring presence from malformed jid");
      return FALSE;
    }

  if (handle == tp_base_connection_get_self_handle (conn))
    {
      NODE_DEBUG (pres_node, "ignoring presence from ourselves on another "
          "resource");
      return FALSE;
    }

  g_assert (handle != 0);

  child_node = wocky_node_get_child (pres_node, "status");

  if (child_node != NULL)
    status_message = child_node->content;

  item = _gabble_roster_item_ensure (roster, handle);

  switch (sub_type)
    {
    case WOCKY_STANZA_SUB_TYPE_SUBSCRIBE:
      DEBUG ("making %s (handle %u) local pending on the publish channel",
          from, handle);

      /* we re-emit ContactsChanged here, even if their state was already ASK
       * with the same message, because the fact that they've nagged us again
       * is significant */
      roster_item_set_publish (item, TP_SUBSCRIPTION_STATE_ASK, status_message);

      tmp = tp_handle_set_new (contact_repo);
      tp_handle_set_add (tmp, handle);
      tp_base_contact_list_contacts_changed ((TpBaseContactList *) roster,
          tmp, NULL);

      if (tp_handle_set_is_member (roster->priv->pre_authorized, handle))
        {
          GError *error = NULL;

          DEBUG ("%s (handle %u) was pre-authorized, will accept their "
              "request", from, handle);

          if (!gabble_roster_handle_subscribed (roster, handle, "", &error))
            {
              DEBUG ("Authorizing pre-authorized request failed: %s",
                  error->message);
              g_clear_error (&error);
            }
        }

      tp_handle_set_destroy (tmp);

      ret = TRUE;
      break;

    case WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBE:
      DEBUG ("removing %s (handle %u) from the publish channel",
          from, handle);

      if (item->publish == TP_SUBSCRIPTION_STATE_YES ||
          item->publish == TP_SUBSCRIPTION_STATE_ASK)
        {
          roster_item_set_publish (item,
              TP_SUBSCRIPTION_STATE_REMOVED_REMOTELY, NULL);

          tmp = tp_handle_set_new (contact_repo);
          tp_handle_set_add (tmp, handle);
          tp_base_contact_list_contacts_changed ((TpBaseContactList *) roster,
              tmp, NULL);
          tp_handle_set_destroy (tmp);

          _gabble_roster_send_presence_ack (roster, from, sub_type, TRUE);
        }
      else
        {
          _gabble_roster_send_presence_ack (roster, from, sub_type, FALSE);
        }

      ret = TRUE;
      break;

    case WOCKY_STANZA_SUB_TYPE_SUBSCRIBED:
      DEBUG ("adding %s (handle %u) to the subscribe channel",
          from, handle);

      if (item->subscribe != TP_SUBSCRIPTION_STATE_YES)
        {
          item->subscribe = TP_SUBSCRIPTION_STATE_YES;

          tmp = tp_handle_set_new (contact_repo);
          tp_handle_set_add (tmp, handle);
          tp_base_contact_list_contacts_changed ((TpBaseContactList *) roster,
              tmp, NULL);
          tp_handle_set_destroy (tmp);

          _gabble_roster_send_presence_ack (roster, from, sub_type, TRUE);
        }
      else
        {
          _gabble_roster_send_presence_ack (roster, from, sub_type, FALSE);
        }

      ret = TRUE;
      break;

    case WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBED:
      DEBUG ("removing %s (handle %u) from the subscribe channel",
          from, handle);

      if (item->subscribe == TP_SUBSCRIPTION_STATE_YES ||
          item->subscribe == TP_SUBSCRIPTION_STATE_ASK)
        {
          item->subscribe = TP_SUBSCRIPTION_STATE_REMOVED_REMOTELY;

          tmp = tp_handle_set_new (contact_repo);
          tp_handle_set_add (tmp, handle);
          tp_base_contact_list_contacts_changed ((TpBaseContactList *) roster,
              tmp, NULL);
          tp_handle_set_destroy (tmp);

          _gabble_roster_send_presence_ack (roster, from, sub_type, TRUE);
        }
      else
        {
          _gabble_roster_send_presence_ack (roster, from, sub_type, FALSE);
        }

      ret = TRUE;
      break;

    default:
      ret = FALSE;
    }

  return ret;
}

static void
gabble_roster_close_all (GabbleRoster *self)
{
  GabbleRosterPrivate *priv = self->priv;

  DEBUG ("closing channels");

  if (self->priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (self->priv->conn,
          self->priv->status_changed_id);
      self->priv->status_changed_id = 0;
    }

  if (self->priv->porter_available_id != 0)
    {
      g_signal_handler_disconnect (self->priv->conn,
          self->priv->porter_available_id);
      self->priv->porter_available_id = 0;
    }

  tp_clear_pointer (&priv->groups, g_hash_table_unref);
  tp_clear_pointer (&priv->pre_authorized, tp_handle_set_destroy);

  if (self->priv->cancel_on_disconnect != NULL)
    g_cancellable_cancel (self->priv->cancel_on_disconnect);

  tp_clear_object (&self->priv->cancel_on_disconnect);

  if (self->priv->iq_cb != 0)
    {
      WockyPorter *porter = gabble_connection_dup_porter (self->priv->conn);

      DEBUG ("removing callbacks");
      g_assert (self->priv->presence_cb != 0);

      wocky_porter_unregister_handler (porter, self->priv->iq_cb);
      self->priv->iq_cb = 0;

      wocky_porter_unregister_handler (porter, self->priv->presence_cb);
      self->priv->presence_cb = 0;

      g_object_unref (porter);
    }

  if (self->priv->version)
      self->priv->version = (void *) g_string_free (self->priv->version, TRUE);
}

static void
roster_received_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer weak_ref)
{
  GabbleRoster *self = tp_weak_ref_dup_object (weak_ref);

  if (self != NULL)
    {
      WockyStanza *response;
      GError *error = NULL;

      if (conn_util_send_iq_finish ((GabbleConnection *) source_object,
            result, &response, &error))
        {
          got_roster_iq (self, response);

          g_object_unref (response);
        }
      else
        {
          DEBUG ("%s", error->message);
          g_clear_error (&error);
        }
    }

  tp_clear_object (&self);
  tp_weak_ref_destroy (weak_ref);
}

static void
gabble_roster_porter_available_cb (GabbleConnection *conn,
    WockyPorter *porter,
    GabbleRoster *self)
{
  DEBUG ("adding callbacks");
  g_assert (self->priv->iq_cb == 0);
  g_assert (self->priv->presence_cb == 0);

  self->priv->iq_cb = wocky_c2s_porter_register_handler_from_server (
      WOCKY_C2S_PORTER (porter),
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, gabble_roster_iq_cb, self,
      '(', "query",
        ':', WOCKY_XMPP_NS_ROSTER,
      ')',
      NULL);

  self->priv->presence_cb = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_PRESENCE, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_MIN, gabble_roster_presence_cb, self,
      NULL);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleRoster *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTED:
        {
          WockyStanza *stanza;
          TpBaseContactList *base = TP_BASE_CONTACT_LIST (self);
          WockyNode *query = NULL;
          const gchar *user;

          self->priv->cancel_on_disconnect = g_cancellable_new ();

          if (tp_base_contact_list_get_download_at_connection (base))
            {
              DEBUG ("requesting roster");

              if (conn->features & GABBLE_CONNECTION_FEATURES_ROSTERVER)
                {
                  if (self->priv->rcache == NULL)
                      self->priv->rcache = roster_cache_dup_shared ();
                  user = conn_util_get_bare_self_jid (conn);
                  if (user)
                      query = roster_cache_get_roster (self->priv->rcache, user);
                  if (query)
                    {
                      process_roster (self, query);
                      wocky_node_free (query);
                    }
                }
              stanza = _gabble_roster_message_new (self, WOCKY_STANZA_SUB_TYPE_GET,
                  NULL);

              conn_util_send_iq_async (conn, stanza,
                  self->priv->cancel_on_disconnect,
                  roster_received_cb, tp_weak_ref_new (self, NULL, NULL));

              g_object_unref (stanza);
            }
          else
            {
              DEBUG ("don't request the roster because the property"
                     " ContactList.DownloadAtConnection is FALSE");
            }
        }
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_roster_close_all (self);
      break;
    }
}


static void
gabble_roster_constructed (GObject *obj)
{
  GabbleRoster *self = GABBLE_ROSTER (obj);
  TpBaseContactList *base = TP_BASE_CONTACT_LIST (obj);
  void (*chain_up)(GObject *) =
    ((GObjectClass *) gabble_roster_parent_class)->constructed;
  TpHandleRepoIface *contact_repo;

  if (chain_up != NULL)
    chain_up (obj);

  /* FIXME: This is not a strong reference because that would create a cycle.
   * I'd like to have a cyclic reference and break it at disconnect time,
   * like the contact list example in telepathy-glib does, but we can't do
   * that because the rest of Gabble assumes that the roster remains useful
   * until the bitter end (for instance, gabble_im_channel_dispose looks
   * at the contact's subscription). */
  self->priv->conn = GABBLE_CONNECTION (tp_base_contact_list_get_connection (
        base, NULL));
  g_assert (GABBLE_IS_CONNECTION (self->priv->conn));

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);
  self->priv->porter_available_id = g_signal_connect (self->priv->conn,
      "porter-available", G_CALLBACK (gabble_roster_porter_available_cb), obj);

  self->priv->groups = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
  self->priv->pre_authorized = tp_handle_set_new (contact_repo);
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
item_edit_new (TpHandleRepoIface *contact_repo,
    TpHandle handle)
{
  GabbleRosterItemEdit *self = g_slice_new0 (GabbleRosterItemEdit);

  self->contact_repo = g_object_ref (contact_repo);
  self->handle = handle;
  self->new_subscription = GABBLE_ROSTER_SUBSCRIPTION_INVALID;
  self->new_google_type = GOOGLE_ITEM_TYPE_INVALID;
  return self;
}

static void
item_edit_free (GabbleRosterItemEdit *edits)
{
  GSList *slist;

  if (!edits)
    return;

  edits->results = g_slist_reverse (edits->results);

  for (slist = edits->results; slist != NULL; slist = slist->next)
    {
      gabble_simple_async_countdown_dec (slist->data);
      g_object_unref (slist->data);
    }

  g_slist_free (edits->results);

  g_object_unref (edits->contact_repo);
  tp_clear_pointer (&edits->add_to_groups, g_hash_table_unref);
  tp_clear_pointer (&edits->remove_from_groups, g_hash_table_unref);
  g_free (edits->new_name);
  g_slice_free (GabbleRosterItemEdit, edits);
}

static void roster_edited_cb (GObject *, GAsyncResult *, gpointer);

static gboolean gabble_roster_handle_subscribed (GabbleRoster *roster,
    TpHandle handle,
    const gchar *message,
    GError **error);

/*
 * Cancel any subscriptions on @item by sending unsubscribe and/or
 * unsubscribed, as appropriate.
 */
static gboolean
roster_item_cancel_subscriptions (
    GabbleRoster *roster,
    TpHandle contact,
    GabbleRosterItem *item,
    GError **error)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) roster->priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *contact_id = tp_handle_inspect (contact_repo, contact);
  gboolean ret = TRUE;

  if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_FROM ||
      item->subscription == GABBLE_ROSTER_SUBSCRIPTION_BOTH)
    {
      DEBUG ("sending unsubscribed");
      ret = gabble_connection_send_presence (roster->priv->conn,
          WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBED, contact_id, NULL, error);
    }

  if (ret && (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_TO ||
        item->subscription == GABBLE_ROSTER_SUBSCRIPTION_BOTH))
    {
      DEBUG ("sending unsubscribe");
      ret = gabble_connection_send_presence (roster->priv->conn,
          WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBE, contact_id, NULL, error);
    }

  return ret;
}

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
  gboolean altered = FALSE;
  GabbleRosterItem edited_item;
  GabbleRosterPrivate *priv = roster->priv;
  GabbleRosterItemEdit *edits = item->unsent_edits;
  WockyStanza *message;

  if (!priv->received)
    {
      DEBUG ("Initial roster has not arrived yet, not editing it");
      return;
    }

  if (item->edits_in_flight)
    {
      DEBUG ("Edits still in flight for contact#%u, not applying more",
          contact);
      return;
    }

  if (edits == NULL)
    {
      DEBUG ("Nothing to do for contact#%u", contact);
      return;
    }

  DEBUG ("Applying edits to contact#%u", contact);

  memcpy (&edited_item, item, sizeof (GabbleRosterItem));

  if (DEBUGGING)
    {
      gchar *dump = _gabble_roster_item_dump (&edited_item);
      DEBUG ("Before, contact#%u: %s", contact, dump);
      g_free (dump);
    }

  if (edits->create)
    {
      DEBUG ("Creating new item");
      altered = TRUE;
    }

  if (edits->new_google_type != GOOGLE_ITEM_TYPE_INVALID
      && edits->new_google_type != item->google_type)
    {
      DEBUG ("Changing Google type from %d to %d", item->google_type,
             edits->new_google_type);
      altered = TRUE;
      edited_item.google_type = edits->new_google_type;
    }

  if (edits->new_subscription != GABBLE_ROSTER_SUBSCRIPTION_INVALID
      && edits->new_subscription != item->subscription)
    {
      /* Here we check the google_type of the *edited* item (as patched in the
       * block above) to deal correctly with a batch of edits containing both
       * (un)block and remove.
       */
      if (edits->new_subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE &&
          edited_item.google_type == GOOGLE_ITEM_TYPE_BLOCKED)
        {
          GError *error = NULL;

          /* If they're blocked, we can't just remove them from the roster,
           * because that would unblock them! So instead, we cancel both
           * subscription directions.
           */
          DEBUG ("contact is blocked; not removing");

          if (!roster_item_cancel_subscriptions (roster, contact, item,
                &error))
            {
              GSList *slist;

              /* in practice this error will probably be overwritten by one
               * from the IQ-set later, but if that succeeds for some reason,
               * we do want to signal error */
              for (slist = edits->results; slist != NULL; slist = slist->next)
                g_simple_async_result_set_from_error (slist->data, error);

              g_clear_error (&error);
            }
          /* deliberately not setting altered: we haven't altered the roster,
           * as such. */
        }
      else
        {
          DEBUG ("Changing subscription from %d to %d",
              item->subscription, edits->new_subscription);
          altered = TRUE;
          edited_item.subscription = edits->new_subscription;
        }
    }

  if (edits->remove_name)
    {
      DEBUG ("Removing name='' (was '%s')", item->name);
      altered = TRUE;
      edited_item.name = NULL;
    }
  else if (edits->new_name != NULL && tp_strdiff (item->name, edits->new_name))
    {
      DEBUG ("Changing name from %s to %s", item->name, edits->new_name);
      altered = TRUE;
      edited_item.name = edits->new_name;
    }

  if (edits->add_to_groups != NULL || edits->remove_from_groups != NULL ||
      edits->remove_from_all_other_groups)
    {
      if (DEBUGGING)
        {
          if (edits->add_to_groups != NULL)
            {
              GString *str = g_string_new ("Adding to groups: ");
              g_hash_table_foreach (edits->add_to_groups,
                  _gabble_roster_item_dump_group, str);
              DEBUG("%s", g_string_free (str, FALSE));
            }
          else
            {
              DEBUG ("Not adding to any groups");
            }

          if (edits->remove_from_all_other_groups)
            {
              DEBUG ("Removing from all other groups");
            }

          if (edits->remove_from_groups != NULL)
            {
              GString *str = g_string_new ("Removing from groups: ");
              g_hash_table_foreach (edits->remove_from_groups,
                  _gabble_roster_item_dump_group, str);
              DEBUG("%s", g_string_free (str, FALSE));
            }
          else
            {
              DEBUG ("Not removing from any groups");
            }
        }

      edited_item.groups = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, NULL);

      if (!edits->remove_from_all_other_groups)
        {
          GHashTable *added;

          added = group_set_update (edited_item.groups, item->groups);
          g_hash_table_unref (added);
        }

      if (edits->add_to_groups)
        {
          GHashTable *added;

          added = group_set_update (edited_item.groups,
              edits->add_to_groups);
          g_hash_table_unref (added);
        }

      if (edits->remove_from_groups)
        {
          group_set_difference_update (edited_item.groups,
              edits->remove_from_groups);
        }

      if (!group_set_is_equal (edited_item.groups, item->groups))
          altered = TRUE;
    }

  /* If we changed something about a transient GabbleRosterItem that
   * wasn't actually on our server-side roster yet, and we weren't actually
   * trying to delete it, then we need to create it as a side-effect. */
  if (altered &&
      edits->new_subscription != GABBLE_ROSTER_SUBSCRIPTION_REMOVE &&
      edited_item.subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    {
      edits->new_subscription = GABBLE_ROSTER_SUBSCRIPTION_NONE;
      edited_item.subscription = GABBLE_ROSTER_SUBSCRIPTION_NONE;
    }

  if (DEBUGGING)
    {
      gchar *dump = _gabble_roster_item_dump (&edited_item);
      DEBUG ("After, contact#%u: %s", contact, dump);
      g_free (dump);
    }

  if (!altered)
    {
      DEBUG ("Contact#%u not actually changed - nothing to do", contact);
      item_edit_free (item->unsent_edits);
      item->unsent_edits = NULL;
      return;
    }

  DEBUG ("Contact#%u did change, sending message", contact);


  message = _gabble_roster_item_to_message (roster, contact, &edited_item);

  /* we're sending the unsent edits - on success, roster_edited_cb will own
   * them */
  item->unsent_edits = NULL;
  item->edits_in_flight = TRUE;

  conn_util_send_iq_async (priv->conn, message,
      priv->cancel_on_disconnect, roster_edited_cb,
      edits);

  if (edits->new_google_type == GOOGLE_ITEM_TYPE_BLOCKED)
    gabble_presence_cache_really_remove (priv->conn->presence_cache, contact);

  if (edited_item.groups != item->groups)
    {
      g_hash_table_unref (edited_item.groups);
    }
}

/* Called when an edit to the roster item has either succeeded or failed. */
static void
roster_edited_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (source_object);
  GabbleRoster *roster = conn->roster;
  GabbleRosterItemEdit *edit = user_data;
  GabbleRosterItem *item = NULL;

  if (edit->results != NULL)
    {
      GError *tp_error = NULL;

      if (!conn_util_send_iq_finish (conn, result, NULL, &tp_error))
        {
          GSList *slist;

          for (slist = edit->results; slist != NULL; slist = slist->next)
            g_simple_async_result_set_from_error (slist->data, tp_error);

          g_clear_error (&tp_error);
        }
    }

  if (roster != NULL)
    item = _gabble_roster_item_lookup (roster, edit->handle);

  if (item != NULL)
    {
      item->edits_in_flight = FALSE;
      /* if more edits have been queued since we sent this batch, do them */
      roster_item_apply_edits (roster, edit->handle, item);

      if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE &&
          edit->new_subscription != GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
        {
          /* The server claims to have created the item, so we should believe
           * that the item exists, even though we haven't yet had the roster
           * push that should confirm it. This will result in
           * _gabble_roster_item_maybe_remove not removing it. */
          item->subscription = edit->new_subscription;
        }

      _gabble_roster_item_maybe_remove (roster, edit->handle);
    }

  item_edit_free (edit);
}

static void
gabble_roster_handle_set_blocked (GabbleRoster *roster,
    TpHandle handle,
    gboolean blocked,
    GSimpleAsyncResult *result)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_if_fail (roster != NULL);
  g_return_if_fail (GABBLE_IS_ROSTER (roster));
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));
  g_return_if_fail (priv->conn->features &
      GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER);

  item = _gabble_roster_item_ensure (roster, handle);

  if (item->unsent_edits == NULL)
    item->unsent_edits = item_edit_new (contact_repo, handle);

  DEBUG ("queue edit to contact#%u - change subscription to blocked=%d",
         handle, blocked);

  if (blocked)
    item->unsent_edits->new_google_type = GOOGLE_ITEM_TYPE_BLOCKED;
  else
    item->unsent_edits->new_google_type = GOOGLE_ITEM_TYPE_NORMAL;

  gabble_simple_async_countdown_inc (result);
  item->unsent_edits->results = g_slist_prepend (
      item->unsent_edits->results, g_object_ref (result));

  /* maybe we can apply the edit immediately? */
  roster_item_apply_edits (roster, handle, item);
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

  item = _gabble_roster_item_lookup (roster, handle);

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

  item = _gabble_roster_item_lookup (roster, handle);

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

  g_return_val_if_fail (roster != NULL, FALSE);
  g_return_val_if_fail (GABBLE_IS_ROSTER (roster), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  item = _gabble_roster_item_ensure (roster, handle);
  g_return_val_if_fail (item != NULL, FALSE);

  if (item->unsent_edits == NULL)
    item->unsent_edits = item_edit_new (contact_repo, handle);

  tp_clear_pointer (&item->unsent_edits->new_name, g_free);

  if (name == NULL)
    {
      DEBUG ("queue edit to contact#%u - remove name", handle);
      item->unsent_edits->remove_name = TRUE;
    }
  else
    {
      DEBUG ("queue edit to contact#%u - set name='%s'", handle, name);
      item->unsent_edits->remove_name = FALSE;
      item->unsent_edits->new_name = g_strdup (name);
    }

  /* maybe we can apply the edit immediately? */
  roster_item_apply_edits (roster, handle, item);

  /* FIXME: this method should be async so we don't need to assume
   * success */
  return TRUE;
}

static void
gabble_roster_handle_remove (GabbleRoster *roster,
                             TpHandle handle,
                             GSimpleAsyncResult *result)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpBaseContactList *base = (TpBaseContactList *) roster;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_if_fail (roster != NULL);
  g_return_if_fail (GABBLE_IS_ROSTER (roster));
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  item = _gabble_roster_item_lookup (roster, handle);

  if (item == NULL)
    return;

  /* If the contact is really stored on the server, deleting their roster item
   * is sufficient. If they're not, we might have some state resulting from
   * a publish request or remote removal or something. */
  if (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    {
      /* These will clear a status of REMOVED_REMOTELY or ASK */
      roster_item_set_publish (item, TP_SUBSCRIPTION_STATE_NO, NULL);
      roster_item_set_subscribe (item, TP_SUBSCRIPTION_STATE_NO);

      /* If there are no edits in-flight, we may just be able to delete the
       * contact list entry and return early. If there are edits in flight,
       * we should not return early: the in-flight edit might be
       * creating the roster item, so we need to queue up a second edit
       * that will delete it again. */
      if (_gabble_roster_item_maybe_remove (roster, handle))
        {
          TpHandleSet *removed = tp_handle_set_new (contact_repo);

          tp_handle_set_add (removed, handle);
          tp_base_contact_list_contacts_changed (base, NULL, removed);
          tp_handle_set_destroy (removed);
          return;
        }
    }

  if (item->unsent_edits == NULL)
    item->unsent_edits = item_edit_new (contact_repo, handle);

  DEBUG ("queue edit to contact#%u - change subscription to REMOVE",
         handle);
  item->unsent_edits->new_subscription = GABBLE_ROSTER_SUBSCRIPTION_REMOVE;
  gabble_simple_async_countdown_inc (result);
  item->unsent_edits->results = g_slist_prepend (
      item->unsent_edits->results, g_object_ref (result));

  /* maybe we can apply the edit immediately? */
  roster_item_apply_edits (roster, handle, item);
}

static void
gabble_roster_handle_add (GabbleRoster *roster,
                          TpHandle handle,
                          GSimpleAsyncResult *result)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;
  gboolean do_add = FALSE;

  g_return_if_fail (roster != NULL);
  g_return_if_fail (GABBLE_IS_ROSTER (roster));
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  if (!gabble_roster_handle_has_entry (roster, handle))
      do_add = TRUE;

  item = _gabble_roster_item_ensure (roster, handle);

  if (item->google_type == GOOGLE_ITEM_TYPE_HIDDEN ||
      item->subscription == GABBLE_ROSTER_SUBSCRIPTION_REMOVE)
    do_add = TRUE;

  if (!do_add)
      return;

  if (item->unsent_edits == NULL)
    item->unsent_edits = item_edit_new (contact_repo, handle);

  DEBUG ("queue edit to contact#%u - change google type to NORMAL",
         handle);
  item->unsent_edits->create = TRUE;
  item->unsent_edits->new_google_type = GOOGLE_ITEM_TYPE_NORMAL;

  if (result != NULL)
    {
      gabble_simple_async_countdown_inc (result);
      item->unsent_edits->results = g_slist_prepend (
          item->unsent_edits->results, g_object_ref (result));
    }

  /* maybe we can apply the edit immediately? */
  roster_item_apply_edits (roster, handle, item);
}

static void
gabble_roster_handle_add_to_group (GabbleRoster *roster,
    TpHandle handle,
    const gchar *group,
    GSimpleAsyncResult *result)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_if_fail (roster != NULL);
  g_return_if_fail (GABBLE_IS_ROSTER (roster));
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  item = _gabble_roster_item_ensure (roster, handle);

  if (item->unsent_edits == NULL)
    item->unsent_edits = item_edit_new (contact_repo, handle);

  DEBUG ("queue edit to contact#%u - add to group '%s'", handle, group);
  gabble_simple_async_countdown_inc (result);
  item->unsent_edits->results = g_slist_prepend (
      item->unsent_edits->results, g_object_ref (result));

  if (!item->unsent_edits->add_to_groups)
    {
      item->unsent_edits->add_to_groups = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, NULL);
    }

  g_hash_table_add (item->unsent_edits->add_to_groups, g_strdup (group));

  if (item->unsent_edits->remove_from_groups)
    {
      g_hash_table_remove (item->unsent_edits->remove_from_groups, group);
    }

  /* maybe we can apply the edit immediately? */
  roster_item_apply_edits (roster, handle, item);
}

static void
gabble_roster_handle_remove_from_group (GabbleRoster *roster,
    TpHandle handle,
    const gchar *group,
    GSimpleAsyncResult *result)
{
  GabbleRosterPrivate *priv = roster->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_if_fail (roster != NULL);
  g_return_if_fail (GABBLE_IS_ROSTER (roster));
  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  item = _gabble_roster_item_ensure (roster, handle);

  if (item->unsent_edits == NULL)
    item->unsent_edits = item_edit_new (contact_repo, handle);

  DEBUG ("queue edit to contact#%u - remove from group '%s'", handle, group);

  gabble_simple_async_countdown_inc (result);
  item->unsent_edits->results = g_slist_prepend (
      item->unsent_edits->results, g_object_ref (result));

  if (!item->unsent_edits->remove_from_groups)
    {
      item->unsent_edits->remove_from_groups = g_hash_table_new_full (
          g_str_hash, g_str_equal, g_free, NULL);
    }

  g_hash_table_add (item->unsent_edits->remove_from_groups, g_strdup (group));

  if (item->unsent_edits->add_to_groups)
    {
      g_hash_table_remove (item->unsent_edits->add_to_groups, group);
    }

  /* maybe we can apply the edit immediately? */
  roster_item_apply_edits (roster, handle, item);
}

static gboolean
gabble_roster_handle_subscribed (
    GabbleRoster *roster,
    TpHandle handle,
    const gchar *message,
    GError **error)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) roster->priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *contact_id = tp_handle_inspect (contact_repo, handle);

  /* send <presence type="subscribed"> */
  return gabble_connection_send_presence (roster->priv->conn,
      WOCKY_STANZA_SUB_TYPE_SUBSCRIBED, contact_id, message, error);
}

static TpHandleSet *
gabble_roster_dup_contacts (TpBaseContactList *base)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpHandleSet *set;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  GHashTableIter iter;
  gpointer k, v;

  set = tp_handle_set_new (contact_repo);

  g_hash_table_iter_init (&iter, self->priv->items);

  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      GabbleRosterItem *item = v;

      /* add all the interesting items */
      if (item->stored ||
          item->subscribe != TP_SUBSCRIPTION_STATE_NO ||
          item->publish != TP_SUBSCRIPTION_STATE_NO)
        tp_handle_set_add (set, GPOINTER_TO_UINT (k));
    }

  return set;
}

static void
gabble_roster_dup_states (TpBaseContactList *base,
    TpHandle contact,
    TpSubscriptionState *subscribe,
    TpSubscriptionState *publish,
    gchar **publish_request)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  GabbleRosterItem *item = _gabble_roster_item_lookup (self, contact);

  if (item == NULL)
    {
      if (subscribe != NULL)
        *subscribe = TP_SUBSCRIPTION_STATE_NO;

      if (publish != NULL)
        *publish = TP_SUBSCRIPTION_STATE_NO;

      if (publish_request != NULL)
        *publish_request = NULL;
    }
  else
    {
      if (subscribe != NULL)
        *subscribe = item->subscribe;

      if (publish != NULL)
        *publish = item->publish;

      if (publish_request != NULL)
        *publish_request = g_strdup (item->publish_request);
    }
}

typedef struct {
    GAsyncReadyCallback callback;
    gpointer user_data;
    TpHandleSet *contacts;
    gchar *message;
} SubscribeContext;

static void
gabble_roster_request_subscription_added_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (source);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  SubscribeContext *context = user_data;
  GError *error = NULL;
  TpIntsetFastIter iter;
  TpHandle contact;

  /* Now that we've added all the contacts, send off all the subscription
   * requests; stop if we hit an error. */

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (context->contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    {
      GabbleRosterItem *item = _gabble_roster_item_lookup (self, contact);
      const gchar *contact_id = tp_handle_inspect (contact_repo, contact);

      /* Note that we *do* send redundant requests if the contact is in
       * ask=subscribe state, since those have semantic value - nagging the
       * contact again. There's no point in requesting subscription if the
       * contact has already said yes, though. */
      if (item != NULL &&
          (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_TO ||
           item->subscription == GABBLE_ROSTER_SUBSCRIPTION_BOTH))
        {
          DEBUG ("Already subscribed to contact#%u '%s', not re-requesting",
              contact, contact_id);
          continue;
        }

      /* stop trying at the first NetworkError, on the assumption that it'll
       * be fatal */
      if (!gabble_connection_send_presence (self->priv->conn,
            WOCKY_STANZA_SUB_TYPE_SUBSCRIBE, contact_id, context->message,
            &error))
        break;
    }

  gabble_simple_async_succeed_or_fail_in_idle (self, context->callback,
      context->user_data, NULL, error);
  g_clear_error (&error);
  tp_clear_pointer (&context->contacts, tp_handle_set_destroy);
  g_free (context->message);
  g_slice_free (SubscribeContext, context);
}

static void
gabble_roster_request_subscription_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    const gchar *message,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  SubscribeContext *context = g_slice_new0 (SubscribeContext);
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      gabble_roster_request_subscription_added_cb, context,
      gabble_roster_request_subscription_async, 1);
  TpIntsetFastIter iter;
  TpHandle contact;

  /* Before subscribing, add items to the roster
   * (GTalk depends on this clearing the H flag) */
  context->contacts = tp_handle_set_copy (contacts);
  context->callback = callback;
  context->user_data = user_data;
  context->message = g_strdup (message);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    gabble_roster_handle_add (self, contact, result);

  /* When all of those edits have been applied, the callback will send the
   * <presence type='subscribe'> requests. */
  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
gabble_roster_authorize_publication_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpIntsetFastIter iter;
  TpHandle contact;
  GError *error = NULL;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    {
      GabbleRosterItem *item = _gabble_roster_item_lookup (self, contact);
      const gchar *contact_id = tp_handle_inspect (contact_repo, contact);

      if (item == NULL || item->publish == TP_SUBSCRIPTION_STATE_NO
          || item->publish == TP_SUBSCRIPTION_STATE_REMOVED_REMOTELY)
        {
          /* The contact didn't ask for our presence, so we can't usefully
           * send out <presence type='subscribed'/> (as per RFC3921 §9.2,
           * our server shouldn't forward it anyway). However, we can
           * remember this "pre-authorization" and use it later in the
           * session to auto-approve a subscription request. */
          DEBUG ("Noting that contact #%u '%s' is pre-authorized",
              contact, contact_id);
          tp_handle_set_add (self->priv->pre_authorized, contact);
        }
      else if (item->publish == TP_SUBSCRIPTION_STATE_ASK)
        {
          /* stop trying at the first NetworkError, on the assumption that
           * it'll be fatal */
          DEBUG ("Sending <presence type='subscribed'/> to contact#%u '%s'",
              contact, contact_id);
          if (!gabble_roster_handle_subscribed (self, contact, "", &error))
            break;
        }
      else
        {
          DEBUG ("contact #%u '%s' already has publish=Y, nothing to do",
              contact, contact_id);
        }
    }

  gabble_simple_async_succeed_or_fail_in_idle (self, callback, user_data,
      gabble_roster_authorize_publication_async, error);
  g_clear_error (&error);
}

static void
gabble_roster_store_contacts_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpIntsetFastIter iter;
  TpHandle contact;
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_store_contacts_async, 1);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    gabble_roster_handle_add (self, contact, result);

  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
gabble_roster_remove_contacts_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpIntsetFastIter iter;
  TpHandle contact;
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_request_subscription_async, 1);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    gabble_roster_handle_remove (self, contact, result);

  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
gabble_roster_unsubscribe_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleSet *changed = tp_handle_set_new (contact_repo);
  TpHandleSet *removed = tp_handle_set_new (contact_repo);
  TpIntsetFastIter iter;
  TpHandle contact;
  GError *error = NULL;

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    {
      const gchar *contact_id = tp_handle_inspect (contact_repo, contact);
      GabbleRosterItem *item = _gabble_roster_item_lookup (self, contact);

      if (item == NULL || item->subscribe == TP_SUBSCRIPTION_STATE_NO)
        {
          DEBUG ("contact #%u '%s' absent or has subscribe=N, nothing to do",
              contact, contact_id);
        }
      else if (item->subscribe == TP_SUBSCRIPTION_STATE_REMOVED_REMOTELY)
        {
          /* just acknowledge remote removal */
          DEBUG ("contact #%u '%s' had subscribe=R, moving to publish=N",
            contact, contact_id);
          roster_item_set_subscribe (item, TP_SUBSCRIPTION_STATE_NO);

          if (_gabble_roster_item_maybe_remove (self, contact))
            tp_handle_set_add (removed, contact);
          else
            tp_handle_set_add (changed, contact);
        }
      else
        {
          /* Deny a request (if ASK) or revoke previously-granted permission
           * (if YES). Stop trying at the first NetworkError, on the
           * assumption that it'll be fatal. Any changes will be signalled when
           * confirmed by a roster push. */
          DEBUG ("Sending <presence type='unsubscribe'/> to contact#%u '%s'",
              contact, contact_id);
          if (!gabble_connection_send_presence (self->priv->conn,
              WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBE, contact_id, "", &error))
            break;
        }
    }

  tp_base_contact_list_contacts_changed (base, changed, removed);
  gabble_simple_async_succeed_or_fail_in_idle (self, callback, user_data,
      gabble_roster_unsubscribe_async, error);
  g_clear_error (&error);
  tp_handle_set_destroy (changed);
  tp_handle_set_destroy (removed);
}

static void
gabble_roster_unpublish_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleSet *changed = tp_handle_set_new (contact_repo);
  TpHandleSet *removed = tp_handle_set_new (contact_repo);
  TpIntsetFastIter iter;
  TpHandle contact;
  GError *error = NULL;

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    {
      const gchar *contact_id = tp_handle_inspect (contact_repo, contact);
      GabbleRosterItem *item = _gabble_roster_item_lookup (self, contact);

      /* If moving from YES to NO, the roster callback will make the change
       * visible to D-Bus when it actually takes effect.
       *
       * If moving from ASK to NO, remove it from publish:local_pending here,
       * because the roster callback doesn't know if it can
       * (subscription='none' is used both during request and when it's
       * rejected).
       *
       * If moving from REMOVED_REMOTELY to NO, there's no real change at the
       * XMPP level, so this is our only chance to make the change visible. */
      if (item != NULL &&
          (item->publish == TP_SUBSCRIPTION_STATE_ASK ||
           item->publish == TP_SUBSCRIPTION_STATE_REMOVED_REMOTELY))
        {
          if (item->publish == TP_SUBSCRIPTION_STATE_ASK)
            DEBUG ("contact #%u '%s' had publish=A, moving to publish=N",
              contact, contact_id);
          else
            DEBUG ("contact #%u '%s' had publish=R, moving to publish=N",
              contact, contact_id);

          roster_item_set_publish (item, TP_SUBSCRIPTION_STATE_NO, NULL);

          if (_gabble_roster_item_maybe_remove (self, contact))
            tp_handle_set_add (removed, contact);
          else
            tp_handle_set_add (changed, contact);
        }

      if (item == NULL || item->publish == TP_SUBSCRIPTION_STATE_NO)
        {
          DEBUG ("contact #%u '%s' already has publish=N, nothing to do",
              contact, contact_id);
        }
      else
        {
          /* stop trying at the first NetworkError, on the assumption that
           * it'll be fatal */
          DEBUG ("Sending <presence type='unsubscribed'/> to contact#%u '%s'",
              contact, contact_id);

          if (!gabble_connection_send_presence (self->priv->conn,
              WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBED, contact_id, "", &error))
            break;
        }
    }

  tp_base_contact_list_contacts_changed (base, changed, removed);
  gabble_simple_async_succeed_or_fail_in_idle (self, callback, user_data,
      gabble_roster_unpublish_async, error);
  g_clear_error (&error);
  tp_handle_set_destroy (changed);
  tp_handle_set_destroy (removed);
}

static void
gabble_roster_download_async (TpBaseContactList *base,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  GError *error = NULL;

  if (!tp_base_contact_list_get_download_at_connection (base))
    {
      WockyStanza *stanza;

      DEBUG ("Downloading roster requested");

      stanza = _gabble_roster_message_new (self, WOCKY_STANZA_SUB_TYPE_GET,
          NULL);

      conn_util_send_iq_async (self->priv->conn, stanza,
          self->priv->cancel_on_disconnect,
          roster_received_cb, tp_weak_ref_new (self, NULL, NULL));

      g_object_unref (stanza);
    }
  else
    {
      DEBUG ("Downloading roster requested but it is already requested at "
             "connection. Just do nothing and return.");
    }

  gabble_simple_async_succeed_or_fail_in_idle (self, callback, user_data,
      gabble_roster_download_async, error);
  g_clear_error (&error);
}

static TpHandleSet *
gabble_roster_dup_blocked_contacts (TpBaseContactList *base)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpHandleSet *set;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  GHashTableIter iter;
  gpointer k, v;

  set = tp_handle_set_new (contact_repo);

  g_hash_table_iter_init (&iter, self->priv->items);

  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      GabbleRosterItem *item = v;

      if (item->blocked)
        tp_handle_set_add (set, GPOINTER_TO_UINT (k));
    }

  return set;
}

static gboolean
gabble_roster_can_block (TpBaseContactList *base)
{
  GabbleRoster *self = GABBLE_ROSTER (base);

  return (self->priv->conn->features &
      GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER) != 0;
}

static void
gabble_roster_block_contacts_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpIntsetFastIter iter;
  TpHandle contact;
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_request_subscription_async, 1);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    gabble_roster_handle_set_blocked (self, contact, TRUE, result);

  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
gabble_roster_unblock_contacts_async (TpBaseContactList *base,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpIntsetFastIter iter;
  TpHandle contact;
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_request_subscription_async, 1);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    gabble_roster_handle_set_blocked (self, contact, FALSE, result);

  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static GStrv
gabble_roster_dup_groups (TpBaseContactList *base)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  GPtrArray *ret;

  if (self->priv->groups != NULL)
    {
      GHashTableIter iter;
      gpointer k;

      ret = g_ptr_array_sized_new (
          g_hash_table_size (self->priv->groups) + 1);

      g_hash_table_iter_init (&iter, self->priv->groups);

      while (g_hash_table_iter_next (&iter, &k, NULL))
        {
          const gchar *group = k;

          g_ptr_array_add (ret, g_strdup (group));
        }
    }
  else
    {
      ret = g_ptr_array_sized_new (1);
    }

  g_ptr_array_add (ret, NULL);
  return (GStrv) g_ptr_array_free (ret, FALSE);
}

static GStrv
gabble_roster_dup_contact_groups (TpBaseContactList *base,
    TpHandle contact)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  GPtrArray *ret;
  GabbleRosterItem *item = _gabble_roster_item_lookup (self, contact);

  if (item != NULL && item->groups != NULL)
    {
      GHashTableIter iter;
      gpointer k;

      ret = g_ptr_array_sized_new (g_hash_table_size (item->groups) + 1);

      g_hash_table_iter_init (&iter, item->groups);
      while (g_hash_table_iter_next (&iter, &k, NULL))
        {
          g_ptr_array_add (ret, g_strdup (k));
        }
    }
  else
    {
      ret = g_ptr_array_sized_new (1);
    }

  g_ptr_array_add (ret, NULL);
  return (GStrv) g_ptr_array_free (ret, FALSE);
}

static TpHandleSet *
gabble_roster_dup_group_members (TpBaseContactList *base,
    const gchar *group)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpHandleSet *set;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  GHashTableIter iter;
  gpointer k, v;

  set = tp_handle_set_new (contact_repo);

  g_hash_table_iter_init (&iter, self->priv->items);

  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      GabbleRosterItem *item = v;

      if (item->groups != NULL &&
          g_hash_table_lookup (item->groups, group) != NULL)
        tp_handle_set_add (set, GPOINTER_TO_UINT (k));
    }

  return set;
}

static void
gabble_roster_set_contact_groups_async (TpBaseContactList *base,
    TpHandle contact,
    const gchar * const *groups,
    gsize n,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  GabbleRosterItem *item = _gabble_roster_item_ensure (self, contact);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  GHashTable *groups_set = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
  GPtrArray *groups_created = g_ptr_array_new ();
  guint i;
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_request_subscription_async, 1);

  for (i = 0; i < n; i++)
    {
      g_hash_table_add (groups_set, g_strdup (groups[i]));

      if (g_hash_table_lookup (self->priv->groups, groups[i]) == NULL)
        {
          g_hash_table_add (self->priv->groups, g_strdup (groups[i]));
          g_ptr_array_add (groups_created, (gchar *) groups[i]);
        }

    }

  if (groups_created->len > 0)
    {
      tp_base_contact_list_groups_created (base,
          (const gchar * const *) groups_created->pdata, groups_created->len);
    }

  g_ptr_array_unref (groups_created);

  if (item->unsent_edits == NULL)
    item->unsent_edits = item_edit_new (contact_repo, contact);

  DEBUG ("queue edit to contact#%u - set %" G_GSIZE_FORMAT
      "contact groups", contact, n);

  tp_clear_pointer (&item->unsent_edits->add_to_groups, g_hash_table_unref);
  item->unsent_edits->add_to_groups = groups_set;

  item->unsent_edits->remove_from_all_other_groups = TRUE;

  tp_clear_pointer (&item->unsent_edits->remove_from_groups,
      g_hash_table_unref);

  gabble_simple_async_countdown_inc (result);
  item->unsent_edits->results = g_slist_prepend (
      item->unsent_edits->results, g_object_ref (result));

  /* maybe we can apply the edit immediately? */
  roster_item_apply_edits (self, contact, item);
  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
gabble_roster_set_group_members_async (TpBaseContactList *base,
    const gchar *group,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_set_group_members_async, 1);
  GHashTableIter iter;
  gpointer k;

  /* we create the group even if @contacts is empty, as the base class
   * requires */
  if (g_hash_table_lookup (self->priv->groups, group) == NULL)
    {
      g_hash_table_add (self->priv->groups, g_strdup (group));
      tp_base_contact_list_groups_created (base, &group, 1);
    }

  g_hash_table_iter_init (&iter, self->priv->items);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      TpHandle contact = GPOINTER_TO_UINT (k);

      if (tp_handle_set_is_member (contacts, contact))
        gabble_roster_handle_add_to_group (self, contact, group, result);
      else
        gabble_roster_handle_remove_from_group (self, contact, group, result);
    }

  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
gabble_roster_add_to_group_async (TpBaseContactList *base,
    const gchar *group,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpIntsetFastIter iter;
  TpHandle contact;
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_add_to_group_async, 1);

  /* we create the group even if @contacts is empty, as the base class
   * requires */
  if (g_hash_table_lookup (self->priv->groups, group) == NULL)
    {
      g_hash_table_add (self->priv->groups, g_strdup (group));
      tp_base_contact_list_groups_created (base, &group, 1);
    }

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    {
      /* we ignore any NetworkError */
      gabble_roster_handle_add_to_group (self, contact, group, result);
    }

  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
gabble_roster_remove_from_group_async (TpBaseContactList *base,
    const gchar *group,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  TpIntsetFastIter iter;
  TpHandle contact;
  GSimpleAsyncResult *result = gabble_simple_async_countdown_new (self,
      callback, user_data, gabble_roster_remove_from_group_async, 1);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &contact))
    {
      gabble_roster_handle_remove_from_group (self, contact, group, result);
    }

  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

typedef struct {
    gchar *group;
    GAsyncReadyCallback callback;
    gpointer user_data;
    TpHandleSet *contacts;
} RemoveGroupContext;

static void
gabble_roster_remove_group_removed_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (source);
  RemoveGroupContext *context = user_data;

  if (context->group != NULL)
    {
      GHashTableIter iter;
      gpointer k, v;
      TpHandle remaining_member = 0;

      /* Now that we've signalled the group being removed, to be internally
       * consistent we should believe that the contacts are no longer there;
       * if a subsequent roster push says they *are* there, we'll just put
       * them back.
       *
       * However, if the group has members that we didn't remove (because
       * members were added since we sent off the removal requests), we can't
       * really remove the group.
       *
       * We defer the contact removal until after we've signalled group
       * removal, so that TpBaseContactList can see who used to be in the
       * group. */

      g_hash_table_iter_init (&iter, self->priv->items);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          TpHandle contact = GPOINTER_TO_UINT (k);
          GabbleRosterItem *item = v;

          if (item->groups != NULL && g_hash_table_lookup (item->groups,
                context->group) != NULL)
            {
              if (!tp_handle_set_is_member (context->contacts, contact))
                remaining_member = contact;
            }
        }

      if (remaining_member == 0)
        {
          g_hash_table_remove (self->priv->groups, context->group);

          tp_base_contact_list_groups_removed ((TpBaseContactList *) self,
              (const gchar * const *) &context->group, 1);

          g_hash_table_iter_init (&iter, self->priv->items);

          while (g_hash_table_iter_next (&iter, NULL, &v))
            {
              GabbleRosterItem *item = v;

              if (item->groups != NULL &&
                  g_hash_table_lookup (item->groups,
                    context->group) != NULL)
                g_hash_table_remove (item->groups, context->group);
            }
        }
      else
        {
          DEBUG ("contact #%u is still a member of group '%s', not removing",
              remaining_member, context->group);
        }
    }

  context->callback (source, result, context->user_data);
  tp_clear_pointer (&context->contacts, tp_handle_set_destroy);
  g_free (context->group);
  g_slice_free (RemoveGroupContext, context);
}

static void
gabble_roster_remove_group_async (TpBaseContactList *base,
    const gchar *group,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleRoster *self = GABBLE_ROSTER (base);
  GHashTableIter iter;
  gpointer k, v;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  GSimpleAsyncResult *result;
  RemoveGroupContext *context;

  context = g_slice_new0 (RemoveGroupContext);
  context->group = g_strdup (group);
  context->callback = callback;
  context->user_data = user_data;
  context->contacts = tp_handle_set_new (contact_repo);

  result = gabble_simple_async_countdown_new (self,
      gabble_roster_remove_group_removed_cb,
      context, gabble_roster_remove_group_async, 1);

  /* if the group didn't exist then we have nothing to do */
  if (context->group == NULL ||
      g_hash_table_lookup (self->priv->groups, context->group) == NULL)
    goto finally;

  g_hash_table_iter_init (&iter, self->priv->items);

  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      TpHandle contact = GPOINTER_TO_UINT (k);
      GabbleRosterItem *item = v;

      if (item->groups != NULL && g_hash_table_lookup (item->groups,
            context->group) != NULL)
        {
          tp_handle_set_add (context->contacts, contact);
          gabble_roster_handle_remove_from_group (self, contact, context->group,
              result);
        }
    }

finally:
  gabble_simple_async_countdown_dec (result);
  g_object_unref (result);
}

static void
mutable_iface_init (TpMutableContactListInterface *iface)
{
  iface->request_subscription_async = gabble_roster_request_subscription_async;
  iface->authorize_publication_async =
    gabble_roster_authorize_publication_async;
  iface->store_contacts_async = gabble_roster_store_contacts_async;
  iface->remove_contacts_async = gabble_roster_remove_contacts_async;
  iface->unsubscribe_async = gabble_roster_unsubscribe_async;
  iface->unpublish_async = gabble_roster_unpublish_async;
  /* we use the default _finish functions, which assume a GSimpleAsyncResult */
}

static void
blockable_iface_init (TpBlockableContactListInterface *iface)
{
  iface->can_block = gabble_roster_can_block;
  iface->dup_blocked_contacts = gabble_roster_dup_blocked_contacts;
  iface->block_contacts_async = gabble_roster_block_contacts_async;
  iface->unblock_contacts_async = gabble_roster_unblock_contacts_async;
  /* we use the default _finish functions, which assume a GSimpleAsyncResult */
}

static void
contact_groups_iface_init (TpContactGroupListInterface *iface)
{
  iface->dup_groups = gabble_roster_dup_groups;
  iface->dup_contact_groups = gabble_roster_dup_contact_groups;
  iface->dup_group_members = gabble_roster_dup_group_members;
}

static void
mutable_contact_groups_iface_init (TpMutableContactGroupListInterface *iface)
{
  iface->set_contact_groups_async = gabble_roster_set_contact_groups_async;
  iface->set_group_members_async = gabble_roster_set_group_members_async;
  iface->add_to_group_async = gabble_roster_add_to_group_async;
  iface->remove_from_group_async = gabble_roster_remove_from_group_async;
  iface->remove_group_async = gabble_roster_remove_group_async;
  /* we use the default _finish functions, which assume a GSimpleAsyncResult */
}

static void
gabble_roster_class_init (GabbleRosterClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  TpBaseContactListClass *base_class = TP_BASE_CONTACT_LIST_CLASS (cls);

  g_type_class_add_private (cls, sizeof (GabbleRosterPrivate));

  object_class->constructed = gabble_roster_constructed;
  object_class->dispose = gabble_roster_dispose;
  object_class->finalize = gabble_roster_finalize;

  base_class->dup_states = gabble_roster_dup_states;
  base_class->dup_contacts = gabble_roster_dup_contacts;
  base_class->download_async = gabble_roster_download_async;

  signals[NICKNAMES_UPDATE] = g_signal_new (
    "nicknames-update",
    G_TYPE_FROM_CLASS (cls),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_UINT_ARRAY);
}

gboolean
gabble_roster_handle_sends_presence_to_us (GabbleRoster *self,
    TpHandle handle)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_val_if_fail (GABBLE_IS_ROSTER (self), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  item = _gabble_roster_item_lookup (self, handle);

  if (item == NULL)
    return FALSE;

  return (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_TO ||
      item->subscription == GABBLE_ROSTER_SUBSCRIPTION_BOTH);
}

gboolean
gabble_roster_handle_gets_presence_from_us (GabbleRoster *self,
    TpHandle handle)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);
  GabbleRosterItem *item;

  g_return_val_if_fail (GABBLE_IS_ROSTER (self), FALSE);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  item = _gabble_roster_item_lookup (self, handle);

  if (item == NULL)
    return FALSE;

  if (item->blocked)
    return FALSE;

  return (item->subscription == GABBLE_ROSTER_SUBSCRIPTION_FROM ||
      item->subscription == GABBLE_ROSTER_SUBSCRIPTION_BOTH);
}
