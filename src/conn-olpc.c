/*
 * conn-olpc.c - Gabble OLPC BuddyInfo and ActivityProperties interfaces
 * Copyright (C) 2007 Collabora Ltd.
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
#include "conn-olpc.h"

#include <string.h>
#include <stdlib.h>

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include "debug.h"
#include "connection.h"
#include "muc-channel.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "pubsub.h"
#include "disco.h"
#include "util.h"
#include "olpc-activity.h"

/* FIXME: At some point we should audit this code to check which assumptions
 * it does about buddy and activity and if they are still relevant.
 * For example, we currently allow the creation of activity objects which
 * don't have an ID. I'm not sure that really make sense.
 * Or at some place in the code, we allow user to change the ID of an existing
 * activity object which is probably bong too. */

static gboolean
update_activities_properties (GabbleConnection *conn, const gchar *contact,
    LmMessage *msg);

/* Returns TRUE if it actually contributed something, else FALSE.
 */
static gboolean
activity_info_contribute_properties (GabbleOlpcActivity *activity,
                                     LmMessageNode *parent,
                                     gboolean only_public)
{
  LmMessageNode *props_node;

  if (activity->id == NULL || activity->properties == NULL)
    return FALSE;

  if (only_public && !gabble_olpc_activity_is_visible (activity))
    return FALSE;

  props_node = lm_message_node_add_child (parent,
      "properties", "");
  lm_message_node_set_attributes (props_node,
      "xmlns", NS_OLPC_ACTIVITY_PROPS,
      "room", gabble_olpc_activity_get_room (activity),
      "activity", activity->id,
      NULL);
  lm_message_node_add_children_from_properties (props_node,
      activity->properties, "property");
  return TRUE;
}

static void
decrement_contacts_activities_set_foreach (TpHandleSet *set,
                                           TpHandle handle,
                                           gpointer data)
{
  GabbleConnection *conn = data;
  GabbleOlpcActivity *activity = g_hash_table_lookup (
      conn->olpc_activities_info, GUINT_TO_POINTER (handle));

  g_object_unref (activity);
}

/* context may be NULL. */
static gboolean
check_pep (GabbleConnection *conn,
           DBusGMethodInvocation *context)
{
  if (!(conn->features & GABBLE_CONNECTION_FEATURES_PEP))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Server does not support PEP" };

      DEBUG ("%s", error.message);
      if (context != NULL)
        dbus_g_method_return_error (context, &error);
      return FALSE;
    }

  return TRUE;
}

static const gchar *
inspect_handle (TpBaseConnection *base,
                DBusGMethodInvocation *context,
                guint handle,
                TpHandleRepoIface *handle_repo)
{
  GError *error = NULL;

  if (!tp_handle_is_valid (handle_repo, handle, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return NULL;
    }

  return tp_handle_inspect (handle_repo, handle);
}

static const gchar *
inspect_contact (TpBaseConnection *base,
                 DBusGMethodInvocation *context,
                 guint contact)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_CONTACT);

  return inspect_handle (base, context, contact, contact_repo);
}

static const gchar *
inspect_room (TpBaseConnection *base,
              DBusGMethodInvocation *context,
              guint room)
{
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_ROOM);

  return inspect_handle (base, context, room, room_repo);
}

static gboolean
check_gadget_buddy (GabbleConnection *conn,
                    DBusGMethodInvocation *context)
{
  GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
    "Server does not provide Gadget Buddy service" };

  if (conn->olpc_gadget_buddy != NULL)
    return TRUE;

  if (context != NULL)
    dbus_g_method_return_error (context, &error);

  return FALSE;
}

/* context may be NULL, since this may be called in response to becoming
 * connected.
 */
static gboolean
check_publish_reply_msg (LmMessage *reply_msg,
                         DBusGMethodInvocation *context)
{
  switch (lm_message_get_sub_type (reply_msg))
    {
    case LM_MESSAGE_SUB_TYPE_RESULT:
      return TRUE;

    default:
        {
          LmMessageNode *error_node;
          GError *error = NULL;

          error_node = lm_message_node_get_child (reply_msg->node, "error");
          if (error_node != NULL)
            {
              GabbleXmppError xmpp_error = gabble_xmpp_error_from_node (
                  error_node, NULL);

              error = g_error_new (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
                  "Failed to publish to the PEP node: %s",
                  gabble_xmpp_error_description (xmpp_error));
            }
          else
            {
              error = g_error_new (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
                  "Failed to publish to the PEP node");
            }

          DEBUG ("%s", error->message);
          if (context != NULL)
            dbus_g_method_return_error (context, error);
          g_error_free (error);
        }
    }

  return FALSE;
}

static gboolean
check_query_reply_msg (LmMessage *reply_msg,
                       DBusGMethodInvocation *context)
{
  switch (lm_message_get_sub_type (reply_msg))
    {
    case LM_MESSAGE_SUB_TYPE_RESULT:
      return TRUE;

    default:
        {
          LmMessageNode *error_node;
          GError *error = NULL;

          if (context == NULL)
            return FALSE;

          error_node = lm_message_node_get_child (reply_msg->node, "error");
          if (error_node != NULL)
            {
              GabbleXmppError xmpp_error = gabble_xmpp_error_from_node (
                  error_node, NULL);

              error = g_error_new (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
                  "Failed to query the PEP node: %s",
                  gabble_xmpp_error_description (xmpp_error));
            }
          else
            {
              error = g_error_new (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
                  "Failed to query the PEP node");
            }

          DEBUG ("%s", error->message);
          dbus_g_method_return_error (context, error);
          g_error_free (error);
        }
    }

  return FALSE;
}

static LmHandlerResult
get_buddy_properties_from_search_reply_cb (GabbleConnection *conn,
                                           LmMessage *sent_msg,
                                           LmMessage *reply_msg,
                                           GObject *object,
                                           gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;
  LmMessageNode *query, *buddy;
  const gchar *buddy_jid;
  GError *error = NULL;
  NodeIter i;

  /* Which buddy are we requesting properties for ? */
  buddy = lm_message_node_find_child (sent_msg->node, "buddy");
  g_assert (buddy != NULL);
  buddy_jid = lm_message_node_get_attribute (buddy, "jid");
  g_assert (buddy_jid != NULL);

  /* Parse the reply */
  query = lm_message_node_get_child_with_namespace (reply_msg->node, "query",
      NS_OLPC_BUDDY);
  if (query == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Search reply doesn't contain <query> node");
      goto search_reply_cb_end;
    }

  for (i = node_iter (query); i; i = node_iter_next (i))
    {
      const gchar *jid;

      buddy = node_iter_data (i);
      jid = lm_message_node_get_attribute (buddy, "jid");

      if (!tp_strdiff (jid, buddy_jid))
        {
          LmMessageNode *properties_node;
          GHashTable *properties;

          properties_node = lm_message_node_get_child_with_namespace (buddy,
              "properties", NS_OLPC_BUDDY_PROPS);
          properties = lm_message_node_extract_properties (properties_node,
              "property");

          gabble_svc_olpc_buddy_info_return_from_get_properties (context,
              properties);
          g_hash_table_destroy (properties);
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  /* We didn't find the buddy */
  g_set_error (&error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
      "Search reply doesn't contain info about %s", buddy_jid);

search_reply_cb_end:
  if (error != NULL)
    {
      DEBUG ("error in indexer reply: %s", error->message);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
get_buddy_properties_from_search (GabbleConnection *conn,
                                  const gchar *buddy,
                                  DBusGMethodInvocation *context)
{
  LmMessage *query;

  if (!check_gadget_buddy (conn, context))
    return;

  query = lm_message_build_with_sub_type (conn->olpc_gadget_buddy,
      LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET,
      '(', "query", "",
          '@', "xmlns", NS_OLPC_BUDDY,
          '(', "buddy", "",
            '@', "jid", buddy,
          ')',
      ')',
      NULL);

  if (!_gabble_connection_send_with_reply (conn, query,
        get_buddy_properties_from_search_reply_cb, NULL, context, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send buddy search query to server" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
    }

  lm_message_unref (query);
}

static LmHandlerResult
get_properties_reply_cb (GabbleConnection *conn,
                         LmMessage *sent_msg,
                         LmMessage *reply_msg,
                         GObject *object,
                         gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;
  GHashTable *properties;
  LmMessageNode *node;

  if (!check_query_reply_msg (reply_msg, NULL))
    {
      const gchar *buddy;

      buddy = lm_message_node_get_attribute (sent_msg->node, "to");
      g_assert (buddy != NULL);

      DEBUG ("PEP query failed. Let's try to search this buddy.");
      get_buddy_properties_from_search (conn, buddy, context);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  node = lm_message_node_find_child (reply_msg->node, "properties");
  properties = lm_message_node_extract_properties (node, "property");

  gabble_svc_olpc_buddy_info_return_from_get_properties (context, properties);
  g_hash_table_destroy (properties);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_buddy_info_get_properties (GabbleSvcOLPCBuddyInfo *iface,
                                guint contact,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  const gchar *jid;
  GHashTable *properties;

  DEBUG ("called");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  /* First check if we can find properties in a buddy view */
  /* FIXME: Maybe we should first try the PEP node as we do for buddy
   * activities ? */
  properties = gabble_olpc_gadget_manager_find_buddy_properties (
      conn->olpc_gadget_manager, contact);
  if (properties != NULL)
    {
      gabble_svc_olpc_buddy_info_return_from_get_properties (context,
          properties);
      return;
    }

  /* Then try to query the PEP node */
  jid = inspect_contact (base, context, contact);
  if (jid == NULL)
    return;

  if (!pubsub_query (conn, jid, NS_OLPC_BUDDY_PROPS, get_properties_reply_cb,
        context))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property request to server" };

      dbus_g_method_return_error (context, &error);
    }
}

/* context may be NULL. */
static LmHandlerResult
set_properties_reply_cb (GabbleConnection *conn,
                         LmMessage *sent_msg,
                         LmMessage *reply_msg,
                         GObject *object,
                         gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;

  if (!check_publish_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (context != NULL)
    gabble_svc_olpc_buddy_info_return_from_set_properties (context);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/* context may be NULL, in which case it will be NULL in the reply_cb. */
static void
transmit_properties (GabbleConnection *conn,
                     GHashTable *properties,
                     DBusGMethodInvocation *context)
{
  LmMessage *msg;
  LmMessageNode *publish;

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  msg = pubsub_make_publish_msg (NULL, NS_OLPC_BUDDY_PROPS,
      NS_OLPC_BUDDY_PROPS, "properties", &publish);

  lm_message_node_add_children_from_properties (publish, properties,
      "property");

  if (!_gabble_connection_send_with_reply (conn, msg,
        set_properties_reply_cb, NULL, context, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property change request to server" };

      DEBUG ("%s", error.message);
      if (context != NULL)
        dbus_g_method_return_error (context, &error);
    }

  lm_message_unref (msg);
}

static GQuark
preload_buddy_properties_quark (void)
{
  static GQuark q = 0;
  if (q == 0)
    {
      q = g_quark_from_static_string
        ("GabbleConnection.preload_buddy_properties_quark");
    }
  return q;
}

static GQuark
invitees_quark (void)
{
  static GQuark q = 0;
  if (q == 0)
    {
      q = g_quark_from_static_string
        ("GabbleConnection.conn_olpc_invitees_quark");
    }
  return q;
}

void
gabble_connection_connected_olpc (GabbleConnection *conn)
{
  GHashTable *preload = g_object_steal_qdata ((GObject *) conn,
      preload_buddy_properties_quark ());

  if (preload != NULL)
    {
      transmit_properties (conn, preload, NULL);
      g_hash_table_destroy (preload);
    }
}

static void
olpc_buddy_info_set_properties (GabbleSvcOLPCBuddyInfo *iface,
                                GHashTable *properties,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base_conn = (TpBaseConnection *) conn;
  DEBUG ("called");

  if (base_conn->status == TP_CONNECTION_STATUS_CONNECTED)
    {
      transmit_properties (conn, properties, context);
    }
  else
    {
      GHashTable *preload;
      GQuark preload_quark = preload_buddy_properties_quark ();

      DEBUG ("Not connected: will perform OLPC buddy property update later");

      preload = g_object_get_qdata ((GObject *) conn, preload_quark);
      if (preload != NULL)
        {
          /* throw away any already-preloaded properties - SetProperties
           * is an overwrite, not an update */
          g_hash_table_remove_all (preload);
        }
      else
        {
          preload = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
               (GDestroyNotify) tp_g_value_slice_free);
          g_object_set_qdata_full ((GObject *) conn, preload_quark, preload,
              (GDestroyNotify) g_hash_table_destroy);
        }

      tp_g_hash_table_update (preload, properties,
          (GBoxedCopyFunc) g_strdup,
          (GBoxedCopyFunc) tp_g_value_slice_dup);

      gabble_svc_olpc_buddy_info_return_from_set_properties (context);
    }
}

gboolean
olpc_buddy_info_properties_event_handler (GabbleConnection *conn,
                                          LmMessage *msg,
                                          TpHandle handle)
{
  GHashTable *properties;
  LmMessageNode *node;
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (handle == base->self_handle)
    /* Ignore echoed pubsub notifications */
    return TRUE;

  node = lm_message_node_find_child (msg->node, "properties");
  properties = lm_message_node_extract_properties (node, "property");
  gabble_svc_olpc_buddy_info_emit_properties_changed (conn, handle,
      properties);
  g_hash_table_destroy (properties);
  return TRUE;
}

static LmHandlerResult
get_activity_properties_reply_cb (GabbleConnection *conn,
                                  LmMessage *sent_msg,
                                  LmMessage *reply_msg,
                                  GObject *object,
                                  gpointer user_data)
{
  const gchar *from;

  from = lm_message_node_get_attribute (reply_msg->node, "from");
  update_activities_properties (conn, from, reply_msg);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
remove_activity (gpointer key,
                 gpointer value,
                 gpointer activity)
{
  return activity == value;
}

static void
activity_disposed_cb (gpointer _conn,
                      GObject *activity)
{
  GabbleConnection *conn = GABBLE_CONNECTION (_conn);

  if (conn->olpc_activities_info == NULL)
    /* We are disposing */
    return;

  g_hash_table_foreach_remove (conn->olpc_activities_info,
      remove_activity, activity);
}

static GabbleOlpcActivity *
add_activity_info (GabbleConnection *conn,
                   TpHandle handle)
{
  GabbleOlpcActivity *activity;

  activity = gabble_olpc_activity_new (conn, handle);

  g_hash_table_insert (conn->olpc_activities_info,
      GUINT_TO_POINTER (handle), activity);
  g_object_weak_ref (G_OBJECT (activity), activity_disposed_cb, conn);

  return activity;
}

static GPtrArray *
get_buddy_activities (GabbleConnection *conn,
                      TpHandle buddy)
{
  TpIntSet *all;
  gboolean free_all = FALSE;
  GPtrArray *activities = g_ptr_array_new ();
  TpHandleSet *invited_activities, *pep_activities;

  invited_activities = g_hash_table_lookup (conn->olpc_invited_activities,
      GUINT_TO_POINTER (buddy));
  pep_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (buddy));

  if (invited_activities == NULL)
    {
      if (pep_activities == NULL)
        {
          all = NULL;
        }
      else
        {
          all = tp_handle_set_peek (pep_activities);
        }
    }
  else
    {
      if (pep_activities == NULL)
        {
          all = tp_handle_set_peek (invited_activities);
        }
      else
        {
          all = tp_intset_union (tp_handle_set_peek (invited_activities),
              tp_handle_set_peek (pep_activities));
          free_all = TRUE;
        }
    }

  if (all != NULL)
    {
      TpIntSetIter iter = TP_INTSET_ITER_INIT (all);

      while (tp_intset_iter_next (&iter))
        {
          GabbleOlpcActivity *activity = g_hash_table_lookup (
              conn->olpc_activities_info, GUINT_TO_POINTER (iter.element));
          GValue gvalue = { 0 };

          g_assert (activity != NULL);
          if (activity->id == NULL)
            {
              DEBUG ("... activity #%u has no ID, skipping", iter.element);
              continue;
            }

          g_value_init (&gvalue, GABBLE_STRUCT_TYPE_ACTIVITY);
          g_value_take_boxed (&gvalue, dbus_g_type_specialized_construct
              (GABBLE_STRUCT_TYPE_ACTIVITY));
          dbus_g_type_struct_set (&gvalue,
              0, activity->id,
              1, activity->room,
              G_MAXUINT);
          DEBUG ("... activity #%u (ID %s)",
              activity->room, activity->id);
          g_ptr_array_add (activities, g_value_get_boxed (&gvalue));
        }
    }

  if (free_all)
    tp_intset_destroy (all);

  return activities;
}

static void
extract_activities (GabbleConnection *conn,
                    LmMessage *msg,
                    TpHandle sender)
{
  LmMessageNode *activities_node;
  TpHandleSet *activities_set, *old_activities;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  NodeIter i;

  activities_node = lm_message_node_find_child (msg->node, "activities");

  activities_set = tp_handle_set_new (room_repo);

  for (i = (activities_node != NULL ? node_iter (activities_node) : NULL);
       i;
       i = node_iter_next (i))
    {
      const gchar *act_id;
      const gchar *room;
      LmMessageNode *node = node_iter_data (i);
      GabbleOlpcActivity *activity;
      TpHandle room_handle;

      if (tp_strdiff (node->name, "activity"))
        continue;

      act_id = lm_message_node_get_attribute (node, "type");
      if (act_id == NULL)
        {
          NODE_DEBUG (node, "No activity ID, skipping");
          continue;
        }

      room = lm_message_node_get_attribute (node, "room");
      if (room == NULL)
        {
          NODE_DEBUG (node, "No room name, skipping");
          continue;
        }

      room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);
      if (room_handle == 0)
        {
          DEBUG ("Invalid room name <%s>, skipping", room);
          continue;
        }

      activity = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (room_handle));

      if (activity == NULL)
        {
          activity = add_activity_info (conn, room_handle);
          g_assert (!tp_handle_set_is_member (activities_set, room_handle));
        }
      else
        {
          if (tp_handle_set_is_member (activities_set, room_handle))
            {
              NODE_DEBUG (node, "Room advertised twice, skipping");
              tp_handle_unref (room_repo, room_handle);
              continue;
            }

          g_object_ref (activity);

          DEBUG ("ref: %s (%d) refcount: %d\n",
              gabble_olpc_activity_get_room (activity),
              activity->room, G_OBJECT (activity)->ref_count);
        }
      /* pass ownership to the activities_set */
      tp_handle_set_add (activities_set, room_handle);
      tp_handle_unref (room_repo, room_handle);

      if (tp_strdiff (activity->id, act_id))
        {
          DEBUG ("Assigning new ID <%s> to room #%u <%s>", act_id, room_handle,
              room);
          g_object_set (activity, "id", act_id, NULL);
        }
    }

  old_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (sender));

  if (old_activities != NULL)
    {
      /* We decrement the refcount (and free if needed) all the
       * activities previously announced by this contact. */
      tp_handle_set_foreach (old_activities,
          decrement_contacts_activities_set_foreach, conn);
    }

  /* Update the list of activities associated with this contact. */
  g_hash_table_insert (conn->olpc_pep_activities,
      GUINT_TO_POINTER (sender), activities_set);
}

static void
free_activities (GPtrArray *activities)
{
  guint i;

  for (i = 0; i < activities->len; i++)
    g_boxed_free (GABBLE_STRUCT_TYPE_ACTIVITY, activities->pdata[i]);

  g_ptr_array_free (activities, TRUE);
}

static void
check_activity_properties (GabbleConnection *conn,
                           GPtrArray *activities,
                           const gchar *from)
{
  /* XXX: dirty hack!
   * We use PEP instead of pubsub until we have MEP.
   * When we request activities from a remote contact we need to check
   * if we already "know" his activities (we have its properties).
   * If not, we need to explicitely ask to the user to send them to us.
   * When we'll have MEP we will be able to request activities
   * propreties from muc's pubsub node and so avoid all this crack.
   */
  gboolean query_needed = FALSE;
  guint i;

  for (i = 0; i < activities->len && !query_needed; i++)
    {
      GValue pair = {0,};
      guint channel;
      GabbleOlpcActivity *activity;

      g_value_init (&pair, GABBLE_STRUCT_TYPE_ACTIVITY);
      g_value_set_static_boxed (&pair, g_ptr_array_index (activities, i));
      dbus_g_type_struct_get (&pair,
          1, &channel,
          G_MAXUINT);

      activity = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (channel));
      if (activity == NULL || activity->properties == NULL)
        {
          query_needed = TRUE;
        }
    }

  if (query_needed)
    {
      if (!pubsub_query (conn, from, NS_OLPC_ACTIVITY_PROPS,
          get_activity_properties_reply_cb, NULL))
        {
          DEBUG ("Failed to send activity properties request to server");
        }
    }
}

static void
return_buddy_activities_from_views (GabbleConnection *conn,
                                    TpHandle contact,
                                    DBusGMethodInvocation *context)
{
  GPtrArray *activities;

  activities = gabble_olpc_gadget_manager_find_buddy_activities (
      conn->olpc_gadget_manager, contact);
  gabble_svc_olpc_buddy_info_return_from_get_activities (context, activities);

  free_activities (activities);
}

static LmHandlerResult
get_activities_reply_cb (GabbleConnection *conn,
                         LmMessage *sent_msg,
                         LmMessage *reply_msg,
                         GObject *object,
                         gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;
  GPtrArray *activities;
  const gchar *from;
  TpHandle from_handle;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);

  from = lm_message_node_get_attribute (reply_msg->node, "from");
  if (from == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Error in pubsub reply: no sender" };

      dbus_g_method_return_error (context, &error);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  from_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (from_handle == 0)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Error in pubsub reply: unknown sender" };

      dbus_g_method_return_error (context, &error);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      DEBUG ("Failed to query PEP node. Compute activities list using views");
      return_buddy_activities_from_views (conn, from_handle, context);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  extract_activities (conn, reply_msg, from_handle);

  activities = get_buddy_activities (conn, from_handle);

  /* FIXME: race between client and PEP */
  check_activity_properties (conn, activities, from);

  gabble_svc_olpc_buddy_info_return_from_get_activities (context, activities);

  free_activities (activities);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_buddy_info_get_activities (GabbleSvcOLPCBuddyInfo *iface,
                                guint contact,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  const gchar *jid;

  DEBUG ("called");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  jid = inspect_contact (base, context, contact);
  if (jid == NULL)
    return;

  if (!pubsub_query (conn, jid, NS_OLPC_ACTIVITIES, get_activities_reply_cb,
        context))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property request to server" };

      dbus_g_method_return_error (context, &error);
    }
}

/* FIXME: API could be improved */
static gboolean
upload_activities_pep (GabbleConnection *conn,
                       GabbleConnectionMsgReplyFunc callback,
                       gpointer user_data,
                       GError **error)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  LmMessageNode *publish;
  LmMessage *msg = pubsub_make_publish_msg (NULL, NS_OLPC_ACTIVITIES,
      NS_OLPC_ACTIVITIES, "activities", &publish);
  TpHandleSet *my_activities = g_hash_table_lookup
      (conn->olpc_pep_activities, GUINT_TO_POINTER (base->self_handle));
  GError *e = NULL;
  gboolean ret;

  if (my_activities != NULL)
    {
      TpIntSetIter iter = TP_INTSET_ITER_INIT (tp_handle_set_peek
            (my_activities));

      while (tp_intset_iter_next (&iter))
        {
          GabbleOlpcActivity *activity = g_hash_table_lookup (
              conn->olpc_activities_info, GUINT_TO_POINTER (iter.element));
          LmMessageNode *activity_node;

          g_assert (activity != NULL);
          if (!gabble_olpc_activity_is_visible (activity))
            continue;

          activity_node = lm_message_node_add_child (publish,
              "activity", "");
          lm_message_node_set_attributes (activity_node,
              "type", activity->id,
              "room", gabble_olpc_activity_get_room (activity),
              NULL);
        }
    }

  ret = _gabble_connection_send_with_reply (conn, msg, callback, NULL,
        user_data, &e);

  if (!ret)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Failed to send property change request to server: %s", e->message);
      g_error_free (e);
    }

  lm_message_unref (msg);
  return ret;
}

static LmHandlerResult
set_activities_reply_cb (GabbleConnection *conn,
                         LmMessage *sent_msg,
                         LmMessage *reply_msg,
                         GObject *object,
                         gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;

  if (!check_publish_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  /* FIXME: emit ActivitiesChanged? */

  gabble_svc_olpc_buddy_info_return_from_set_activities (context);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_buddy_info_set_activities (GabbleSvcOLPCBuddyInfo *iface,
                                const GPtrArray *activities,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_ROOM);
  guint i;
  TpHandleSet *activities_set, *old_activities;

  DEBUG ("called");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  activities_set = tp_handle_set_new (room_repo);

  for (i = 0; i < activities->len; i++)
    {
      GValue pair = {0,};
      gchar *id;
      guint channel;
      const gchar *room = NULL;
      GabbleOlpcActivity *activity;
      GError *error = NULL;

      g_value_init (&pair, GABBLE_STRUCT_TYPE_ACTIVITY);
      g_value_set_static_boxed (&pair, g_ptr_array_index (activities, i));
      dbus_g_type_struct_get (&pair,
          0, &id,
          1, &channel,
          G_MAXUINT);

      if (!tp_handle_is_valid (room_repo, channel, &error))
        {
          DEBUG ("Invalid room handle");
          dbus_g_method_return_error (context, error);

          /* We have to unref information previously
           * refed in this loop */
          tp_handle_set_foreach (activities_set,
              decrement_contacts_activities_set_foreach, conn);

          /* set_activities failed so we don't unref old activities
           * of the local user */

          tp_handle_set_destroy (activities_set);
          g_error_free (error);
          g_free (id);
          return;
        }

      room = tp_handle_inspect (room_repo, channel);

      activity = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (channel));

      if (activity == NULL)
        {
          activity = add_activity_info (conn, channel);
        }
      else
        {
          if (tp_handle_set_is_member (activities_set, channel))
            {
              error = g_error_new (TP_ERRORS,
                  TP_ERROR_INVALID_ARGUMENT,
                  "Can't set twice the same activity: %s", room);

              DEBUG ("activity already added: %s", room);
              dbus_g_method_return_error (context, error);

              /* We have to unref information previously
               * refed in this loop */
              tp_handle_set_foreach (activities_set,
                  decrement_contacts_activities_set_foreach, conn);

              /* set_activities failed so we don't unref old activities
               * of the local user */

              tp_handle_set_destroy (activities_set);
              g_error_free (error);
              g_free (activity);
              g_free (id);
              return;
            }

          g_object_ref (activity);

          DEBUG ("ref: %s (%d) refcount: %d\n",
              gabble_olpc_activity_get_room (activity),
              activity->room, G_OBJECT (activity)->ref_count);
        }

      g_object_set (activity, "id", id, NULL);
      g_free (id);

      tp_handle_set_add (activities_set, channel);
    }

  old_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle));

  if (old_activities != NULL)
    {
      /* We decrement the refcount (and free if needed) all the
       * activities previously announced by our own contact. */
      tp_handle_set_foreach (old_activities,
          decrement_contacts_activities_set_foreach, conn);
    }

  /* Update the list of activities associated with our own contact. */
  g_hash_table_insert (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle), activities_set);

  if (!upload_activities_pep (conn, set_activities_reply_cb, context, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property request to server" };

      dbus_g_method_return_error (context, &error);
    }

  /* FIXME: what if we were advertising properties for things that
   * we've declared are no longer in our activities list? Strictly speaking
   * we should probably re-upload our activity properties PEP if that's
   * the case */
}

gboolean
olpc_buddy_info_activities_event_handler (GabbleConnection *conn,
                                          LmMessage *msg,
                                          TpHandle handle)
{
  GPtrArray *activities;
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (handle == base->self_handle)
    /* Ignore echoed pubsub notifications */
    return TRUE;

  extract_activities (conn, msg, handle);
  activities = get_buddy_activities (conn, handle);
  gabble_svc_olpc_buddy_info_emit_activities_changed (conn, handle,
      activities);
  free_activities (activities);
  return TRUE;
}

static GabbleOlpcActivity *
add_activity_info_in_set (GabbleConnection *conn,
                          TpHandle room_handle,
                          const gchar *from,
                          GHashTable *table)
{
  GabbleOlpcActivity *activity;
  TpHandle from_handle;
  TpHandleSet *activities_set;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);

  from_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);

  if (from_handle == 0)
    {
      DEBUG ("unknown sender");
      return NULL;
    }

  activity = add_activity_info (conn, room_handle);

  /* Add activity information in the list of the contact */
  activities_set = g_hash_table_lookup (table, GUINT_TO_POINTER (
        from_handle));
  if (activities_set == NULL)
    {
      activities_set = tp_handle_set_new (room_repo);
      g_hash_table_insert (table, GUINT_TO_POINTER (from_handle),
          activities_set);
    }

  /* add_activity_info_in_set isn't meant to be called if the
   * activity already existed */
  g_assert (!tp_handle_set_is_member (activities_set, room_handle));

  /* the set owns the ref of the newly created activity */
  tp_handle_set_add (activities_set, room_handle);

  return activity;
}

static GabbleOlpcActivity *
extract_current_activity (GabbleConnection *conn,
                          LmMessageNode *node,
                          const gchar *contact,
                          gboolean create_activity)
{
  const gchar *room, *id;
  GabbleOlpcActivity *activity;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle room_handle, contact_handle;

  if (node == NULL)
    return NULL;

  /* For some weird reasons, the PEP protocol use "type" for the activity ID.
   * We can't change that without breaking compatibility but if there is no
   * "type" attribute then we can use the "id" one.
   * The Gadget protocol use "id" instead of "type". */
  id = lm_message_node_get_attribute (node, "type");
  if (id == NULL)
    {
      id = lm_message_node_get_attribute (node, "id");
    }

  room = lm_message_node_get_attribute (node, "room");
  if (room == NULL || room[0] == '\0')
    return NULL;

  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    return NULL;

  contact_handle = tp_handle_lookup (contact_repo, contact, NULL, NULL);
  if (contact_handle == 0)
    return NULL;

  activity = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));

  if (activity == NULL && create_activity)
    {
      /* Humm we received as current activity an activity we don't know yet.
       * If the remote user doesn't announce this activity
       * in his next activities list, information about
       * it will be freed */

      DEBUG ("unknown current activity %s", room);

      activity = add_activity_info_in_set (conn, room_handle, contact,
          conn->olpc_pep_activities);
      g_object_set (activity, "id", id, NULL);
    }

  tp_handle_unref (room_repo, room_handle);

  /* update current-activity cache */
  if (activity != NULL)
    {
      g_hash_table_insert (conn->olpc_current_act,
          GUINT_TO_POINTER (contact_handle), g_object_ref (activity));
    }
  else
    {
      g_hash_table_remove (conn->olpc_current_act,
          GUINT_TO_POINTER (contact_handle));
    }

  return activity;
}

static LmHandlerResult
get_current_activity_reply_cb (GabbleConnection *conn,
                               LmMessage *sent_msg,
                               LmMessage *reply_msg,
                               GObject *object,
                               gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;
  LmMessageNode *node;
  const gchar *from;
  GabbleOlpcActivity *activity;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      DEBUG ("Failed to query PEP node. No current activity");

      gabble_svc_olpc_buddy_info_return_from_get_current_activity (context,
          "", 0);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  from = lm_message_node_get_attribute (reply_msg->node, "from");
  node = lm_message_node_find_child (reply_msg->node, "activity");
  activity = extract_current_activity (conn, node, from, TRUE);
  if (activity == NULL)
    {
      DEBUG ("GetCurrentActivity returns no activity");

      gabble_svc_olpc_buddy_info_return_from_get_current_activity (context,
          "", 0);
    }
  else
    {
      DEBUG ("GetCurrentActivity returns (\"%s\", room#%u)", activity->id,
          activity->room);

      gabble_svc_olpc_buddy_info_return_from_get_current_activity (context,
          activity->id, activity->room);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_buddy_info_get_current_activity (GabbleSvcOLPCBuddyInfo *iface,
                                      guint contact,
                                      DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  const gchar *jid;
  GabbleOlpcActivity *activity;

  DEBUG ("called for contact#%u", contact);

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  jid = inspect_contact (base, context, contact);
  if (jid == NULL)
    return;

  activity = g_hash_table_lookup (conn->olpc_current_act,
      GUINT_TO_POINTER (contact));
  if (activity != NULL)
    {
      DEBUG ("found current activity in cache: %s (%u)", activity->id,
          activity->room);

      gabble_svc_olpc_buddy_info_return_from_get_current_activity (context,
          activity->id, activity->room);
      return;
    }

    DEBUG ("current activity not in cache, query PEP node");

    if (!pubsub_query (conn, jid, NS_OLPC_CURRENT_ACTIVITY,
        get_current_activity_reply_cb, context))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property request to server" };

      dbus_g_method_return_error (context, &error);
    }
}

static LmHandlerResult
set_current_activity_reply_cb (GabbleConnection *conn,
                               LmMessage *sent_msg,
                               LmMessage *reply_msg,
                               GObject *object,
                               gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;

  if (!check_publish_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  gabble_svc_olpc_buddy_info_return_from_set_current_activity (context);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/* Check if this activity is in our own activities list */
static gboolean
activity_in_own_set (GabbleConnection *conn,
                     const gchar *room)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleSet *activities_set;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    /* If activity's information was in the list, we would
     * have found the handle as Activity keep a ref on it */
    return FALSE;

  activities_set = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle));

  if (activities_set == NULL ||
      !tp_handle_set_is_member (activities_set, room_handle))
    return FALSE;

  return TRUE;
}

static void
olpc_buddy_info_set_current_activity (GabbleSvcOLPCBuddyInfo *iface,
                                      const gchar *activity,
                                      guint channel,
                                      DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  LmMessage *msg;
  LmMessageNode *publish;
  const gchar *room = "";

  DEBUG ("called");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  /* if activity == "" there is no current activity */
  if (activity[0] != '\0')
    {
      room = inspect_room (base, context, channel);
      if (room == NULL)
        return;

      if (!activity_in_own_set (conn, room))
        {
          GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "Can't set an activity as current if you're not announcing it" };

          dbus_g_method_return_error (context, &error);
          return;
        }
    }

  msg = pubsub_make_publish_msg (NULL, NS_OLPC_CURRENT_ACTIVITY,
      NS_OLPC_CURRENT_ACTIVITY, "activity", &publish);

  lm_message_node_set_attributes (publish,
      "type", activity,
      "room", room,
      NULL);

  if (!_gabble_connection_send_with_reply (conn, msg,
        set_current_activity_reply_cb, NULL, context, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property change request to server" };

      dbus_g_method_return_error (context, &error);
    }

  lm_message_unref (msg);
}

gboolean
olpc_buddy_info_current_activity_event_handler (GabbleConnection *conn,
                                                LmMessage *msg,
                                                TpHandle handle)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  LmMessageNode *node;
  const gchar *from;
  GabbleOlpcActivity *activity;

  if (handle == base->self_handle)
    /* Ignore echoed pubsub notifications */
    return TRUE;

  from = lm_message_node_get_attribute (msg->node, "from");
  node = lm_message_node_find_child (msg->node, "activity");

  activity = extract_current_activity (conn, node, from, TRUE);
  if (activity != NULL)
    {
      DEBUG ("emitting CurrentActivityChanged(contact#%u, ID \"%s\", room#%u)",
             handle, activity->id, activity->room);
      gabble_svc_olpc_buddy_info_emit_current_activity_changed (conn, handle,
          activity->id, activity->room);
    }
  else
    {
      DEBUG ("emitting CurrentActivityChanged(contact#%u, \"\", 0)",
             handle);
      gabble_svc_olpc_buddy_info_emit_current_activity_changed (conn, handle,
          "", 0);
    }

  return TRUE;
}

void
olpc_buddy_info_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  GabbleSvcOLPCBuddyInfoClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_olpc_buddy_info_implement_##x (\
    klass, olpc_buddy_info_##x)
  IMPLEMENT(get_activities);
  IMPLEMENT(set_activities);
  IMPLEMENT(get_properties);
  IMPLEMENT(set_properties);
  IMPLEMENT(get_current_activity);
  IMPLEMENT(set_current_activity);
#undef IMPLEMENT
}

/* FIXME: API could be improved */
static gboolean
upload_activity_properties_pep (GabbleConnection *conn,
                                GabbleConnectionMsgReplyFunc callback,
                                gpointer user_data,
                                GError **error)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  LmMessageNode *publish;
  LmMessage *msg = pubsub_make_publish_msg (NULL, NS_OLPC_ACTIVITY_PROPS,
      NS_OLPC_ACTIVITY_PROPS, "activities", &publish);
  GError *e = NULL;
  gboolean ret;
  TpHandleSet *my_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle));

  if (my_activities != NULL)
    {
      TpIntSetIter iter = TP_INTSET_ITER_INIT (tp_handle_set_peek
          (my_activities));

      while (tp_intset_iter_next (&iter))
        {
          GabbleOlpcActivity *activity = g_hash_table_lookup (
              conn->olpc_activities_info, GUINT_TO_POINTER (iter.element));

          activity_info_contribute_properties (activity, publish, TRUE);
        }
    }

  ret = _gabble_connection_send_with_reply (conn, msg, callback, NULL,
        user_data, &e);

  if (!ret)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Failed to send property change request to server: %s", e->message);
      g_error_free (e);
    }

  lm_message_unref (msg);
  return ret;
}

typedef struct {
    DBusGMethodInvocation *context;
    gboolean visibility_changed;
    GabbleOlpcActivity *activity;
} set_properties_ctx;

static LmHandlerResult
set_activity_properties_activities_reply_cb (GabbleConnection *conn,
                                             LmMessage *sent_msg,
                                             LmMessage *reply_msg,
                                             GObject *object,
                                             gpointer user_data)
{
  set_properties_ctx *context = user_data;

  /* if the SetProperties() call was skipped, both messages are NULL */
  g_assert ((sent_msg == NULL) == (reply_msg == NULL));

  if (reply_msg != NULL &&
      !check_publish_reply_msg (reply_msg, context->context))
    {
      g_slice_free (set_properties_ctx, context);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  gabble_svc_olpc_activity_properties_emit_activity_properties_changed (
      conn, context->activity->room, context->activity->properties);

  gabble_svc_olpc_activity_properties_return_from_set_properties (
      context->context);

  g_slice_free (set_properties_ctx, context);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult
set_activity_properties_reply_cb (GabbleConnection *conn,
                                  LmMessage *sent_msg,
                                  LmMessage *reply_msg,
                                  GObject *object,
                                  gpointer user_data)
{
  set_properties_ctx *context = user_data;

  /* if the SetProperties() call was skipped, both messages are NULL */
  g_assert ((sent_msg == NULL) == (reply_msg == NULL));

  if (reply_msg != NULL &&
      !check_publish_reply_msg (reply_msg, context->context))
    {
      g_slice_free (set_properties_ctx, context);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  if (context->visibility_changed)
    {
      GError *err = NULL;

      if (!upload_activities_pep (conn,
            set_activity_properties_activities_reply_cb,
            context, &err))
        {
          dbus_g_method_return_error (context->context, err);
          g_error_free (err);
        }
    }
  else
    {
      /* nothing to do, so just "succeed" */
      set_activity_properties_activities_reply_cb (conn, NULL, NULL, NULL,
          context);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
refresh_invitations (GabbleConnection *conn,
                     GabbleMucChannel *chan,
                     GabbleOlpcActivity *activity,
                     GError **error)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleSet *invitees = g_object_get_qdata ((GObject *) chan,
      invitees_quark ());

  if (invitees != NULL && tp_handle_set_size (invitees) > 0)
    {
      LmMessage *msg = lm_message_new (NULL, LM_MESSAGE_TYPE_MESSAGE);
      TpIntSetIter iter = TP_INTSET_ITER_INIT (tp_handle_set_peek
          (invitees));

      activity_info_contribute_properties (activity, msg->node, FALSE);

      while (tp_intset_iter_next (&iter))
        {
          const gchar *to = tp_handle_inspect (contact_repo, iter.element);

          lm_message_node_set_attribute (msg->node, "to", to);
          if (!_gabble_connection_send (conn, msg, error))
            {
              DEBUG ("Unable to re-send activity properties to invitee %s",
                  to);
              lm_message_unref (msg);
              return FALSE;
            }
        }

      lm_message_unref (msg);
    }

  return TRUE;
}

static gboolean
invite_gadget (GabbleConnection *conn,
               GabbleMucChannel *muc)
{
  if (conn->olpc_gadget_activity == NULL)
    return FALSE;

  DEBUG ("Activity becomes public. Invite gadget to it");
  return gabble_muc_channel_send_invite (muc, conn->olpc_gadget_activity,
      "Share activity", NULL);
}

static void
olpc_activity_properties_set_properties (GabbleSvcOLPCActivityProperties *iface,
                                         guint room,
                                         GHashTable *properties,
                                         DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  LmMessage *msg;
  const gchar *jid;
  GHashTable *properties_copied;
  GabbleOlpcActivity *activity;
  GabbleMucChannel *muc_channel;
  guint state;
  gboolean was_visible, is_visible;
  set_properties_ctx *ctx;
  GError *err = NULL;

  DEBUG ("called");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  jid = inspect_room (base, context, room);
  if (jid == NULL)
    return;

  if (!activity_in_own_set (conn, jid))
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Can't set properties on an activity if you're not announcing it" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  muc_channel = gabble_muc_factory_find_text_channel (conn->muc_factory,
      room);
  if (muc_channel != NULL)
    {
      g_object_get (muc_channel,
          "state", &state,
          NULL);
    }
  if (muc_channel == NULL || state != MUC_STATE_JOINED)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Can't set properties on an activity if you're not in it" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  properties_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  tp_g_hash_table_update (properties_copied, properties,
      (GBoxedCopyFunc) g_strdup, (GBoxedCopyFunc) tp_g_value_slice_dup);

  activity = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room));

  was_visible = gabble_olpc_activity_is_visible (activity);

  g_object_set (activity, "properties", properties_copied, NULL);

  is_visible = gabble_olpc_activity_is_visible (activity);

  msg = lm_message_new (jid, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_node_set_attribute (msg->node, "type", "groupchat");
  activity_info_contribute_properties (activity, msg->node, FALSE);
  if (!_gabble_connection_send (conn, msg, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property change notification to chatroom" };

      lm_message_unref (msg);
      dbus_g_method_return_error (context, &error);
      return;
    }
  lm_message_unref (msg);

  if (!refresh_invitations (conn, muc_channel, activity, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  ctx = g_slice_new (set_properties_ctx);
  ctx->context = context;
  ctx->visibility_changed = (was_visible != is_visible);
  ctx->activity = activity;

  if (!was_visible && is_visible)
    {
      if (conn->olpc_gadget_publish)
        {
          /* activity becomes visible. Invite gadget */
          invite_gadget (conn, muc_channel);
        }
    }

  if (was_visible || is_visible)
    {
      if (!upload_activity_properties_pep (conn,
            set_activity_properties_reply_cb, ctx, &err))
        {
          g_slice_free (set_properties_ctx, ctx);
          dbus_g_method_return_error (context, err);
          g_error_free (err);
          return;
        }
    }
  else
    {
      /* chain straight to the reply callback, which changes our Activities
       * list */
      set_activity_properties_reply_cb (conn, NULL, NULL, NULL, ctx);
    }
}

static void
olpc_activity_properties_get_properties (GabbleSvcOLPCActivityProperties *iface,
                                         guint room,
                                         DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  gboolean not_prop = FALSE;
  GHashTable *properties;
  GabbleOlpcActivity *activity;

  DEBUG ("called");

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  activity = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room));

  if (activity == NULL || activity->properties == NULL)
    {
      /* no properties */
      properties = g_hash_table_new (g_str_hash, g_str_equal);
      not_prop = TRUE;
    }
  else
    {
      properties = activity->properties;
    }

  gabble_svc_olpc_activity_properties_return_from_get_properties (context,
      properties);

  if (not_prop)
    g_hash_table_destroy (properties);
}

struct _i_hate_g_hash_table_foreach
{
  GHashTable *old_properties;
  gboolean new_infos;
};

static void
check_prop_in_old_properties (gpointer key,
                              gpointer value,
                              gpointer user_data)
{
  const gchar *prop = key;
  GValue *gvalue = value, *old_gvalue;
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;

  old_gvalue = g_hash_table_lookup (data->old_properties, prop);

  if (old_gvalue == NULL)
    {
      data->new_infos = TRUE;
    }
  else if (G_VALUE_TYPE (gvalue) != G_VALUE_TYPE (old_gvalue))
    {
      data->new_infos = TRUE;
    }
  else
    {
      if (G_VALUE_TYPE (gvalue) ==  G_TYPE_STRING)
        {
          const gchar *str1, *str2;

          str1 = g_value_get_string (gvalue);
          str2 = g_value_get_string (old_gvalue);

          if (tp_strdiff (str1, str2))
            {
              data->new_infos = TRUE;
            }
        }
      else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
        {
          gboolean bool1, bool2;

          bool1 = g_value_get_boolean (gvalue);
          bool2 = g_value_get_boolean (old_gvalue);

          if (bool1 != bool2)
            {
              data->new_infos = TRUE;
            }
        }
      else
        {
          /* if in doubt, emit the signal */
          data->new_infos = TRUE;
        }
    }
}

static gboolean
properties_contains_new_infos (GHashTable *old_properties,
                               GHashTable *new_properties)
{
  struct _i_hate_g_hash_table_foreach data;

  if (g_hash_table_size (new_properties) > g_hash_table_size (old_properties))
    /* New key/value pair(s) */
    return TRUE;

  data.old_properties = old_properties;
  data.new_infos = FALSE;

  g_hash_table_foreach (new_properties, check_prop_in_old_properties,
      &data);

  return data.new_infos;
}

static void
update_activity_properties (GabbleConnection *conn,
                            const gchar *room,
                            const gchar *contact,
                            LmMessageNode *properties_node)
{
  GHashTable *new_properties, *old_properties;
  gboolean new_infos = FALSE;
  GabbleOlpcActivity *activity;
  TpHandle room_handle;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);

  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);

  activity = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));

  if (activity == NULL)
    {
      DEBUG ("unknown activity: %s", room);
      if (contact != NULL)
        {
          /* Humm we received properties for an activity we don't
           * know yet.
           * If the remote user doesn't announce this activity
           * in his next activities list, information about
           * it will be freed */
          activity = add_activity_info_in_set (conn, room_handle, contact,
              conn->olpc_pep_activities);
        }
      else
        {
          activity = add_activity_info (conn, room_handle);
        }
    }

  tp_handle_unref (room_repo, room_handle);

  if (activity == NULL)
    return;

  old_properties = activity->properties;

  new_properties = lm_message_node_extract_properties (properties_node,
      "property");

  if (g_hash_table_size (new_properties) == 0)
    {
      g_hash_table_destroy (new_properties);
      return;
    }

  if (old_properties == NULL ||
      properties_contains_new_infos (old_properties,
        new_properties))
    {
      new_infos = TRUE;
    }

  g_object_set (activity, "properties", new_properties, NULL);

  if (new_infos)
    {
      /* Only emit the signal if we add new values */

      gabble_svc_olpc_activity_properties_emit_activity_properties_changed (
          conn, activity->room, new_properties);
    }
}

static gboolean
update_activities_properties (GabbleConnection *conn,
                              const gchar *contact,
                              LmMessage *msg)
{
  const gchar *room;
  LmMessageNode *node;
  NodeIter i;

  node = lm_message_node_find_child (msg->node, "activities");
  if (node == NULL)
    return FALSE;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *properties_node = node_iter_data (i);

      if (strcmp (properties_node->name, "properties") != 0)
        continue;

      room = lm_message_node_get_attribute (properties_node, "room");
      if (room == NULL)
        continue;

      update_activity_properties (conn, room, contact, properties_node);
    }
  return TRUE;
}

gboolean
olpc_activities_properties_event_handler (GabbleConnection *conn,
                                          LmMessage *msg,
                                          TpHandle handle)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  const gchar *from;

  if (handle == base->self_handle)
    /* Ignore echoed pubsub notifications */
    return TRUE;

  from = lm_message_node_get_attribute (msg->node, "from");

  return update_activities_properties (conn, from, msg);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              TpConnectionStatus status,
                              TpConnectionStatusReason reason,
                              gpointer user_data)
{
  if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
      /* Well, let's do another crack.
       * We have to cleanup PEP node to avoid to confuse
       * remote contacts with old properties from a previous session.
       */
      if (!upload_activities_pep (conn, NULL, NULL, NULL))
        {
          DEBUG ("Failed to send PEP activities reset in response to "
              "initial connection");
        }
      if (!upload_activity_properties_pep (conn, NULL, NULL,
            NULL))
        {
          DEBUG ("Failed to send PEP activity props reset in response to "
              "initial connection");
        }
    }
}

static LmHandlerResult
pseudo_invite_reply_cb (GabbleConnection *conn,
                        LmMessage *sent_msg,
                        LmMessage *reply_msg,
                        GObject *object,
                        gpointer user_data)
{
  if (!check_publish_reply_msg (reply_msg, NULL))
    {
      NODE_DEBUG (reply_msg->node, "Failed to make PEP change in "
          "response to pseudo-invitation message");
      NODE_DEBUG (sent_msg->node, "The failed request was");
    }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

gboolean
conn_olpc_process_activity_properties_message (GabbleConnection *conn,
                                               LmMessage *msg,
                                               const gchar *from)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  LmMessageNode *node = lm_message_node_get_child_with_namespace (msg->node,
      "properties", NS_OLPC_ACTIVITY_PROPS);
  const gchar *id;
  TpHandle room_handle, contact_handle = 0;
  GabbleOlpcActivity *activity;
  TpHandleSet *their_invites, *our_activities;
  GHashTable *old_properties, *new_properties;
  gboolean properties_changed, pep_properties_changed, activities_changed;
  gboolean was_visible, is_visible;
  GabbleMucChannel *muc_channel = NULL;

  /* if no <properties xmlns=...>, then not for us */
  if (node == NULL)
    return FALSE;

  DEBUG ("Found <properties> node in <message>");

  id = lm_message_node_get_attribute (node, "activity");
  if (id == NULL)
    {
      NODE_DEBUG (node, "... activity ID missing - ignoring");
      return TRUE;
    }

  room_handle = gabble_get_room_handle_from_jid (room_repo, from);

  if (room_handle != 0)
    {
      muc_channel = gabble_muc_factory_find_text_channel (conn->muc_factory,
          room_handle);
    }

  if (muc_channel == NULL)
    {
      const gchar *room;

      DEBUG ("Activity properties message was a pseudo-invitation");

      /* FIXME: This is stupid. We should ref the handles in a TpHandleSet
       * per activity, then we could _ensure this handle */
      contact_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
      if (contact_handle == 0)
        {
          DEBUG ("... contact <%s> unknown - ignoring (FIX THIS)", from);
          return TRUE;
        }

      room = lm_message_node_get_attribute (node, "room");
      if (room == NULL)
        {
          NODE_DEBUG (node, "... room name missing - ignoring");
          return TRUE;
        }
      DEBUG ("... room <%s>", room);
      room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);
      if (room_handle == 0)
        {
          DEBUG ("... room <%s> invalid - ignoring", room);
          return TRUE;
        }

      muc_channel = gabble_muc_factory_find_text_channel (conn->muc_factory,
          room_handle);
      if (muc_channel != NULL)
        {
          guint state;

          g_object_get (muc_channel,
              "state", &state,
              NULL);
          if (state == MUC_STATE_JOINED)
            {
              DEBUG ("Ignoring pseudo-invitation to <%s> - we're already "
                  "there", room);
              return TRUE;
            }
        }
    }
  else
    {
      TpHandle self_handle;

      DEBUG ("Activity properties message was in a chatroom");

      tp_group_mixin_get_self_handle ((GObject *) muc_channel, &self_handle,
          NULL);

      if (tp_handle_lookup (contact_repo, from, NULL, NULL) == self_handle)
        {
          DEBUG ("Ignoring echoed activity properties message from myself");
          return TRUE;
        }
    }

  activity = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));

  if (contact_handle != 0)
    {
      their_invites = g_hash_table_lookup (conn->olpc_invited_activities,
          GUINT_TO_POINTER (contact_handle));
      if (their_invites == NULL)
        {
          activities_changed = TRUE;
          their_invites = tp_handle_set_new (room_repo);
          g_hash_table_insert (conn->olpc_invited_activities,
              GUINT_TO_POINTER (contact_handle), their_invites);
        }
      else
        {
          activities_changed = !tp_handle_set_is_member (their_invites,
              room_handle);
        }

      if (activity == NULL)
        {
          DEBUG ("... creating new Activity");
          activity = add_activity_info (conn, room_handle);
          tp_handle_set_add (their_invites, room_handle);
        }
      else if (!tp_handle_set_is_member (their_invites, room_handle))
        {
          DEBUG ("... it's the first time that contact invited me, "
              "referencing Activity on their behalf");
          g_object_ref (activity);
          tp_handle_set_add (their_invites, room_handle);
        }
      tp_handle_unref (room_repo, room_handle);
    }
  else
    {
      activities_changed = FALSE;
      /* we're in the room, so it ought to have an Activity ref'd */
      g_assert (activity != NULL);
    }

  new_properties = lm_message_node_extract_properties (node,
      "property");
  g_assert (new_properties);

  /* before applying the changes, gather enough information to work out
   * whether anything changed */

  old_properties = activity->properties;

  was_visible = gabble_olpc_activity_is_visible (activity);

  properties_changed = old_properties == NULL
    || properties_contains_new_infos (old_properties, new_properties);

  /* apply the info we found */

  if (tp_strdiff (activity->id, id))
    {
      DEBUG ("... recording new activity ID %s", id);
      g_object_set (activity, "id", id, NULL);
    }

  g_object_set (activity, "properties", new_properties, NULL);

  /* emit signals and amend our PEP nodes, if necessary */

  is_visible = gabble_olpc_activity_is_visible (activity);

  if (is_visible)
    {
      pep_properties_changed = properties_changed || !was_visible;
    }
  else
    {
      pep_properties_changed = was_visible;
    }

  if (properties_changed)
    gabble_svc_olpc_activity_properties_emit_activity_properties_changed (conn,
        room_handle, new_properties);

  if (activities_changed)
    {
      GPtrArray *activities;
      g_assert (contact_handle != 0);

      activities = get_buddy_activities (conn, contact_handle);
      gabble_svc_olpc_buddy_info_emit_activities_changed (conn, contact_handle,
          activities);
      free_activities (activities);
    }

  if (properties_changed && muc_channel != NULL)
    refresh_invitations (conn, muc_channel, activity, NULL);

  /* If we're announcing this activity, we might need to change our PEP node */
  if (pep_properties_changed)
    {
      our_activities = g_hash_table_lookup (conn->olpc_pep_activities,
          GUINT_TO_POINTER (base->self_handle));
      if (our_activities != NULL &&
          tp_handle_set_is_member (our_activities, room_handle))
        {
          if (!upload_activity_properties_pep (conn,
                pseudo_invite_reply_cb, NULL, NULL))
            {
              DEBUG ("Failed to send PEP properties change in response to "
              "properties change message");
            }
        }
   }

  if (is_visible != was_visible)
    {
      our_activities = g_hash_table_lookup (conn->olpc_pep_activities,
          GUINT_TO_POINTER (base->self_handle));
      if (our_activities != NULL &&
          tp_handle_set_is_member (our_activities, room_handle))
        {
          if (!upload_activities_pep (conn,
                pseudo_invite_reply_cb, NULL, NULL))
            {
              DEBUG ("Failed to send PEP activities change in response to "
              "properties change message");
            }
        }
   }

  return TRUE;
}

static LmHandlerResult
closed_pep_reply_cb (GabbleConnection *conn,
                     LmMessage *sent_msg,
                     LmMessage *reply_msg,
                     GObject *object,
                     gpointer user_data)
{
  if (!check_publish_reply_msg (reply_msg, NULL))
    {
      NODE_DEBUG (reply_msg->node, "Failed to make PEP change in "
          "response to channel closure");
      NODE_DEBUG (sent_msg->node, "The failed request was");
    }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
revoke_invitations (GabbleConnection *conn,
                    GabbleMucChannel *chan,
                    GabbleOlpcActivity *activity,
                    GError **error)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleSet *invitees = g_object_get_qdata ((GObject *) chan,
      invitees_quark ());

  if (activity->id == NULL)
    /* this is not a real OLPC activity */
    return TRUE;

  if (invitees != NULL && tp_handle_set_size (invitees) > 0)
    {
      LmMessage *msg = lm_message_new (NULL, LM_MESSAGE_TYPE_MESSAGE);
      TpIntSetIter iter = TP_INTSET_ITER_INIT (tp_handle_set_peek
          (invitees));
      LmMessageNode *uninvite_node;

      uninvite_node = lm_message_node_add_child (msg->node, "uninvite", "");
      lm_message_node_set_attribute (uninvite_node, "xmlns",
          NS_OLPC_ACTIVITY_PROPS);
      lm_message_node_set_attribute (uninvite_node, "room",
          gabble_olpc_activity_get_room (activity));
      lm_message_node_set_attribute (uninvite_node, "id",
          activity->id);

      DEBUG ("revoke invitations for activity %s", activity->id);
      while (tp_intset_iter_next (&iter))
        {
          const gchar *to = tp_handle_inspect (contact_repo, iter.element);

          lm_message_node_set_attribute (msg->node, "to", to);
          if (!_gabble_connection_send (conn, msg, error))
            {
              DEBUG ("Unable to send activity invitee revocation %s",
                  to);
              lm_message_unref (msg);
              return FALSE;
            }
        }

      lm_message_unref (msg);
    }

  return TRUE;
}

gboolean
conn_olpc_process_activity_uninvite_message (GabbleConnection *conn,
                                             LmMessage *msg,
                                             const gchar *from)
{
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *node;
  const gchar *id, *room;
  TpHandle room_handle, from_handle;
  TpHandleSet *rooms;

  node = lm_message_node_get_child_with_namespace (msg->node, "uninvite",
      NS_OLPC_ACTIVITY_PROPS);

  /* if no <uninvite xmlns=...>, then not for us */
  if (node == NULL)
    return FALSE;

  id = lm_message_node_get_attribute (node, "id");
  if (id == NULL)
    {
      DEBUG ("no activity id. Skip");
      return TRUE;
    }

  room = lm_message_node_get_attribute (node, "room");
  if (room == NULL)
    {
      DEBUG ("no room. Skip");
      return TRUE;
    }

  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    {
      DEBUG ("room %s unknown", room);
      return TRUE;
    }

  from_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (from_handle == 0)
    {
      DEBUG ("sender %s unknown", from);
      return TRUE;
    }

  rooms = g_hash_table_lookup (conn->olpc_invited_activities,
      GUINT_TO_POINTER (from_handle));

  if (rooms == NULL)
    {
      DEBUG ("No invites associated with contact %d", from_handle);
      return TRUE;
    }

  if (tp_handle_set_remove (rooms, room_handle))
    {
      GabbleOlpcActivity *activity;
      GPtrArray *activities;

      activity = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (room_handle));

      if (activity == NULL)
        {
          DEBUG ("No info about activity associated with room %s", room);
          return TRUE;
        }

      if (tp_strdiff (id, activity->id))
        {
          DEBUG ("Uninvite's activity id (%s) doesn't match our "
              "activity id (%s)", id, activity->id);
          return TRUE;
        }

      DEBUG ("remove invite from %s", from);
      g_object_unref (activity);

      /* Emit BuddyInfo::ActivitiesChanged */
      activities = get_buddy_activities (conn, from_handle);
      gabble_svc_olpc_buddy_info_emit_activities_changed (conn, from_handle,
          activities);
      free_activities (activities);
    }
  else
    {
      DEBUG ("No invite from %s for activity %s (room %s)", from, id, room);
      return TRUE;
    }

  return TRUE;
}

static void
muc_channel_closed_cb (GabbleMucChannel *chan,
                       GabbleOlpcActivity *activity)
{
  GabbleConnection *conn;
  TpHandleSet *my_activities;
  gboolean was_in_our_pep = FALSE;

  g_object_get (activity, "connection", &conn, NULL);

  /* Revoke invitations we sent for this activity */
  revoke_invitations (conn, chan, activity, NULL);

  /* remove it from our advertised activities list, unreffing it in the
   * process if it was in fact advertised */
  my_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (TP_BASE_CONNECTION (conn)->self_handle));
  if (my_activities != NULL)
    {
      if (tp_handle_set_remove (my_activities, activity->room))
        {
          was_in_our_pep = gabble_olpc_activity_is_visible (activity);
          g_object_unref (activity);
        }
    }

  /* unref it again (it was referenced on behalf of the channel) */
  g_object_unref (activity);

  if (was_in_our_pep)
    {
      if (!upload_activities_pep (conn, closed_pep_reply_cb, NULL, NULL))
        {
          DEBUG ("Failed to send PEP activities change in response to "
              "channel close");
        }
      if (!upload_activity_properties_pep (conn, closed_pep_reply_cb, NULL,
            NULL))
        {
          DEBUG ("Failed to send PEP activity props change in response to "
              "channel close");
        }
    }

  g_object_unref (conn);
}

static void
muc_channel_pre_invite_cb (GabbleMucChannel *chan,
                           const gchar *jid,
                           GabbleOlpcActivity *activity)
{
  GabbleConnection *conn;
  TpHandleRepoIface *contact_repo;
  GQuark quark = invitees_quark ();
  TpHandleSet *invitees;
  /* send them the properties */
  LmMessage *msg;

  g_object_get (activity, "connection", &conn, NULL);
  contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);

  msg = lm_message_new (jid, LM_MESSAGE_TYPE_MESSAGE);

  if (activity_info_contribute_properties (activity, msg->node, FALSE))
    {
      /* not much we can do about errors - but if this fails, the invitation
       * will too, unless something extremely strange is going on */
      if (!_gabble_connection_send (conn, msg, NULL))
        {
          DEBUG ("Unable to send activity properties to invitee");
        }
    }
  lm_message_unref (msg);

  /* don't add gadget */
  if (tp_strdiff (jid, conn->olpc_gadget_activity))
    {
      TpHandle handle;
      GError *error = NULL;

      handle = tp_handle_ensure (contact_repo, jid, NULL, &error);
      if (handle == 0)
        {
          DEBUG ("can't add %s to invitees: %s", jid, error->message);
          g_error_free (error);
          g_object_unref (conn);
          return;
        }

      invitees = g_object_get_qdata ((GObject *) chan, quark);
      if (invitees == NULL)
        {
          invitees = tp_handle_set_new (contact_repo);
          g_object_set_qdata_full ((GObject *) chan, quark, invitees,
              (GDestroyNotify) tp_handle_set_destroy);
        }

      tp_handle_set_add (invitees, handle);
      tp_handle_unref (contact_repo, handle);
    }

  g_object_unref (conn);
}

typedef struct
{
  GabbleConnection *conn;
  TpHandle room_handle;
} remove_invite_foreach_ctx;

static void
remove_invite_foreach (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  TpHandle inviter = GPOINTER_TO_UINT (key);
  TpHandleSet *rooms = (TpHandleSet *) value;
  remove_invite_foreach_ctx *ctx = (remove_invite_foreach_ctx *) user_data;

  /* We are now in the activity and so the responsibilty to track
   * buddies membership is delegated to the PS. At some point, maybe that
   * should be done by CM's */
  if (tp_handle_set_remove (rooms, ctx->room_handle))
    {
      GabbleOlpcActivity *activity;
      GPtrArray *activities;

      activity = g_hash_table_lookup (ctx->conn->olpc_activities_info,
          GUINT_TO_POINTER (ctx->room_handle));

      activities = get_buddy_activities (ctx->conn, inviter);
      gabble_svc_olpc_buddy_info_emit_activities_changed (ctx->conn, inviter,
          activities);
      free_activities (activities);

      g_assert (activity != NULL);
      DEBUG ("forget invite for activity %s from contact %d", activity->id,
          inviter);
      g_object_unref (activity);
    }
}

static void
forget_activity_invites (GabbleConnection *conn,
                         TpHandle room_handle)
{
  remove_invite_foreach_ctx ctx;

  ctx.conn = conn;
  ctx.room_handle = room_handle;
  g_hash_table_foreach (conn->olpc_invited_activities, remove_invite_foreach,
      &ctx);
}

static void
muc_channel_contact_join_cb (GabbleMucChannel *chan,
                             TpHandle contact,
                             GabbleOlpcActivity *activity)
{
  GabbleConnection *conn;

  g_object_get (activity, "connection", &conn, NULL);

  if (contact == TP_BASE_CONNECTION (conn)->self_handle)
    {
      /* We join the channel, forget about all invites we received about
       * this activity */
      forget_activity_invites (conn, activity->room);
    }
  else
    {
      GQuark quark = invitees_quark ();
      TpHandleSet *invitees;

      invitees = g_object_get_qdata ((GObject *) chan, quark);
      if (invitees != NULL)
        {
          DEBUG ("contact %d joined the muc, remove the invite we sent to him",
              contact);
          tp_handle_set_remove (invitees, contact);
        }
    }

  g_object_unref (conn);
}

static void
muc_factory_new_channel_cb (gpointer key,
                            gpointer value,
                            gpointer data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (key);
  GabbleOlpcActivity *activity;
  TpHandle room_handle;

  if (!GABBLE_IS_MUC_CHANNEL (chan))
    return;

  g_object_get (chan,
      "handle", &room_handle,
      NULL);

  /* ref the activity as long as we have a channel open */

  activity = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));

  if (activity == NULL)
    {
      activity = add_activity_info (conn, room_handle);
    }
  else
    {
      g_object_ref (activity);
    }

  g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb),
      activity);
  g_signal_connect (chan, "pre-invite", G_CALLBACK (muc_channel_pre_invite_cb),
      activity);
  g_signal_connect (chan, "contact-join",
      G_CALLBACK (muc_channel_contact_join_cb), activity);
}

static void
muc_factory_new_channels_cb (GabbleMucFactory *fac,
                             GHashTable *channels,
                             GabbleConnection *conn)
{
  g_hash_table_foreach (channels, muc_factory_new_channel_cb, conn);
}

static void
connection_presence_do_update (GabblePresenceCache *cache,
                               TpHandle handle,
                               GabbleConnection *conn)
{
  GabblePresence *presence;

  presence = gabble_presence_cache_get (cache, handle);

  if (presence && presence->status <= GABBLE_PRESENCE_LAST_UNAVAILABLE)
    {
      /* Contact becomes unavailable. We have to unref all the information
       * provided by him
       */
      GPtrArray *empty = g_ptr_array_new ();
      TpHandleSet *list;

      list = g_hash_table_lookup (conn->olpc_pep_activities,
          GUINT_TO_POINTER (handle));

      if (list != NULL)
        tp_handle_set_foreach (list,
            decrement_contacts_activities_set_foreach, conn);

      g_hash_table_remove (conn->olpc_pep_activities,
          GUINT_TO_POINTER (handle));

      list = g_hash_table_lookup (conn->olpc_invited_activities,
          GUINT_TO_POINTER (handle));

      if (list != NULL)
        tp_handle_set_foreach (list,
            decrement_contacts_activities_set_foreach, conn);

      g_hash_table_remove (conn->olpc_invited_activities,
          GUINT_TO_POINTER (handle));

      gabble_svc_olpc_buddy_info_emit_activities_changed (conn, handle,
          empty);
      g_ptr_array_free (empty, TRUE);
    }
}

static void
buddy_changed (GabbleConnection *conn,
               LmMessageNode *change)
{
  LmMessageNode *node;
  const gchar *jid, *id_str;
  guint id;
  TpHandle handle;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  GabbleOlpcView *view;

  jid = lm_message_node_get_attribute (change, "jid");
  if (jid == NULL)
    {
      DEBUG ("No jid attribute in change message. Discarding");
      return;
    }

  id_str = lm_message_node_get_attribute (change, "id");
  if (id_str == NULL)
    {
      DEBUG ("No view ID attribute in change message. Discarding");
      return;
    }

  id = strtoul (id_str, NULL, 10);
  view = gabble_olpc_gadget_manager_get_view (conn->olpc_gadget_manager, id);

  if (view == NULL)
    {
      DEBUG ("No active view with ID %u", id);
      return;
    }

  handle = tp_handle_lookup (contact_repo, jid, NULL, NULL);
  if (handle == 0)
    {
      DEBUG ("Invalid jid: %s. Discarding", jid);
      return;
    }

  node = lm_message_node_get_child_with_namespace (change,
      "properties", NS_OLPC_BUDDY_PROPS);
  if (node != NULL)
    {
      /* Buddy properties changes */
      GHashTable *properties;

      properties = lm_message_node_extract_properties (node,
          "property");

      gabble_olpc_view_set_buddy_properties (view, handle, properties);

      gabble_svc_olpc_buddy_info_emit_properties_changed (conn, handle,
          properties);

      g_hash_table_unref (properties);
    }

  node = lm_message_node_get_child_with_namespace (change,
      "activity", NS_OLPC_CURRENT_ACTIVITY);
  if (node != NULL)
    {
      /* Buddy current activity change */
      GabbleOlpcActivity *activity;

      /* extract_current_activity won't create the activity if we don't
       * know it yet as we'll have no way to find activity's info if
       * the activity is not in a view */
      activity = extract_current_activity (conn, node, jid, FALSE);
      if (activity == NULL)
        {
          gabble_svc_olpc_buddy_info_emit_current_activity_changed (conn,
              handle, "", 0);
        }
      else
        {
          gabble_svc_olpc_buddy_info_emit_current_activity_changed (conn,
              handle, activity->id, activity->room);
        }
    }
}

static void
activity_changed (GabbleConnection *conn,
                  LmMessageNode *change)
{
  LmMessageNode *node;

  node = lm_message_node_get_child_with_namespace (change, "properties",
      NS_OLPC_ACTIVITY_PROPS);
  if (node != NULL)
    {
      const gchar *room;
      LmMessageNode *properties_node;

      room = lm_message_node_get_attribute (change, "room");
      properties_node = lm_message_node_get_child_with_namespace (change,
          "properties", NS_OLPC_ACTIVITY_PROPS);

      if (room != NULL && properties_node != NULL)
        {
          update_activity_properties (conn, room, NULL, properties_node);
        }
    }
}

static gboolean
populate_buddies_from_nodes (GabbleConnection *conn,
                             LmMessageNode *node,
                             const gchar *node_name,
                             GArray *buddies,
                             GPtrArray *buddies_properties)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  NodeIter i;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *buddy = node_iter_data (i);
      const gchar *jid;
      TpHandle handle;

      if (tp_strdiff (buddy->name, node_name))
        continue;

      jid = lm_message_node_get_attribute (buddy, "jid");

      handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);
      if (handle == 0)
        {
          guint j;

          DEBUG ("Invalid jid: %s", jid);

          /* Free the ressources previously allocated */
          for (j = 0; j < buddies->len; j++)
            tp_handle_unref (contact_repo,
                g_array_index (buddies, TpHandle, j));

          if (buddies_properties != NULL)
            {
              g_ptr_array_foreach (buddies_properties,
                  (GFunc) g_hash_table_unref, NULL);
            }

          return FALSE;
        }

      g_array_append_val (buddies, handle);

      if (buddies_properties != NULL)
        {
          LmMessageNode *properties_node;
          GHashTable *properties;

          properties_node = lm_message_node_get_child_with_namespace (buddy,
              "properties", NS_OLPC_BUDDY_PROPS);
          properties = lm_message_node_extract_properties (properties_node,
              "property");

          g_ptr_array_add (buddies_properties, properties);
        }
    }

  return TRUE;
}

gboolean
add_activities_to_view_from_node (GabbleConnection *conn,
                                  GabbleOlpcView *view,
                                  LmMessageNode *node)
{
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  GHashTable *activities;
  GPtrArray *buddies_to_add;
  struct buddies_to_add_t
    {
      GArray *buddies;
      GPtrArray *buddies_properties;
      TpHandle room;
    };
  guint i;
  NodeIter k;

  activities = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      g_object_unref );

  buddies_to_add = g_ptr_array_new ();

  for (k = node_iter (node); k; k = node_iter_next (k))
    {
      const gchar *jid, *act_id;
      LmMessageNode *activity_node = node_iter_data (k);
      LmMessageNode *properties_node;
      GHashTable *properties;
      TpHandle handle;
      GabbleOlpcActivity *activity;
      struct buddies_to_add_t *tmp;

      jid = lm_message_node_get_attribute (activity_node, "room");
      if (jid == NULL)
        {
          NODE_DEBUG (activity_node, "No room attribute, skipping");
          continue;
        }

      act_id = lm_message_node_get_attribute (activity_node, "id");
      if (act_id == NULL)
        {
          NODE_DEBUG (activity_node, "No activity ID, skipping");
          continue;
        }

      handle = tp_handle_ensure (room_repo, jid, NULL, NULL);
      if (handle == 0)
        {
          DEBUG ("Invalid jid: %s", jid);
          g_hash_table_destroy (activities);
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      properties_node = lm_message_node_get_child_with_namespace (
          activity_node, "properties", NS_OLPC_ACTIVITY_PROPS);
      properties = lm_message_node_extract_properties (properties_node,
          "property");

      gabble_svc_olpc_activity_properties_emit_activity_properties_changed (
          conn, handle, properties);

      /* ref the activity while it is in the view */
      activity = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (handle));
      if (activity == NULL)
        {
          activity = add_activity_info (conn, handle);
        }
      else
        {
          g_object_ref (activity);
        }

      g_hash_table_insert (activities, GUINT_TO_POINTER (handle), activity);
      tp_handle_unref (room_repo, handle);

      if (tp_strdiff (activity->id, act_id))
        {
          DEBUG ("Assigning new ID <%s> to room #%u", act_id, handle);

          g_object_set (activity, "id", act_id, NULL);
        }

      g_object_set (activity, "properties", properties, NULL);

      /* We have to wait that activities were added to the view before
       * adding participants */
      tmp = g_slice_new (struct buddies_to_add_t);
      tmp->buddies = g_array_new (FALSE, FALSE, sizeof (TpHandle));
      tmp->buddies_properties = g_ptr_array_new ();
      tmp->room = handle;

      if (!populate_buddies_from_nodes (conn, activity_node, "buddy",
            tmp->buddies, tmp->buddies_properties))
        {
          g_array_free (tmp->buddies, TRUE);
          g_ptr_array_free (tmp->buddies_properties, TRUE);
          continue;
        }

      g_ptr_array_add (buddies_to_add, tmp);
    }

  gabble_olpc_view_add_activities (view, activities);

  /* Add participants to the view */
  for (i = 0; i < buddies_to_add->len; i++)
    {
      struct buddies_to_add_t *tmp;
      guint j;

      tmp = g_ptr_array_index (buddies_to_add, i);

      gabble_olpc_view_add_buddies (view, tmp->buddies,
          tmp->buddies_properties, tmp->room);

      /* Free the ressource allocated in populate_buddies_from_nodes */
      for (j = 0; j < tmp->buddies->len; j++)
        {
          TpHandle handle;
          GHashTable *props;

          handle = g_array_index (tmp->buddies, TpHandle, j);
          props = g_ptr_array_index (tmp->buddies_properties, j);

          tp_handle_unref (contact_repo, handle);
          g_hash_table_unref (props);
        }

      g_array_free (tmp->buddies, TRUE);
      g_ptr_array_free (tmp->buddies_properties, TRUE);
      g_slice_free (struct buddies_to_add_t, tmp);
    }

  g_ptr_array_free (buddies_to_add, TRUE);
  g_hash_table_destroy (activities);

  return TRUE;
}

static void
activity_added (GabbleConnection *conn,
                LmMessageNode *added)
{
  const gchar *id_str;
  guint id;
  GabbleOlpcView *view;

  id_str = lm_message_node_get_attribute (added, "id");
  if (id_str == NULL)
    return;

  id = strtoul (id_str, NULL, 10);
  view = gabble_olpc_gadget_manager_get_view (conn->olpc_gadget_manager, id);

  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  add_activities_to_view_from_node (conn, view, added);
}

/* if activity is not zero, buddies are associated with the given
 * activity room */
gboolean
add_buddies_to_view_from_node (GabbleConnection *conn,
                               GabbleOlpcView *view,
                               LmMessageNode *node,
                               const gchar *node_name,
                               TpHandle activity)
{
  GArray *buddies;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  GPtrArray *buddies_properties;
  guint i;

  buddies = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  buddies_properties = g_ptr_array_new ();

  if (!populate_buddies_from_nodes (conn, node, node_name, buddies,
        buddies_properties))
    {
      g_array_free (buddies, TRUE);
      g_ptr_array_free (buddies_properties, TRUE);
      return FALSE;
    }

  if (buddies->len == 0)
    {
      g_array_free (buddies, TRUE);
      g_ptr_array_free (buddies_properties, TRUE);
      return TRUE;
    }

  gabble_olpc_view_add_buddies (view, buddies, buddies_properties, activity);

  for (i = 0; i < buddies->len; i++)
    {
      TpHandle handle;
      GHashTable *properties;

      handle = g_array_index (buddies, TpHandle, i);
      properties = g_ptr_array_index (buddies_properties, i);

      gabble_svc_olpc_buddy_info_emit_properties_changed (conn, handle,
          properties);

      /* Free the ressource allocated in populate_buddies_from_nodes */
      tp_handle_unref (contact_repo, handle);
      g_hash_table_unref (properties);
    }

  g_array_free (buddies, TRUE);
  g_ptr_array_free (buddies_properties, TRUE);

  return TRUE;
}

static void
buddy_added (GabbleConnection *conn,
             LmMessageNode *added)
{
  const gchar *id_str;
  guint id;
  GabbleOlpcView *view;

  id_str = lm_message_node_get_attribute (added, "id");
  if (id_str == NULL)
    return;

  id = strtoul (id_str, NULL, 10);
  view = gabble_olpc_gadget_manager_get_view (conn->olpc_gadget_manager, id);

  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  add_buddies_to_view_from_node (conn, view, added, "buddy", 0);
}

static gboolean
remove_buddies_from_view_from_node (GabbleConnection *conn,
                                    GabbleOlpcView *view,
                                    LmMessageNode *node)
{
  TpHandleSet *buddies;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  NodeIter i;

  buddies = tp_handle_set_new (contact_repo);

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *buddy = node_iter_data (i);
      const gchar *jid;
      TpHandle handle;

      if (tp_strdiff (buddy->name, "buddy"))
        continue;

      jid = lm_message_node_get_attribute (buddy, "jid");

      handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);
      if (handle == 0)
        {
          DEBUG ("Invalid jid: %s", jid);
          tp_handle_set_destroy (buddies);
          return FALSE;
        }

      tp_handle_set_add (buddies, handle);
      tp_handle_unref (contact_repo, handle);
    }

  gabble_olpc_view_remove_buddies (view, buddies);
  tp_handle_set_destroy (buddies);

  return TRUE;
}

static void
buddy_removed (GabbleConnection *conn,
               LmMessageNode *removed)
{
  const gchar *id_str;
  guint id;
  GabbleOlpcView *view;

  id_str = lm_message_node_get_attribute (removed, "id");
  if (id_str == NULL)
    return;

  id = strtoul (id_str, NULL, 10);
  view = gabble_olpc_gadget_manager_get_view (conn->olpc_gadget_manager, id);

  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  remove_buddies_from_view_from_node (conn, view, removed);
}

static gboolean
remove_activities_from_view_from_node (GabbleConnection *conn,
                                       GabbleOlpcView *view,
                                       LmMessageNode *node)
{
  TpHandleSet *rooms;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  NodeIter i;

  rooms = tp_handle_set_new (room_repo);

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *activity = node_iter_data (i);
      const gchar *room;
      TpHandle handle;

      if (tp_strdiff (activity->name, "activity"))
        continue;

      room = lm_message_node_get_attribute (activity, "room");

      handle = tp_handle_ensure (room_repo, room, NULL, NULL);
      if (handle == 0)
        {
          DEBUG ("Invalid room: %s", room);
          tp_handle_set_destroy (rooms);
          return FALSE;
        }

      tp_handle_set_add (rooms, handle);
      tp_handle_unref (room_repo, handle);
    }

  gabble_olpc_view_remove_activities (view, rooms);
  tp_handle_set_destroy (rooms);

  return TRUE;
}

static void
activity_removed (GabbleConnection *conn,
                  LmMessageNode *removed)
{
  const gchar *id_str;
  guint id;
  GabbleOlpcView *view;

  id_str = lm_message_node_get_attribute (removed, "id");
  if (id_str == NULL)
    return;

  id = strtoul (id_str, NULL, 10);
  view = gabble_olpc_gadget_manager_get_view (conn->olpc_gadget_manager, id);

  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  remove_activities_from_view_from_node (conn, view, removed);
}

static gboolean
remove_buddies_from_activity_view (GabbleConnection *conn,
                                   GabbleOlpcView *view,
                                   LmMessageNode *node,
                                   const gchar *node_name,
                                   TpHandle room)
{
  GArray *buddies;
  guint i;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);

  buddies = g_array_new (FALSE, FALSE, sizeof (TpHandle));

  if (!populate_buddies_from_nodes (conn, node, node_name, buddies,
        NULL))
    {
      g_array_free (buddies, TRUE);
      return FALSE;
    }

  gabble_olpc_view_buddies_left_activity (view, buddies, room);

  /* Free the ressource allocated in populate_buddies_from_nodes */
  for (i = 0; i < buddies->len; i++)
    {
      TpHandle handle;

      handle = g_array_index (buddies, TpHandle, i);

      tp_handle_unref (contact_repo, handle);
    }

  g_array_free (buddies, TRUE);
  return TRUE;
}

static void
activity_membership_change (GabbleConnection *conn,
                            LmMessageNode *activity_node)
{
  const gchar *id_str;
  guint id;
  GabbleOlpcView *view;
  TpHandle handle;
  const gchar *room;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);

  id_str = lm_message_node_get_attribute (activity_node, "view");
  if (id_str == NULL)
    return;

  id = strtoul (id_str, NULL, 10);
  view = gabble_olpc_gadget_manager_get_view (conn->olpc_gadget_manager, id);

  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  room = lm_message_node_get_attribute (activity_node, "room");
  if (room == NULL)
    {
      DEBUG ("no room attribute");
      return;
    }

  handle = tp_handle_ensure (room_repo, room, NULL, NULL);
  if (handle == 0)
    {
      DEBUG ("Invalid room handle");
      return;
    }

  /* joined buddies */
  add_buddies_to_view_from_node (conn, view, activity_node, "joined", handle);

  /* left buddies */
  remove_buddies_from_activity_view (conn, view, activity_node, "left",
      handle);

  tp_handle_unref (room_repo, handle);
}

LmHandlerResult
conn_olpc_msg_cb (LmMessageHandler *handler,
                  LmConnection *connection,
                  LmMessage *message,
                  gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  const gchar *from;
  NodeIter i;

  from = lm_message_node_get_attribute (message->node, "from");
  if (from == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  /* If we are receiving notifications from Gadget that means we
   * previoulsy sent it a view request, so
   * conn->olpc_gadget_{buddy,activity} have been defined */
  if (tp_strdiff (from, conn->olpc_gadget_buddy) &&
      tp_strdiff (from, conn->olpc_gadget_activity))
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  for (i = node_iter (message->node); i; i = node_iter_next (i))
    {
      LmMessageNode *node = node_iter_data (i);
      const gchar *ns;

      ns = lm_message_node_get_attribute (node, "xmlns");

      if (!tp_strdiff (node->name, "change") &&
        !tp_strdiff (ns, NS_OLPC_BUDDY))
        {
          buddy_changed (conn, node);
        }
      else if (!tp_strdiff (node->name, "change") &&
          !tp_strdiff (ns, NS_OLPC_ACTIVITY))
        {
          activity_changed (conn, node);
        }
      else if (!tp_strdiff (node->name, "added") &&
          !tp_strdiff (ns, NS_OLPC_BUDDY))
        {
          buddy_added (conn, node);
        }
      else if (!tp_strdiff (node->name, "removed") &&
          !tp_strdiff (ns, NS_OLPC_BUDDY))
        {
          buddy_removed (conn, node);
        }
      else if (!tp_strdiff (node->name, "added") &&
          !tp_strdiff (ns, NS_OLPC_ACTIVITY))
        {
          activity_added (conn, node);
        }
      else if (!tp_strdiff (node->name, "removed") &&
          !tp_strdiff (ns, NS_OLPC_ACTIVITY))
        {
          activity_removed (conn, node);
        }
      else if (!tp_strdiff (node->name, "activity") &&
          !tp_strdiff (ns, NS_OLPC_ACTIVITY))
        {
          activity_membership_change (conn, node);
        }
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
connection_presences_updated_cb (GabblePresenceCache *cache,
                                 GArray *handles,
                                 GabbleConnection *conn)
{
  guint i;

  for (i = 0; i < handles->len ; i++)
    {
      TpHandle handle;

      handle = g_array_index (handles, TpHandle, i);
      connection_presence_do_update (cache, handle, conn);
    }
}

static void
disco_item_found_cb (GabbleDisco *disco,
                     GabbleDiscoItem *item,
                     GabbleConnection *conn)
{
  gboolean gadget_discovered = FALSE;

  if (tp_strdiff (item->category, "collaboration") ||
      tp_strdiff (item->type, "gadget"))
    return;

  /* we can't use g_hash_table_lookup as the value associated in the hash
   * table is NULL */
  if (g_hash_table_lookup_extended (item->features, NS_OLPC_BUDDY, NULL, NULL))
    {
      DEBUG ("buddy gadget discovered");
      gadget_discovered = TRUE;
      conn->olpc_gadget_buddy = item->jid;
    }

  if (g_hash_table_lookup_extended (item->features, NS_OLPC_ACTIVITY, NULL,
        NULL))
    {
      DEBUG ("activity gadget discovered");
      gadget_discovered = TRUE;
      conn->olpc_gadget_activity = item->jid;
    }

  if (gadget_discovered)
    {
      gabble_svc_olpc_gadget_emit_gadget_discovered (conn);
    }
}

static void
buddy_activities_changed_cb (GabbleOlpcView *view,
                             TpHandle contact,
                             GabbleConnection *conn)
{
  GPtrArray *activities;

  /* FIXME: this is not optimal as we completely ignore PEP-announced
   * activities. Ideally we should cache PEP activities. */
  activities = gabble_olpc_gadget_manager_find_buddy_activities (
      conn->olpc_gadget_manager, contact);

  gabble_svc_olpc_buddy_info_emit_activities_changed (conn, contact,
      activities);

  free_activities (activities);
}

static void
gadget_manager_new_channels_cb (GabbleOlpcGadgetManager *mgr,
                                GHashTable *channels,
                                GabbleConnection *conn)
{
  /* new views has been created */
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, channels);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (GABBLE_IS_OLPC_VIEW (key))
        {
          GabbleOlpcView *view = GABBLE_OLPC_VIEW (key);

          g_signal_connect (view, "buddy-activities-changed",
              G_CALLBACK (buddy_activities_changed_cb), conn);
        }
    }
}

void
conn_olpc_activity_properties_init (GabbleConnection *conn)
{
  /* room TpHandle => borrowed Activity */
  conn->olpc_activities_info = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);

  /* Activity from PEP
   *
   * contact TpHandle => TpHandleSet of room handles,
   *    each representing a reference to an Activity
   *
   * Special case: the entry for self_handle is the complete list of
   * activities, not just those from PEP
   */
  conn->olpc_pep_activities = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) tp_handle_set_destroy);

  /* Activity from pseudo-invitations
   *
   * contact TpHandle => TpHandleSet of room handles,
   *    each representing a reference to an Activity
   *
   * Special case: there is never an entry for self_handle
   */
  conn->olpc_invited_activities = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) tp_handle_set_destroy);

  /* Current activity
   *
   * contact TpHandle => reffed GabbleOlpcActivity
   */
  conn->olpc_current_act = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_object_unref);

  conn->olpc_gadget_buddy = NULL;
  conn->olpc_gadget_activity = NULL;
  conn->olpc_gadget_publish = FALSE;

  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), NULL);

  g_signal_connect (TP_CHANNEL_MANAGER (conn->muc_factory), "new-channels",
      G_CALLBACK (muc_factory_new_channels_cb), conn);

  g_signal_connect (conn->disco, "item-found",
      G_CALLBACK (disco_item_found_cb), conn);

  g_signal_connect (conn->presence_cache, "presences-updated",
      G_CALLBACK (connection_presences_updated_cb), conn);

  g_signal_connect (conn->olpc_gadget_manager, "new-channels",
      G_CALLBACK (gadget_manager_new_channels_cb), conn);
}

static void
unref_activities_in_each_set (TpHandle handle,
                            TpHandleSet *set,
                            GabbleConnection *conn)
{
  if (set != NULL)
    {
      tp_handle_set_foreach (set,
          decrement_contacts_activities_set_foreach, conn);
    }
}

void
conn_olpc_activity_properties_dispose (GabbleConnection *self)
{
  g_hash_table_destroy (self->olpc_current_act);
  self->olpc_current_act = NULL;

  g_hash_table_foreach (self->olpc_pep_activities,
      (GHFunc) unref_activities_in_each_set, self);
  g_hash_table_destroy (self->olpc_pep_activities);
  self->olpc_pep_activities = NULL;

  g_hash_table_foreach (self->olpc_invited_activities,
      (GHFunc) unref_activities_in_each_set, self);
  g_hash_table_destroy (self->olpc_invited_activities);
  self->olpc_invited_activities = NULL;

  g_hash_table_destroy (self->olpc_activities_info);
  self->olpc_activities_info = NULL;
}

void
olpc_activity_properties_iface_init (gpointer g_iface,
                                     gpointer iface_data)
{
  GabbleSvcOLPCActivityPropertiesClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_olpc_activity_properties_implement_##x (\
    klass, olpc_activity_properties_##x)
  IMPLEMENT(get_properties);
  IMPLEMENT(set_properties);
#undef IMPLEMENT
}

static gboolean
send_presence_to_gadget (GabbleConnection *conn,
                         LmMessageSubType sub_type,
                         GError **error)
{
  return gabble_connection_send_presence (conn, sub_type,
      conn->olpc_gadget_buddy, NULL, error);
}

static void
olpc_gadget_publish (GabbleSvcOLPCGadget *iface,
                     gboolean publish,
                     DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  GError *error = NULL;

  if (!check_gadget_buddy (conn, context))
    {
      DEBUG ("Server does not provide Gadget Buddy service");
      return;
    }

  conn->olpc_gadget_publish = publish;

  if (publish)
    {
      /* FIXME: we should check if we are already registered before. Not
       * convenient as roster.[ch] is handle oriented */
      /* FIXME: add to roster ? */
      if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_SUBSCRIBE,
            &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }

      /* FIXME: Should we invite Gadget to all our public activities? */
    }
  else
    {
      /* FIXME: remove from roster ? */
      if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE,
            &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }

      if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED,
            &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }

  gabble_svc_olpc_gadget_return_from_publish (context);
}

LmHandlerResult
conn_olpc_presence_cb (LmMessageHandler *handler,
                       LmConnection *connection,
                       LmMessage *presence,
                       gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  LmMessageNode *pres_node;
  const gchar *from;
  LmMessageSubType sub_type;
  GError *error = NULL;

  if (!check_gadget_buddy (conn, NULL))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  pres_node = lm_message_get_node (presence);
  from = lm_message_node_get_attribute (pres_node, "from");
  if (from == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* We are only interested about presence from Gadget */
  if (tp_strdiff (from, conn->olpc_gadget_buddy))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  sub_type = lm_message_get_sub_type (presence);

  if (sub_type == LM_MESSAGE_SUB_TYPE_SUBSCRIBE)
    {
      if (conn->olpc_gadget_publish)
        {
          DEBUG ("accept Gadget subscribe request");

          if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_SUBSCRIBED,
                &error))
            {
              DEBUG ("failed to send subscribed presence to Gadget: %s",
                  error->message);
              g_error_free (error);
            }
        }
      else
        {
          DEBUG ("decline Gadget subscribe request");

          if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED,
                &error))
            {
              DEBUG ("failed to send subscribed presence to Gadget: %s",
                  error->message);
              g_error_free (error);
            }
        }
    }
  else if (sub_type == LM_MESSAGE_SUB_TYPE_NOT_SET ||
      sub_type == LM_MESSAGE_SUB_TYPE_AVAILABLE)
    {
      DEBUG ("Got presence from Gadget. Close open views if any");
      gabble_olpc_gadget_manager_close_all_views (conn->olpc_gadget_manager);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

void
conn_olpc_gadget_properties_getter (GObject *object,
                                   GQuark interface,
                                   GQuark name,
                                   GValue *value,
                                   gpointer getter_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (object);

  if (!tp_strdiff (g_quark_to_string (name), "GadgetAvailable"))
    {
      g_value_set_boolean (value, (conn->olpc_gadget_buddy != NULL) ||
          (conn->olpc_gadget_activity != NULL));
    }
  else
    {
      g_assert_not_reached ();
    }
}

void
olpc_gadget_iface_init (gpointer g_iface,
                        gpointer iface_data)
{
  GabbleSvcOLPCGadgetClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_olpc_gadget_implement_##x (\
    klass, olpc_gadget_##x)
  IMPLEMENT(publish);
#undef IMPLEMENT
}
