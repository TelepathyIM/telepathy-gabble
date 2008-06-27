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

#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include "debug.h"
#include "channel-manager.h"
#include "connection.h"
#include "muc-channel.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "pubsub.h"
#include "disco.h"
#include "util.h"
#include "olpc-view.h"
#include "olpc-activity.h"

static gboolean
update_activities_properties (GabbleConnection *conn, const gchar *contact,
    LmMessage *msg);

typedef struct
{
  TpHandle handle;
  GHashTable *properties;
  gchar *id;

  GabbleConnection *conn;
  TpHandleRepoIface *room_repo;
  guint refcount;
} ActivityInfo;

static const gchar*
activity_info_get_room (ActivityInfo *info)
{
  return tp_handle_inspect (info->room_repo, info->handle);
}

static ActivityInfo*
activity_info_new (GabbleConnection *conn,
                   TpHandle handle)
{
  ActivityInfo *info;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_ROOM);

  g_assert (tp_handle_is_valid (room_repo, handle, NULL));

  info = g_slice_new0 (ActivityInfo);

  info->handle = handle;
  tp_handle_ref (room_repo, handle);
  info->properties = NULL;
  info->id = NULL;

  info->conn = conn;
  info->room_repo = room_repo;
  info->refcount = 1;

  DEBUG ("%s (%d)\n", activity_info_get_room (info), info->handle);

  return info;
}

static void
activity_info_free (ActivityInfo *info)
{
  if (info->properties != NULL)
    {
      g_hash_table_destroy (info->properties);
    }
  g_free (info->id);

  tp_handle_unref (info->room_repo, info->handle);

  g_slice_free (ActivityInfo, info);
}

static void
activity_info_set_properties (ActivityInfo *info,
                              GHashTable *properties)
{
  if (info->properties != NULL)
    {
      g_hash_table_destroy (info->properties);
    }

  info->properties = properties;
}

static void
activity_info_unref (ActivityInfo *info)
{
  info->refcount--;

  DEBUG ("unref: %s (%d) refcount: %d\n",
      activity_info_get_room (info), info->handle,
      info->refcount);

  if (info->refcount == 0)
    {
      g_hash_table_remove (info->conn->olpc_activities_info,
          GUINT_TO_POINTER (info->handle));
    }
}

static gboolean
activity_info_is_visible (ActivityInfo *info)
{
  GValue *gv;

  /* false if incomplete */
  if (info->id == NULL || info->properties == NULL)
    return FALSE;

  gv = g_hash_table_lookup (info->properties, "private");

  if (gv == NULL)
    {
      return FALSE;
    }

  /* if they put something non-boolean in it, err on the side of privacy */
  if (!G_VALUE_HOLDS_BOOLEAN (gv))
    return FALSE;

  /* if they specified a privacy level, go with it */
  return !g_value_get_boolean (gv);
}

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
  const GabbleDiscoItem *item;

  if (conn->olpc_gadget_buddy != NULL)
    return TRUE;

  item = gabble_disco_service_find (conn->disco, "collaboration", "gadget",
      NS_OLPC_BUDDY);

  if (item != NULL)
    conn->olpc_gadget_buddy = item->jid;

  if (conn->olpc_gadget_buddy == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Server does not provide Gadget Buddy service" };

      DEBUG ("%s", error.message);
      if (context != NULL)
        dbus_g_method_return_error (context, &error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
check_gadget_activity (GabbleConnection *conn,
                       DBusGMethodInvocation *context)
{
  const GabbleDiscoItem *item;

  if (conn->olpc_gadget_activity != NULL)
    return TRUE;

  item = gabble_disco_service_find (conn->disco, "collaboration", "gadget",
      NS_OLPC_ACTIVITY);

  if (item != NULL)
    conn->olpc_gadget_activity = item->jid;

  if (conn->olpc_gadget_activity == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Server does not provide Gadget Activity service" };

      DEBUG ("%s", error.message);
      if (context != NULL)
        dbus_g_method_return_error (context, &error);
      return FALSE;
    }

  return TRUE;
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
                  error_node);

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
                  error_node);

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

  for (buddy = query->children; buddy != NULL; buddy = buddy->next)
    {
      const gchar *jid;

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

      DEBUG ("PEP query failed. Let's try to search this buddy");
      get_buddy_properties_from_search (conn, buddy, context);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  node = lm_message_node_find_child (reply_msg->node, "properties");
  properties = lm_message_node_extract_properties (node, "property");

  gabble_svc_olpc_buddy_info_return_from_get_properties (context, properties);
  g_hash_table_destroy (properties);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
find_view_having_properties_for_buddy (gpointer id,
                                       gpointer value,
                                       gpointer buddy)
{
  GabbleOlpcView *view = GABBLE_OLPC_VIEW (value);
  TpHandle handle = GPOINTER_TO_UINT (buddy);

  return gabble_olpc_view_get_buddy_properties (view, handle) != NULL;
}

static GHashTable *
find_buddy_properties_from_views (GabbleConnection *conn,
                                  TpHandle buddy)
{
  GabbleOlpcView *view;

  view = g_hash_table_find (conn->olpc_views,
      find_view_having_properties_for_buddy, GUINT_TO_POINTER (buddy));
  if (view == NULL)
    return NULL;

  return gabble_olpc_view_get_buddy_properties (view, buddy);
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
  properties = find_buddy_properties_from_views (conn, contact);
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

#ifndef HAS_G_HASH_TABLE_REMOVE_ALL
static gboolean
_hash_table_remove_yes (gpointer key, gpointer value, gpointer user_data)
{
  return TRUE;
}

static void
our_g_hash_table_remove_all (GHashTable *table)
{
  g_hash_table_foreach_remove (table, _hash_table_remove_yes, NULL);
}

#define g_hash_table_remove_all our_g_hash_table_remove_all
#endif

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

static ActivityInfo*
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
  LmMessageNode *node;
  TpHandleSet *activities_set, *old_activities;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);

  activities_node = lm_message_node_find_child (msg->node, "activities");

  activities_set = tp_handle_set_new (room_repo);
  for (node = (activities_node != NULL ? activities_node->children : NULL);
       node;
       node = node->next)
    {
      const gchar *act_id;
      const gchar *room;
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
      pubsub_query (conn, from, NS_OLPC_ACTIVITY_PROPS,
          get_activity_properties_reply_cb, NULL);
    }
}

struct find_activities_of_buddy_ctx
{
  TpHandle buddy;
  GHashTable *activities;
};

static void
find_activities_of_buddy (TpHandle contact,
                          GabbleOlpcView *view,
                          struct find_activities_of_buddy_ctx *ctx)
{
  GPtrArray *act;
  guint i;

  act = gabble_olpc_view_get_buddy_activities (view, ctx->buddy);

  for (i = 0; i < act->len; i++)
    {
      GabbleOlpcActivity *activity;

      activity = g_ptr_array_index (act, i);
      g_hash_table_insert (ctx->activities, GUINT_TO_POINTER (activity->room),
          activity);
    }

  g_ptr_array_free (act, TRUE);
}

static void
copy_activity_to_array (TpHandle room,
                        GabbleOlpcActivity *activity,
                        GPtrArray *activities)
{
  GValue gvalue = { 0 };

  if (activity->id == NULL)
  {
    DEBUG ("... activity #%u has no ID, skipping", room);
    return;
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

static GPtrArray *
find_buddy_activities_from_views (GabbleConnection *conn,
                                  TpHandle contact)
{
  GPtrArray *result;
  struct find_activities_of_buddy_ctx ctx;

  result = g_ptr_array_new ();

  /* We use a hash table first so we won't add twice the same activity */
  ctx.activities = g_hash_table_new (g_direct_hash, g_direct_equal);
  ctx.buddy = contact;

  g_hash_table_foreach (conn->olpc_views, (GHFunc) find_activities_of_buddy,
      &ctx);

  /* Now compute the result array using the hash table */
  g_hash_table_foreach (ctx.activities, (GHFunc) copy_activity_to_array,
      result);

  g_hash_table_destroy (ctx.activities);

  return result;
}

static void
return_buddy_activities_from_views (GabbleConnection *conn,
                                    TpHandle contact,
                                    DBusGMethodInvocation *context)
{
  GPtrArray *activities;

  activities = find_buddy_activities_from_views (conn, contact);
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
              return;
            }

          g_object_ref (activity);

          DEBUG ("ref: %s (%d) refcount: %d\n",
              gabble_olpc_activity_get_room (activity),
              activity->room, G_OBJECT (activity)->ref_count);
        }

      g_object_set (activity, "id", id, NULL);

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

static ActivityInfo*
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

  tp_handle_set_add (activities_set, room_handle);

  return activity;
}

static gboolean
extract_current_activity (GabbleConnection *conn,
                          LmMessageNode *node,
                          const gchar *contact,
                          const gchar **id,
                          guint *handle)
{
  const gchar *room;
  GabbleOlpcActivity *activity;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  if (node == NULL)
    return FALSE;

  *id = lm_message_node_get_attribute (node, "type");

  room = lm_message_node_get_attribute (node, "room");
  if (room == NULL || room[0] == '\0')
    return FALSE;

  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    return FALSE;

  activity = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));

  if (activity == NULL)
    {
      /* Humm we received as current activity an activity we don't know yet.
       * If the remote user doesn't announce this activity
       * in his next activities list, information about
       * it will be freed */

      DEBUG ("unknown current activity %s", room);

      activity = add_activity_info_in_set (conn, room_handle, contact,
          conn->olpc_pep_activities);
    }

  tp_handle_unref (room_repo, room_handle);

  if (activity == NULL)
    return FALSE;

  *handle = activity->room;

  return TRUE;
}

static LmHandlerResult
get_current_activity_reply_cb (GabbleConnection *conn,
                               LmMessage *sent_msg,
                               LmMessage *reply_msg,
                               GObject *object,
                               gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;
  guint room_handle;
  const gchar *activity;
  LmMessageNode *node;
  const gchar *from;

  if (!check_query_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  from = lm_message_node_get_attribute (reply_msg->node, "from");
  node = lm_message_node_find_child (reply_msg->node, "activity");
  if (!extract_current_activity (conn, node, from, &activity, &room_handle))
    {
      activity = "";
      room_handle = 0;
    }

  DEBUG ("GetCurrentActivity returns (\"%s\", room#%u)", activity,
      room_handle);
  gabble_svc_olpc_buddy_info_return_from_get_current_activity (context,
      activity, room_handle);
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

  DEBUG ("called for contact#%u", contact);

  gabble_connection_ensure_capabilities (conn, PRESENCE_CAP_OLPC_1);
  if (!check_pep (conn, context))
    return;

  jid = inspect_contact (base, context, contact);
  if (jid == NULL)
    return;

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
      GINT_TO_POINTER (base->self_handle));

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
  guint room_handle;
  const gchar *activity;
  TpBaseConnection *base = (TpBaseConnection*) conn;

  if (handle == base->self_handle)
    /* Ignore echoed pubsub notifications */
    return TRUE;

  from = lm_message_node_get_attribute (msg->node, "from");
  node = lm_message_node_find_child (msg->node, "activity");
  if (extract_current_activity (conn, node, from, &activity, &room_handle))
    {
      DEBUG ("emitting CurrentActivityChanged(contact#%u, ID \"%s\", room#%u)",
             handle, activity, room_handle);
      gabble_svc_olpc_buddy_info_emit_current_activity_changed (conn, handle,
          activity, room_handle);
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
  if (!check_gadget_activity (conn, NULL))
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
      /* activity becomes visible. Invite gadget */
      invite_gadget (conn, muc_channel);
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
  LmMessageNode *node, *properties_node;

  node = lm_message_node_find_child (msg->node, "activities");
  if (node == NULL)
    return FALSE;

  for (properties_node = node->children; properties_node != NULL;
      properties_node = properties_node->next)
    {
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
  TpBaseConnection *base = (TpBaseConnection*) conn;

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
  GabbleExportableChannel *chan = GABBLE_EXPORTABLE_CHANNEL (key);
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
      DEBUG ("No jid attribute in change message. Discard");
      return;
    }

  id_str = lm_message_node_get_attribute (change, "id");
  if (id_str == NULL)
    {
      DEBUG ("No view ID attribute in change message. Discard");
      return;
    }

  id = strtoul (id_str, NULL, 10);
  view = g_hash_table_lookup (conn->olpc_views, GUINT_TO_POINTER (id));
  if (view == NULL)
    {
      DEBUG ("No active view with ID %u", id);
      return;
    }

  /* FIXME: Should we really have to ensure the handle? If we receive changes
   * notifications that means this contact is in your search frame so maybe
   * we should keep a ref on his handle */
  handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);
  if (handle == 0)
    {
      DEBUG ("Invalid jid: %s. Discard", jid);
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

      if (view != NULL)
        {
          gabble_olpc_view_set_buddy_properties (view, handle, properties);
        }

      gabble_svc_olpc_buddy_info_emit_properties_changed (conn, handle,
          properties);

      g_hash_table_unref (properties);
    }

  node = lm_message_node_get_child_with_namespace (change,
      "activity", NS_OLPC_CURRENT_ACTIVITY);
  if (node != NULL)
    {
      /* Buddy current activity change */
      const gchar *activity;
      TpHandle room_handle;

      if (!extract_current_activity (conn, node, jid, &activity, &room_handle))
        {
          activity = "";
          room_handle = 0;
        }

      gabble_svc_olpc_buddy_info_emit_current_activity_changed (conn, handle,
          activity, room_handle);
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
                             GArray *buddies,
                             GPtrArray *buddies_properties)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *buddy;

  for (buddy = node->children; buddy != NULL; buddy = buddy->next)
    {
      const gchar *jid;
      LmMessageNode *properties_node;
      GHashTable *properties;
      TpHandle handle;

      if (tp_strdiff (buddy->name, "buddy"))
        continue;

      jid = lm_message_node_get_attribute (buddy, "jid");

      handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);
      if (handle == 0)
        {
          guint i;

          DEBUG ("Invalid jid: %s", jid);

          for (i = 0; i < buddies->len; i++)
            tp_handle_unref (contact_repo, g_array_index (buddies, TpHandle,
                  i));

          g_ptr_array_foreach (buddies_properties, (GFunc) g_hash_table_unref,
              NULL);

          return FALSE;
        }

      g_array_append_val (buddies, handle);

      properties_node = lm_message_node_get_child_with_namespace (buddy,
          "properties", NS_OLPC_BUDDY_PROPS);
      properties = lm_message_node_extract_properties (properties_node,
          "property");

      g_ptr_array_add (buddies_properties, properties);
    }

  return TRUE;
}

static gboolean
add_activities_to_view_from_node (GabbleConnection *conn,
                                  GabbleOlpcView *view,
                                  LmMessageNode *node)
{
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_ROOM);
  GHashTable *activities;
  LmMessageNode *activity_node;

  activities = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      g_object_unref );

  for (activity_node = node->children; activity_node != NULL;
      activity_node = activity_node->next)
    {
      const gchar *jid, *act_id;
      LmMessageNode *properties_node;
      GHashTable *properties;
      TpHandle handle;
      GabbleOlpcActivity *activity;
      GArray *buddies;
      GPtrArray *buddies_properties;

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

      properties_node = lm_message_node_get_child_with_namespace (activity_node,
          "properties", NS_OLPC_ACTIVITY_PROPS);
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

      buddies = g_array_new (FALSE, FALSE, sizeof (TpHandle));
      buddies_properties = g_ptr_array_new ();

      if (!populate_buddies_from_nodes (conn, activity_node, buddies,
            buddies_properties))
        {
          g_array_free (buddies, TRUE);
          g_ptr_array_free (buddies_properties, TRUE);
          continue;
        }

      gabble_olpc_view_add_buddies (view, buddies, buddies_properties, handle);

      g_array_free (buddies, TRUE);
      g_ptr_array_free (buddies_properties, TRUE);
    }

  gabble_olpc_view_add_activities (view, activities);

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
  view = g_hash_table_lookup (conn->olpc_views, GUINT_TO_POINTER (id));
  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  add_activities_to_view_from_node (conn, view, added);
}

static gboolean
add_buddies_to_view_from_node (GabbleConnection *conn,
                               GabbleOlpcView *view,
                               LmMessageNode *node)
{
  GArray *buddies;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_CONTACT);
  GPtrArray *buddies_properties;
  guint i;

  buddies = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  buddies_properties = g_ptr_array_new ();

  if (!populate_buddies_from_nodes (conn, node, buddies, buddies_properties))
    {
      g_array_free (buddies, TRUE);
      g_ptr_array_free (buddies_properties, TRUE);
      return FALSE;
    }

  gabble_olpc_view_add_buddies (view, buddies, buddies_properties, 0);

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
  view = g_hash_table_lookup (conn->olpc_views, GUINT_TO_POINTER (id));
  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  add_buddies_to_view_from_node (conn, view, added);
}

static gboolean
remove_buddies_from_view_from_node (GabbleConnection *conn,
                                    GabbleOlpcView *view,
                                    LmMessageNode *node)
{
  TpHandleSet *buddies;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *buddy;

  buddies = tp_handle_set_new (contact_repo);

  for (buddy = node->children; buddy != NULL; buddy = buddy->next)
    {

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
  view = g_hash_table_lookup (conn->olpc_views, GUINT_TO_POINTER (id));
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
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_ROOM);
  LmMessageNode *activity;

  rooms = tp_handle_set_new (room_repo);

  for (activity = node->children; activity != NULL; activity = activity->next)
    {
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
  view = g_hash_table_lookup (conn->olpc_views, GUINT_TO_POINTER (id));
  if (view == NULL)
    {
      DEBUG ("no view with ID %u", id);
      return;
    }

  remove_activities_from_view_from_node (conn, view, removed);
}

LmHandlerResult
conn_olpc_msg_cb (LmMessageHandler *handler,
                  LmConnection *connection,
                  LmMessage *message,
                  gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  const gchar *from;
  LmMessageNode *node;

  /* FIXME: We call that to be sure conn->olpc_gadget_{buddy,activity} are
   * initialised if the service was discovered.
   * Are we supposed to receive changes notifications message from gadget
   * if we didn't send it a search request before? If not we can assume
   * these check_gadget_* functions were already called and just check if
   * conn->olpc_gadget_{buddy,activity} are not NULL.
   */
  if (!check_gadget_buddy (conn, NULL) && !check_gadget_activity (conn, NULL))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  from = lm_message_node_get_attribute (message->node, "from");
  if (from == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  /* FIXME: we shouldn't hardcode that */
  if (tp_strdiff (from, conn->olpc_gadget_buddy) &&
      tp_strdiff (from, conn->olpc_gadget_activity))
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  for (node = message->node->children; node != NULL; node = node->next)
    {
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
      /* TODO: join, left and closed announcements */
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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

  /* Active views
   *
   * view id guint => GabbleOlpcView
   */
  conn->olpc_views = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);

  conn->olpc_gadget_buddy = NULL;
  conn->olpc_gadget_activity = NULL;

  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), NULL);

  g_signal_connect (GABBLE_CHANNEL_MANAGER (conn->muc_factory), "new-channels",
      G_CALLBACK (muc_factory_new_channels_cb), conn);

  g_signal_connect (conn->presence_cache, "presence-update",
      G_CALLBACK (connection_presence_update_cb), conn);
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

static void
view_closed_cb (GabbleOlpcView *view,
                GabbleConnection *conn)
{
  guint id;

  g_object_get (view, "id", &id, NULL);
  g_hash_table_remove (conn->olpc_views, GUINT_TO_POINTER (id));
}

static GabbleOlpcView *
create_view (GabbleConnection *conn,
             GabbleOlpcViewType type)
{
  guint id;
  GabbleOlpcView *view;

  /* Look for a free ID */
  for (id = 0; id < G_MAXUINT &&
      g_hash_table_lookup (conn->olpc_views, GUINT_TO_POINTER (id))
      != NULL; id++);

  if (id == G_MAXUINT)
    {
      /* All the ID's are allocated */
      return NULL;
    }

  view = gabble_olpc_view_new (conn, type, id);
  g_hash_table_insert (conn->olpc_views, GUINT_TO_POINTER (id), view);

  g_signal_connect (view, "closed", G_CALLBACK (view_closed_cb), conn);

  return view;
}

static LmHandlerResult
buddy_query_result_cb (GabbleConnection *conn,
                       LmMessage *sent_msg,
                       LmMessage *reply_msg,
                       GObject *_view,
                       gpointer user_data)
{
  LmMessageNode *view_node;
  GabbleOlpcView *view = GABBLE_OLPC_VIEW (_view);

  view_node = lm_message_node_get_child_with_namespace (reply_msg->node, "view",
      NS_OLPC_BUDDY);
  if (view_node == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  add_buddies_to_view_from_node (conn, view, view_node);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_gadget_request_random_buddies (GabbleSvcOLPCGadget *iface,
                                    guint max,
                                    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *query;
  gchar *max_str, *id_str;
  gchar *object_path;
  guint id;
  GabbleOlpcView *view;

  if (!check_gadget_buddy (conn, context))
    return;

  if (max == 0)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "max have to be greater than 0" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      return;
    }

  view = create_view (conn, GABBLE_OLPC_VIEW_TYPE_BUDDY);
  if (view == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "can't create view" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (view,
      "id", &id,
      "object-path", &object_path,
      NULL);

  max_str = g_strdup_printf ("%u", max);
  id_str = g_strdup_printf ("%u", id);

  query = lm_message_build_with_sub_type (conn->olpc_gadget_buddy,
      LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET,
      '(', "view", "",
          '@', "xmlns", NS_OLPC_BUDDY,
          '@', "id", id_str,
          '(', "random", "",
            '@', "max", max_str,
          ')',
      ')',
      NULL);

  g_free (max_str);
  g_free (id_str);

  if (!_gabble_connection_send_with_reply (conn, query,
        buddy_query_result_cb, G_OBJECT (view), NULL, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send buddy search query to server" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      lm_message_unref (query);
      g_free (object_path);
      return;
    }

  gabble_svc_olpc_gadget_return_from_request_random_buddies (context,
      object_path);

  g_free (object_path);
  lm_message_unref (query);
}

static void
olpc_gadget_search_buddies_by_properties (GabbleSvcOLPCGadget *iface,
                                          GHashTable *properties,
                                          DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *query;
  LmMessageNode *properties_node;
  gchar *id_str;
  gchar *object_path;
  guint id;
  GabbleOlpcView *view;

  if (!check_gadget_buddy (conn, context))
    return;

  view = create_view (conn, GABBLE_OLPC_VIEW_TYPE_BUDDY);
  if (view == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "can't create view" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (view,
      "id", &id,
      "object-path", &object_path,
      NULL);

  id_str = g_strdup_printf ("%u", id);

  query = lm_message_build_with_sub_type (conn->olpc_gadget_buddy,
      LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET,
      '(', "view", "",
          '@', "xmlns", NS_OLPC_BUDDY,
          '@', "id", id_str,
          '(', "buddy", "",
            '(', "properties", "",
              '*', &properties_node,
              '@', "xmlns", NS_OLPC_BUDDY_PROPS,
            ')',
          ')',
      ')',
      NULL);

  g_free (id_str);

  lm_message_node_add_children_from_properties (properties_node, properties,
      "property");

  if (!_gabble_connection_send_with_reply (conn, query,
        buddy_query_result_cb, G_OBJECT (view), NULL, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send buddy search query to server" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      lm_message_unref (query);
      g_free (object_path);
      return;
    }

  gabble_svc_olpc_gadget_return_from_search_buddies_by_properties (context,
      object_path);

  g_free (object_path);
  lm_message_unref (query);
}

static LmHandlerResult
activity_query_result_cb (GabbleConnection *conn,
                          LmMessage *sent_msg,
                          LmMessage *reply_msg,
                          GObject *_view,
                          gpointer user_data)
{
  LmMessageNode *view_node;
  GabbleOlpcView *view = GABBLE_OLPC_VIEW (_view);

  view_node = lm_message_node_get_child_with_namespace (reply_msg->node, "view",
      NS_OLPC_ACTIVITY);
  if (view_node == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  add_activities_to_view_from_node (conn, view, view_node);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_gadget_request_random_activities (GabbleSvcOLPCGadget *iface,
                                       guint max,
                                       DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *query;
  gchar *max_str, *id_str;
  gchar *object_path;
  guint id;
  GabbleOlpcView *view;

  if (!check_gadget_activity (conn, context))
    return;

  if (max == 0)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "max have to be greater than 0" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      return;
    }

  view = create_view (conn, GABBLE_OLPC_VIEW_TYPE_ACTIVITY);
  if (view == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "can't create view" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (view,
      "id", &id,
      "object-path", &object_path,
      NULL);

  max_str = g_strdup_printf ("%u", max);
  id_str = g_strdup_printf ("%u", id);

  query = lm_message_build_with_sub_type (conn->olpc_gadget_activity,
      LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET,
      '(', "view", "",
          '@', "xmlns", NS_OLPC_ACTIVITY,
          '@', "id", id_str,
          '(', "random", "",
            '@', "max", max_str,
          ')',
      ')',
      NULL);

  g_free (max_str);
  g_free (id_str);

  if (!_gabble_connection_send_with_reply (conn, query,
        activity_query_result_cb, G_OBJECT (view), NULL, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send activity search query to server" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      lm_message_unref (query);
      g_free (object_path);
      return;
    }

  gabble_svc_olpc_gadget_return_from_request_random_activities (context,
      object_path);

  g_free (object_path);
  lm_message_unref (query);
}

static void
olpc_gadget_search_activities_by_properties (GabbleSvcOLPCGadget *iface,
                                             GHashTable *properties,
                                             DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *query;
  LmMessageNode *properties_node;
  gchar *id_str;
  gchar *object_path;
  guint id;
  GabbleOlpcView *view;

  if (!check_gadget_activity (conn, context))
    return;

  view = create_view (conn, GABBLE_OLPC_VIEW_TYPE_ACTIVITY);
  if (view == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "can't create view" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (view,
      "id", &id,
      "object-path", &object_path,
      NULL);

  id_str = g_strdup_printf ("%u", id);

  query = lm_message_build_with_sub_type (conn->olpc_gadget_activity,
      LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET,
      '(', "view", "",
          '@', "xmlns", NS_OLPC_ACTIVITY,
          '@', "id", id_str,
          '(', "activity", "",
            '(', "properties", "",
              '*', &properties_node,
              '@', "xmlns", NS_OLPC_ACTIVITY_PROPS,
            ')',
          ')',
      ')',
      NULL);

  g_free (id_str);

  lm_message_node_add_children_from_properties (properties_node, properties,
      "property");

  if (!_gabble_connection_send_with_reply (conn, query,
        activity_query_result_cb, G_OBJECT (view), NULL, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send activity search query to server" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      lm_message_unref (query);
      g_free (object_path);
      return;
    }

  gabble_svc_olpc_gadget_return_from_search_activities_by_properties (context,
      object_path);

  g_free (object_path);
  lm_message_unref (query);
}

static void
olpc_gadget_search_activities_by_participants (GabbleSvcOLPCGadget *iface,
                                               const GArray *participants,
                                               DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *query;
  LmMessageNode *activity_node;
  gchar *id_str;
  gchar *object_path;
  guint id, i;
  GabbleOlpcView *view;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);

  if (!check_gadget_activity (conn, context))
    return;

  view = create_view (conn, GABBLE_OLPC_VIEW_TYPE_ACTIVITY);
  if (view == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "can't create view" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (view,
      "id", &id,
      "object-path", &object_path,
      NULL);

  id_str = g_strdup_printf ("%u", id);

  query = lm_message_build_with_sub_type (conn->olpc_gadget_activity,
      LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET,
      '(', "view", "",
          '@', "xmlns", NS_OLPC_ACTIVITY,
          '@', "id", id_str,
          '(', "activity", "",
            '*', &activity_node,
          ')',
      ')',
      NULL);

  g_free (id_str);

  for (i = 0; i < participants->len; i++)
    {
      LmMessageNode *buddy;
      const gchar *jid;

      jid = tp_handle_inspect (contact_repo,
          g_array_index (participants, TpHandle, i));

      buddy = lm_message_node_add_child (activity_node, "buddy", "");
      lm_message_node_set_attribute (buddy, "jid", jid);
    }

  if (!_gabble_connection_send_with_reply (conn, query,
        activity_query_result_cb, G_OBJECT (view), NULL, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send activity search query to server" };

      DEBUG ("%s", error.message);
      dbus_g_method_return_error (context, &error);
      lm_message_unref (query);
      g_free (object_path);
      return;
    }

  gabble_svc_olpc_gadget_return_from_search_activities_by_participants (context,
      object_path);

  g_free (object_path);
  lm_message_unref (query);
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
    return;

  if (publish)
    {
      /* FIXME: we should check if we are already registered before */
      /* FIXME: add to roster ? */
      if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_SUBSCRIBE,
            &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }

      if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_SUBSCRIBED,
            &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }
  else
    {
      if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_SUBSCRIBE,
            &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }

      if (!send_presence_to_gadget (conn, LM_MESSAGE_SUB_TYPE_SUBSCRIBED,
            &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }

  gabble_svc_olpc_gadget_return_from_publish (context);
}

void
olpc_gadget_iface_init (gpointer g_iface,
                        gpointer iface_data)
{
  GabbleSvcOLPCGadgetClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_olpc_gadget_implement_##x (\
    klass, olpc_gadget_##x)
  IMPLEMENT(request_random_buddies);
  IMPLEMENT(search_buddies_by_properties);
  IMPLEMENT(request_random_activities);
  IMPLEMENT(search_activities_by_properties);
  IMPLEMENT(search_activities_by_participants);
  IMPLEMENT(publish);
#undef IMPLEMENT
}
