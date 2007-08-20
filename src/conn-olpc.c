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

#include <telepathy-glib/channel-iface.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include "debug.h"
#include "gabble-connection.h"
#include "gabble-muc-channel.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "pubsub.h"
#include "util.h"

#define ACTIVITY_PAIR_TYPE \
    dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, \
        G_TYPE_INVALID)

static gboolean
update_activities_properties (GabbleConnection *conn, LmMessage *msg);

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

/* Returns TRUE if we have an ID and properties for the activity, else FALSE.
 */
static gboolean
activity_info_contribute_properties (ActivityInfo *info,
                                     LmMessageNode *parent)
{
  LmMessageNode *props_node;

  if (info->id == NULL || info->properties == NULL)
    return FALSE;

  props_node = lm_message_node_add_child (parent,
      "properties", "");
  lm_message_node_set_attribute (props_node, "xmlns", NS_OLPC_ACTIVITY_PROPS);
  lm_message_node_set_attribute (props_node, "room",
      activity_info_get_room (info));
  lm_message_node_set_attribute (props_node, "activity", info->id);
  lm_message_node_add_children_from_properties (props_node, info->properties,
      "property");
  return TRUE;
}

static void
decrement_contacts_activities_list_foreach (TpHandleSet *set,
                                            TpHandle handle,
                                            gpointer data)
{
  GabbleConnection *conn = data;
  ActivityInfo *info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (handle));

  activity_info_unref (info);
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
get_properties_reply_cb (GabbleConnection *conn,
                         LmMessage *sent_msg,
                         LmMessage *reply_msg,
                         GObject *object,
                         gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;
  GHashTable *properties;
  LmMessageNode *node;

  if (!check_query_reply_msg (reply_msg, context))
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;

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

  DEBUG ("called");

  if (!check_pep (conn, context))
    return;

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
preload_buddy_properties_quark ()
{
  static GQuark q = 0;
  if (q == 0)
    {
      q = g_quark_from_static_string
        ("GabbleConnection.preload_buddy_properties_quark");
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

      gabble_g_hash_table_update (preload, properties,
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
  update_activities_properties (conn, reply_msg);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static ActivityInfo*
add_activity_info (GabbleConnection *conn,
                   TpHandle handle)
{
  ActivityInfo *info;

  info = activity_info_new (conn, handle);

  g_hash_table_insert (conn->olpc_activities_info,
      GUINT_TO_POINTER (handle), info);

  return info;
}

static GPtrArray*
extract_activities (GabbleConnection *conn,
                    LmMessage *msg)
{
  GPtrArray *activities;
  LmMessageNode *activities_node;
  LmMessageNode *node;
  TpHandleSet *activities_list, *old_activities, *invited_activities;
  const gchar *from;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle from_handle;
  TpIntSetIter iter = { NULL, 0 };

  activities_node = lm_message_node_find_child (msg->node, "activities");
  activities = g_ptr_array_new ();

  from = lm_message_node_get_attribute (msg->node, "from");
  if (from == NULL)
    {
      NODE_DEBUG (msg->node, "No sender, skipping");
      return activities;
    }
  from_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (from_handle == 0)
    {
      DEBUG ("unknown sender");
      return activities;
    }

  DEBUG ("Incorporating public activities into GetActivities() return...");
  activities_list = tp_handle_set_new (room_repo);
  for (node = (activities_node != NULL ? activities_node->children : NULL);
       node;
       node = node->next)
    {
      const gchar *act_id;
      const gchar *room;
      GValue gvalue = {0,};
      ActivityInfo *info;
      TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
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

      info = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (room_handle));

      if (info == NULL)
        {
          info = add_activity_info (conn, room_handle);
          g_assert (!tp_handle_set_is_member (activities_list, room_handle));
        }
      else
        {
          if (tp_handle_set_is_member (activities_list, room_handle))
            {
              NODE_DEBUG (node, "Room advertised twice, skipping");
              tp_handle_unref (room_repo, room_handle);
              continue;
            }
          info->refcount++;

          DEBUG ("ref: %s (%d) refcount: %d\n",
              activity_info_get_room (info),
              info->handle, info->refcount);
        }
      /* pass ownership to the activities_list */
      tp_handle_set_add (activities_list, room_handle);
      tp_handle_unref (room_repo, room_handle);

      if (tp_strdiff (info->id, act_id))
        {
          DEBUG ("Assigning new ID <%s> to room #%u <%s>", act_id, room_handle,
              room);
          g_free (info->id);
          info->id = g_strdup (act_id);
        }

      g_value_init (&gvalue, ACTIVITY_PAIR_TYPE);
      g_value_take_boxed (&gvalue,
          dbus_g_type_specialized_construct (ACTIVITY_PAIR_TYPE));

      dbus_g_type_struct_set (&gvalue,
          0, act_id,
          1, info->handle,
          G_MAXUINT);
      DEBUG ("... public activity #%u (ID %s)", info->handle, act_id);
      g_ptr_array_add (activities, g_value_get_boxed (&gvalue));
    }

  old_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (from_handle));

  if (old_activities != NULL)
    {
      /* We decrement the refcount (and free if needed) all the
       * activities previously announced by this contact. */
      tp_handle_set_foreach (old_activities,
          decrement_contacts_activities_list_foreach, conn);
    }

  /* Update the list of activities associated with this contact. */
  g_hash_table_insert (conn->olpc_pep_activities,
      GUINT_TO_POINTER (from_handle), activities_list);

  DEBUG ("Incorporating private activities into GetActivities() return...");
  invited_activities = g_hash_table_lookup (conn->olpc_invited_activities,
      GUINT_TO_POINTER (from_handle));

  if (invited_activities != NULL)
    {
      g_assert (tp_handle_set_peek (invited_activities) != NULL);
      tp_intset_iter_init (&iter, tp_handle_set_peek (invited_activities));
      while (tp_intset_iter_next (&iter))
        {
          if (tp_handle_set_is_member (activities_list, iter.element))
            {
              DEBUG ("... invited activity #%u was public, skipping",
                  iter.element);
            }
          else
            {
              ActivityInfo *invited = g_hash_table_lookup (
                  conn->olpc_activities_info, GUINT_TO_POINTER (iter.element));
              GValue gvalue = { 0 };

              g_assert (invited != NULL);
              if (invited->id == NULL)
                {
                  DEBUG ("... private activity #%u has no ID, skipping",
                      iter.element);
                  continue;
                }

              g_value_init (&gvalue, ACTIVITY_PAIR_TYPE);
              g_value_take_boxed (&gvalue, dbus_g_type_specialized_construct
                  (ACTIVITY_PAIR_TYPE));
              dbus_g_type_struct_set (&gvalue,
                  0, invited->id,
                  1, invited->handle,
                  G_MAXUINT);
              DEBUG ("... private activity #%u (ID %s)",
                  invited->handle, invited->id);
              g_ptr_array_add (activities, g_value_get_boxed (&gvalue));
            }
        }
    }
  DEBUG ("... done");

  return activities;
}

static void
free_activities (GPtrArray *activities)
{
  guint i;

  for (i = 0; i < activities->len; i++)
    g_boxed_free (ACTIVITY_PAIR_TYPE, activities->pdata[i]);

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
      gchar *activity;
      guint channel;
      ActivityInfo *info;

      g_value_init (&pair, ACTIVITY_PAIR_TYPE);
      g_value_set_static_boxed (&pair, g_ptr_array_index (activities, i));
      dbus_g_type_struct_get (&pair,
          0, &activity,
          1, &channel,
          G_MAXUINT);

      info = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (channel));
      if (info == NULL || info->properties == NULL)
        {
          query_needed = TRUE;
        }

      g_free (activity);
    }

  if (query_needed)
    {
      pubsub_query (conn, from, NS_OLPC_ACTIVITY_PROPS,
          get_activity_properties_reply_cb, NULL);
    }
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

  if (!check_query_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  activities = extract_activities (conn, reply_msg);

  from = lm_message_node_get_attribute (reply_msg->node, "from");
  /* FIXME: race? */
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

  gabble_svc_olpc_buddy_info_return_from_set_activities (context);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_buddy_info_set_activities (GabbleSvcOLPCBuddyInfo *iface,
                                const GPtrArray *activities,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection*) conn;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_ROOM);
  LmMessage *msg;
  LmMessageNode *publish;
  guint i;
  TpHandleSet *activities_list, *old_activities;

  DEBUG ("called");

  if (!check_pep (conn, context))
    return;

  activities_list = tp_handle_set_new (room_repo);
  msg = pubsub_make_publish_msg (NULL, NS_OLPC_ACTIVITIES, NS_OLPC_ACTIVITIES,
      "activities", &publish);

  for (i = 0; i < activities->len; i++)
    {
      GValue pair = {0,};
      LmMessageNode *activity_node;
      gchar *activity;
      guint channel;
      const gchar *room = NULL;
      ActivityInfo *info;
      GError *error = NULL;

      g_value_init (&pair, ACTIVITY_PAIR_TYPE);
      g_value_set_static_boxed (&pair, g_ptr_array_index (activities, i));
      dbus_g_type_struct_get (&pair,
          0, &activity,
          1, &channel,
          G_MAXUINT);

      if (!tp_handle_is_valid (room_repo, channel, &error))
        {
          DEBUG ("Invalid room handle");
          dbus_g_method_return_error (context, error);

          /* We have to unref information previously
           * refed in this loop */
          tp_handle_set_foreach (activities_list,
              decrement_contacts_activities_list_foreach, conn);

          /* set_activities failed so we don't unref old activities
           * of the local user */

          tp_handle_set_destroy (activities_list);
          g_error_free (error);
          g_free (activity);
          return;
        }

      room = tp_handle_inspect (room_repo, channel);

      info = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (channel));

      if (info == NULL)
        {
          info = add_activity_info (conn, channel);
        }
      else
        {
          if (tp_handle_set_is_member (activities_list, channel))
            {
              GError *error = g_error_new (TP_ERRORS,
                  TP_ERROR_INVALID_ARGUMENT,
                  "Can't set twice the same activity: %s", room);

              DEBUG ("activity already added: %s", room);
              dbus_g_method_return_error (context, error);

              /* We have to unref information previously
               * refed in this loop */
              tp_handle_set_foreach (activities_list,
                  decrement_contacts_activities_list_foreach, conn);

              /* set_activities failed so we don't unref old activities
               * of the local user */

              tp_handle_set_destroy (activities_list);
              g_error_free (error);
              g_free (activity);
              return;
            }

          info->refcount++;

          DEBUG ("ref: %s (%d) refcount: %d\n",
              activity_info_get_room (info),
              info->handle, info->refcount);
        }
      g_free (info->id);
      info->id = activity;

      tp_handle_set_add (activities_list, channel);

      activity_node = lm_message_node_add_child (publish, "activity", "");
      lm_message_node_set_attributes (activity_node,
            "type", activity,
            "room", room,
            NULL);
    }

  old_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle));

  if (old_activities != NULL)
    {
      /* We decrement the refcount (and free if needed) all the
       * activities previously announced by our own contact. */
      tp_handle_set_foreach (old_activities,
          decrement_contacts_activities_list_foreach, conn);
    }

  /* Update the list of activities associated with our own contact. */
  g_hash_table_insert (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle), activities_list);

  if (!_gabble_connection_send_with_reply (conn, msg,
        set_activities_reply_cb, NULL, context, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property request to server" };

      dbus_g_method_return_error (context, &error);
    }

  lm_message_unref (msg);
}

gboolean
olpc_buddy_info_activities_event_handler (GabbleConnection *conn,
                                          LmMessage *msg,
                                          TpHandle handle)
{
  GPtrArray *activities;

  activities = extract_activities (conn, msg);
  gabble_svc_olpc_buddy_info_emit_activities_changed (conn, handle,
      activities);
  free_activities (activities);
  return TRUE;
}

static ActivityInfo*
add_activity_info_in_list (GabbleConnection *conn,
                           TpHandle room_handle,
                           const gchar *from,
                           GHashTable *table)
{
  ActivityInfo *info;
  TpHandle from_handle;
  TpHandleSet *activities_list;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_ROOM);

  from_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);

  if (from_handle == 0)
    {
      DEBUG ("unknown sender");
      return NULL;
    }

  info = add_activity_info (conn, room_handle);

  /* Add activity information in the list of the contact */
  activities_list = g_hash_table_lookup (table, GUINT_TO_POINTER (
        from_handle));
  if (activities_list == NULL)
    {
      activities_list = tp_handle_set_new (room_repo);
      tp_handle_set_add (activities_list, room_handle);
      g_hash_table_insert (table, GUINT_TO_POINTER (from_handle),
          activities_list);
    }

  return info;
}

static gboolean
extract_current_activity (GabbleConnection *conn,
                          LmMessage *msg,
                          const gchar **activity,
                          guint *handle)
{
  LmMessageNode *node;
  const gchar *room;
  ActivityInfo *info;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  node = lm_message_node_find_child (msg->node, "activity");

  if (node == NULL)
    return FALSE;

  *activity = lm_message_node_get_attribute (node, "type");

  room = lm_message_node_get_attribute (node, "room");
  if (room == NULL)
    return FALSE;

  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    return FALSE;

  info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));

  if (info == NULL)
    {
      /* Humm we received as current activity an activity we don't know yet.
       * If the remote user doesn't announce this activity
       * in his next activities list, information about
       * it will be freed */
      const gchar *from;

      DEBUG ("unknown current activity %s", room);

      from = lm_message_node_get_attribute (msg->node, "from");

      info = add_activity_info_in_list (conn, room_handle, from,
          conn->olpc_pep_activities);
    }

  tp_handle_unref (room_repo, room_handle);

  if (info == NULL)
    return FALSE;

  *handle = info->handle;

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

  if (!check_query_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (!extract_current_activity (conn, reply_msg, &activity, &room_handle))
    {
      if (extract_current_activity (conn, reply_msg, &activity, &room_handle))
        {
          gabble_svc_olpc_buddy_info_return_from_get_current_activity (context,
              activity, room_handle);
        }
    }

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

  DEBUG ("called");

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
activity_in_own_list (GabbleConnection *conn,
                      const gchar *room)
{
  TpBaseConnection *base = (TpBaseConnection*) conn;
  TpHandleSet *activities_list;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    /* If activity's information was in the list, we would
     * have found the handle as ActivityInfo keep a ref on it */
    return FALSE;

  activities_list = g_hash_table_lookup (conn->olpc_pep_activities,
      GINT_TO_POINTER (base->self_handle));

  if (activities_list == NULL ||
      !tp_handle_set_is_member (activities_list, room_handle))
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

  if (!check_pep (conn, context))
    return;

  /* if activity == "" there is no current activity */
  if (activity[0] != '\0')
    {
      room = inspect_room (base, context, channel);
      if (room == NULL)
        return;

      if (!activity_in_own_list (conn, room))
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

  if (extract_current_activity (conn, msg, &activity, &room_handle))
    {
      gabble_svc_olpc_buddy_info_emit_current_activity_changed (conn, handle,
          activity, room_handle);
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

static void
set_activity_properties (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  LmMessageNode *node = user_data;
  ActivityInfo *info = (ActivityInfo*) value;

  activity_info_contribute_properties (info, node);
}

static LmHandlerResult
set_activity_properties_reply_cb (GabbleConnection *conn,
                                  LmMessage *sent_msg,
                                  LmMessage *reply_msg,
                                  GObject *object,
                                  gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;

  if (!check_publish_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  gabble_svc_olpc_activity_properties_return_from_set_properties (context);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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
  LmMessageNode *publish;
  const gchar *jid;
  GHashTable *properties_copied;
  ActivityInfo *info;

  DEBUG ("called");

  if (!check_pep (conn, context))
    return;

  jid = inspect_room (base, context, room);
  if (jid == NULL)
    return;

  if (!activity_in_own_list (conn, jid))
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Can't set properties on an activity if you're not announcing it" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  properties_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  gabble_g_hash_table_update (properties_copied, properties,
      (GBoxedCopyFunc) g_strdup, (GBoxedCopyFunc) tp_g_value_slice_dup);

  info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room));
  activity_info_set_properties (info, properties_copied);

  msg = pubsub_make_publish_msg (NULL,
      NS_OLPC_ACTIVITY_PROPS,
      NS_OLPC_ACTIVITY_PROPS,
      "activities",
      &publish);

  g_hash_table_foreach (conn->olpc_activities_info, set_activity_properties,
      publish);

  if (!_gabble_connection_send_with_reply (conn, msg,
        set_activity_properties_reply_cb, NULL, context, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property change request to server" };

      dbus_g_method_return_error (context, &error);
    }

  lm_message_unref (msg);
}

static void
olpc_activity_properties_get_properties (GabbleSvcOLPCActivityProperties *iface,
                                         guint room,
                                         DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  gboolean not_prop = FALSE;
  GHashTable *properties;
  ActivityInfo *info;

  DEBUG ("called");

  if (!check_pep (conn, context))
    return;

  info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room));

  if (info == NULL || info->properties == NULL)
    {
      /* no properties */
      properties = g_hash_table_new (g_str_hash, g_str_equal);
      not_prop = TRUE;
    }
  else
    {
      properties = info->properties;
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
      /* XXX check other types of property (we don't actually have any yet) */
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

static gboolean
update_activities_properties (GabbleConnection *conn,
                              LmMessage *msg)
{
  const gchar *room;
  LmMessageNode *node, *properties_node;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_ROOM);

  node = lm_message_node_find_child (msg->node, "activities");
  if (node == NULL)
    return FALSE;

  for (properties_node = node->children; properties_node != NULL;
      properties_node = properties_node->next)
    {
      GHashTable *new_properties, *old_properties;
      gboolean new_infos = FALSE;
      ActivityInfo *info;
      TpHandle room_handle;

      if (strcmp (properties_node->name, "properties") != 0)
        continue;

      room = lm_message_node_get_attribute (properties_node, "room");
      if (room == NULL)
        continue;

      room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);

      info = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (room_handle));

      if (info == NULL)
        {
          /* Humm we received properties for an activity we don't
           * know yet.
           * If the remote user doesn't announce this activity
           * in his next activities list, information about
           * it will be freed */
          const gchar *from;

          DEBUG ("unknown activity: %s", room);

          from = lm_message_node_get_attribute (msg->node, "from");

          info = add_activity_info_in_list (conn, room_handle, from,
              conn->olpc_pep_activities);
        }

      tp_handle_unref (room_repo, room_handle);

      if (info == NULL)
        continue;

      old_properties = info->properties;

      new_properties = lm_message_node_extract_properties (properties_node, "property");

      if (g_hash_table_size (new_properties) == 0)
        {
          g_hash_table_destroy (new_properties);
          continue;
        }

      if (old_properties == NULL ||
          properties_contains_new_infos (old_properties,
            new_properties))
        {
          new_infos = TRUE;
        }

      activity_info_set_properties (info, new_properties);

      if (new_infos)
        {
          /* Only emit the signal if we add new values */

          gabble_svc_olpc_activity_properties_emit_activity_properties_changed (
              conn, info->handle, new_properties);
        }
    }

  return TRUE;
}

gboolean
olpc_activities_properties_event_handler (GabbleConnection *conn,
                                          LmMessage *msg,
                                          TpHandle handle)
{
  return update_activities_properties (conn, msg);
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
      LmMessage *msg;
      LmMessageNode *publish;

      msg = pubsub_make_publish_msg (NULL,
          NS_OLPC_ACTIVITY_PROPS,
          NS_OLPC_ACTIVITY_PROPS,
          "activities",
          &publish);

      g_hash_table_foreach (conn->olpc_activities_info,
          set_activity_properties, publish);

      _gabble_connection_send_with_reply (conn, msg,
        NULL, NULL, NULL, NULL);

      lm_message_unref (msg);
    }
}

gboolean
conn_olpc_process_activity_properties_message (GabbleConnection *conn,
                                               LmMessage *msg,
                                               const gchar *from)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  LmMessageNode *node = lm_message_node_get_child_with_namespace (msg->node,
      "properties", NS_OLPC_ACTIVITY_PROPS);
  const gchar *room, *id;
  TpHandle room_handle, contact_handle;
  ActivityInfo *info;
  TpHandleSet *their_invites;
  GHashTable *new_properties;

  /* if no <properties xmlns=...>, then not for us */
  if (node == NULL)
    return FALSE;

  DEBUG ("Found <properties> node in <message>");

  /* FIXME: This is stupid. We should ref the handles in a TpHandleSet
   * per activity, then we could _ensure this handle */
  contact_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (contact_handle == 0)
    {
      DEBUG ("... contact <%s> unknown - ignoring (FIX THIS)", from);
      return TRUE;
    }

  id = lm_message_node_get_attribute (node, "activity");
  if (id == NULL)
    {
      NODE_DEBUG (node, "... activity ID missing - ignoring");
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

  their_invites = g_hash_table_lookup (conn->olpc_invited_activities,
      GUINT_TO_POINTER (contact_handle));
  if (their_invites == NULL)
    {
      their_invites = tp_handle_set_new (room_repo);
      g_hash_table_insert (conn->olpc_invited_activities,
          GUINT_TO_POINTER (contact_handle), their_invites);
    }

  info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));
  if (info == NULL)
    {
      DEBUG ("... creating new ActivityInfo");
      info = add_activity_info (conn, room_handle);
      tp_handle_set_add (their_invites, room_handle);
    }
  else if (!tp_handle_set_is_member (their_invites, room_handle))
    {
      DEBUG ("... it's the first time that contact invited me, referencing "
          "ActivityInfo");
      info->refcount++;
      tp_handle_set_add (their_invites, room_handle);
    }
  tp_handle_unref (room_repo, room_handle);

  /* apply the info we found */
  if (tp_strdiff (info->id, id))
    {
      DEBUG ("... recording new activity ID %s", id);
      g_free (info->id);
      info->id = g_strdup (id);
    }

  new_properties = lm_message_node_extract_properties (node,
      "property");
  activity_info_set_properties (info, new_properties);

  /* FIXME: if necessary, emit signals and stuff */

  return TRUE;
}

static void
muc_channel_closed_cb (GabbleMucChannel *chan,
                       ActivityInfo *info)
{
  GabbleConnection *conn = info->conn;
  TpBaseConnection *base = (TpBaseConnection *) info->conn;
  TpHandleSet *my_activities;

  /* remove it from our advertised activities list, unreffing it in the
   * process */
  my_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle));
  g_hash_table_steal (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle));
  if (tp_handle_set_remove (my_activities, info->handle))
    {
      activity_info_unref (info);
    }
  g_hash_table_insert (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle), my_activities);

  /* unref it again (it was referenced on behalf of the channel) */
  activity_info_unref (info);

  /* FIXME: update our activities PEP */
  /* FIXME: update our activity properties PEP */
}

static void
muc_channel_pre_invite_cb (GabbleMucChannel *chan,
                           TpHandle invitee,
                           ActivityInfo *info)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) info->conn, TP_HANDLE_TYPE_CONTACT);
  /* send them the properties */
  LmMessage *msg;

  msg = lm_message_new (tp_handle_inspect (contact_repo, invitee),
      LM_MESSAGE_TYPE_MESSAGE);

  if (activity_info_contribute_properties (info, msg->node))
    {
      /* not much we can do about errors - but if this fails, the invitation
       * will too, unless something extremely strange is going on */
      if (!_gabble_connection_send (info->conn, msg, NULL))
        {
          DEBUG ("Unable to send activity properties to invitee");
        }
    }
  lm_message_unref (msg);
}

static void
muc_channel_conctact_join_cb (GabbleMucChannel *chan,
                              TpHandle contact,
                              gpointer unused)
{
  GQuark quark = invitees_quark ();
  TpHandleSet *invitees;

  invitees = g_object_get_qdata ((GObject *) chan, quark);
  if (invitees != NULL)
    {
      tp_handle_set_remove (invitees, contact);
    }
}

static void
muc_factory_new_channel_cb (GabbleMucFactory *fac,
                            TpChannelIface *chan,
                            gpointer opaque_request,
                            GabbleConnection *conn)
{
  ActivityInfo *info;
  TpHandle room_handle;

  if (!GABBLE_IS_MUC_CHANNEL (chan))
    return;

  g_object_get (chan,
      "handle", &room_handle,
      NULL);

  /* ref the activity info for as long as we have a channel open */

  info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));
  if (info == NULL)
    {
      info = add_activity_info (conn, room_handle);
    }
  else
    {
      info->refcount++;
    }

  g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb),
      info);
  g_signal_connect (chan, "pre-invite", G_CALLBACK (muc_channel_pre_invite_cb),
      info);
  g_signal_connect (chan, "contact-join",
      G_CALLBACK (muc_channel_conctact_join_cb), NULL);
}

static void
connection_presence_update_cb (GabblePresenceCache *cache,
                               TpHandle handle,
                               GabbleConnection *conn)
{
  GabblePresence *presence;

  presence = gabble_presence_cache_get (cache, handle);

  if (presence && presence->status == GABBLE_PRESENCE_OFFLINE)
    {
      /* Contact goes offline. We have to unref all the information
       * provided by him
       */
      TpHandleSet *list;

      list = g_hash_table_lookup (conn->olpc_pep_activities,
          GUINT_TO_POINTER (handle));

      tp_handle_set_foreach (list,
          decrement_contacts_activities_list_foreach, conn);

      g_hash_table_remove (conn->olpc_pep_activities,
          GUINT_TO_POINTER (handle));

      list = g_hash_table_lookup (conn->olpc_invited_activities,
          GUINT_TO_POINTER (handle));

      tp_handle_set_foreach (list,
          decrement_contacts_activities_list_foreach, conn);

      g_hash_table_remove (conn->olpc_invited_activities,
          GUINT_TO_POINTER (handle));
    }
}

void
conn_olpc_activity_properties_init (GabbleConnection *conn)
{
  /* room TpHandle => borrowed ActivityInfo */
  conn->olpc_activities_info = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) activity_info_free);

  /* Activity info from PEP
   *
   * contact TpHandle => TpHandleSet of room handles,
   *    each representing a reference to an ActivityInfo
   *
   * Special case: the entry for self_handle is the complete list of
   * activities, not just those from PEP
   */
  conn->olpc_pep_activities = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) tp_handle_set_destroy);

  /* Activity info from pseudo-invitations
   *
   * contact TpHandle => TpHandleSet of room handles,
   *    each representing a reference to an ActivityInfo
   *
   * Special case: there is never an entry for self_handle
   */
  conn->olpc_invited_activities = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) tp_handle_set_destroy);

  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), NULL);

  g_signal_connect (conn->muc_factory, "new-channel",
      G_CALLBACK (muc_factory_new_channel_cb), conn);

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
