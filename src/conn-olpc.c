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

#include "conn-olpc.h"

#include <string.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include "debug.h"
#include "gabble-connection.h"
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
decrement_contacts_activities_list_foreach (ActivityInfo *info,
                                            GabbleConnection *conn)
{
  info->refcount--;

  DEBUG ("unref: %s (%d) refcount: %d\n",
      activity_info_get_room (info), info->handle,
      info->refcount);

  if (info->refcount == 0)
    {
      g_hash_table_remove (conn->olpc_activities_info,
          GUINT_TO_POINTER (info->handle));
    }
}

static gboolean
check_pep (GabbleConnection *conn,
           DBusGMethodInvocation *context)
{
  if (!(conn->features & GABBLE_CONNECTION_FEATURES_PEP))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Server does not support PEP" };

      DEBUG ("%s", error.message);
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

  gabble_svc_olpc_buddy_info_return_from_set_properties (context);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
olpc_buddy_info_set_properties (GabbleSvcOLPCBuddyInfo *iface,
                                GHashTable *properties,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *msg;
  LmMessageNode *publish;

  DEBUG ("called");

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

      dbus_g_method_return_error (context, &error);
    }

  lm_message_unref (msg);
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
  GSList *activities_list = NULL, *old_activities;
  const gchar *from;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle from_handle;

  activities_node = lm_message_node_find_child (msg->node, "activities");
  activities = g_ptr_array_new ();

  if (activities_node == NULL)
    return activities;

  from = lm_message_node_get_attribute (msg->node, "from");

  from_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);

  if (from_handle == 0)
    {
      DEBUG ("unknown sender");
      return activities;
    }

  for (node = activities_node->children; node; node = node->next)
    {
      const gchar *type;
      const gchar *room;
      GValue gvalue = {0,};
      ActivityInfo *info;
      TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
      TpHandle room_handle;


      if (0 != strcmp (node->name, "activity"))
        continue;

      type = lm_message_node_get_attribute (node, "type");

      if (!type)
        continue;

      room = lm_message_node_get_attribute (node, "room");

      if (!room)
        continue;

      room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);
      if (room_handle == 0)
        continue;

      info = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (room_handle));

      if (info == NULL)
        {
          info = add_activity_info (conn, room_handle);
        }
      else
        {
          if (g_slist_find (activities_list, info) != NULL)
            {
              /* Avoid to add an activity if the contact has
               * the stupid idea to announce it more than once */
              DEBUG ("activity already added: %s", room);
              tp_handle_unref (room_repo, room_handle);
              continue;
            }

          info->refcount++;

          DEBUG ("ref: %s (%d) refcount: %d\n",
              activity_info_get_room (info),
              info->handle, info->refcount);
        }

      /* Let's add this activity in user's activities list */
      activities_list = g_slist_prepend (activities_list, info);

      g_value_init (&gvalue, ACTIVITY_PAIR_TYPE);
      g_value_take_boxed (&gvalue,
          dbus_g_type_specialized_construct (ACTIVITY_PAIR_TYPE));

      dbus_g_type_struct_set (&gvalue,
          0, type,
          1, info->handle,
          G_MAXUINT);
      g_ptr_array_add (activities, g_value_get_boxed (&gvalue));

      tp_handle_unref (room_repo, room_handle);
    }

  old_activities = g_hash_table_lookup (conn->olpc_contacts_activities,
      GUINT_TO_POINTER (from_handle));

  if (old_activities != NULL)
    {
      /* We decrement the refcount (and free if needed) all the
       * activities previously announced by this contact. */
      g_slist_foreach (old_activities,
          (GFunc) decrement_contacts_activities_list_foreach, conn);
    }

  /* Update the list of activities associated with this contact. */
  g_hash_table_insert (conn->olpc_contacts_activities,
      GUINT_TO_POINTER (from_handle), activities_list);

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
  GSList *activities_list = NULL, *old_activities;

  DEBUG ("called");

  if (!check_pep (conn, context))
    return;

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
          g_slist_foreach (activities_list,
              (GFunc) decrement_contacts_activities_list_foreach, conn);

          /* set_activities failed so we don't unref old activities
           * of the local user */

          g_slist_free (activities_list);
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
          if (g_slist_find (activities_list, info) != NULL)
            {
              GError *error = g_error_new (TP_ERRORS,
                  TP_ERROR_INVALID_ARGUMENT,
                  "Can't set twice the same activity: %s", room);

              DEBUG ("activity already added: %s", room);
              dbus_g_method_return_error (context, error);

              /* We have to unref information previously
               * refed in this loop */
              g_slist_foreach (activities_list,
                  (GFunc) decrement_contacts_activities_list_foreach, conn);

              /* set_activities failed so we don't unref old activities
               * of the local user */

              g_slist_free (activities_list);
              g_error_free (error);
              g_free (activity);
              return;
            }

          info->refcount++;

          DEBUG ("ref: %s (%d) refcount: %d\n",
              activity_info_get_room (info),
              info->handle, info->refcount);
        }

      activities_list = g_slist_prepend (activities_list, info);

      activity_node = lm_message_node_add_child (publish, "activity", "");
      lm_message_node_set_attributes (activity_node,
            "type", activity,
            "room", room,
            NULL);

      g_free (activity);
    }

  old_activities = g_hash_table_lookup (conn->olpc_contacts_activities,
      GUINT_TO_POINTER (base->self_handle));

  if (old_activities != NULL)
    {
      /* We decrement the refcount (and free if needed) all the
       * activities previously announced by our own contact. */
      g_slist_foreach (old_activities,
          (GFunc) decrement_contacts_activities_list_foreach, conn);
    }

  /* Update the list of activities associated with our own contact. */
  g_hash_table_insert (conn->olpc_contacts_activities,
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
                           const gchar *from)
{
  ActivityInfo *info;
  TpHandle from_handle;
  GSList *activities_list;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection*) conn, TP_HANDLE_TYPE_CONTACT);

  from_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);

  if (from_handle == 0)
    {
      DEBUG ("unknown sender");
      return NULL;
    }

  info = add_activity_info (conn, room_handle);

  /* Add activity information in the list of the contact */
  activities_list = g_hash_table_lookup (conn->olpc_contacts_activities,
      GINT_TO_POINTER (from_handle));
  activities_list = g_slist_prepend (activities_list, info);

  g_hash_table_steal (conn->olpc_contacts_activities,
      GINT_TO_POINTER (from_handle));
  g_hash_table_insert (conn->olpc_contacts_activities,
      GINT_TO_POINTER (from_handle), activities_list);

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

      DEBUG ("unknow current activity %s", room);

      from = lm_message_node_get_attribute (msg->node, "from");

      info = add_activity_info_in_list (conn, room_handle, from);
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
  ActivityInfo *info;
  GSList *activities_list;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    /* If activity's information was in the list, we would
     * have found the handle as ActivityInfo keep a ref on it */
    return FALSE;

  info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room_handle));

  if (info == NULL)
    return FALSE;

  activities_list = g_hash_table_lookup (conn->olpc_contacts_activities,
      GINT_TO_POINTER (base->self_handle));

  if (activities_list == NULL || g_slist_find (activities_list, info) == NULL)
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
  if (strlen (activity) > 0)
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
  LmMessageNode *properties_node;
  ActivityInfo *info = (ActivityInfo*) value;

  if (info->properties != NULL)
    {
      const gchar *room = activity_info_get_room (info);
      properties_node = lm_message_node_add_child (node, "properties", "");

      lm_message_node_set_attribute (properties_node, "room", room);
      lm_message_node_add_children_from_properties (properties_node,
          info->properties, "property");
    }
}

static void
copy_properties (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
  const gchar *prop = key;
  GValue *gvalue = value;
  GHashTable *properties_copied = user_data;
  GValue *gvalue_copied;

  gvalue_copied = g_slice_new0 (GValue);
  g_value_init (gvalue_copied, G_VALUE_TYPE (gvalue));
  g_value_copy (gvalue, gvalue_copied);

  g_hash_table_insert (properties_copied, g_strdup (prop), gvalue_copied);
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
  g_hash_table_foreach (properties, copy_properties,
      properties_copied);

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
      /* XXX check other properties type */
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

          info = add_activity_info_in_list (conn, room_handle, from);
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

      _gabble_connection_send (conn, msg, NULL);

      lm_message_unref (msg);
    }
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
      GSList *list;

      list = g_hash_table_lookup (conn->olpc_contacts_activities,
          GINT_TO_POINTER (handle));

      g_slist_foreach (list,
          (GFunc) decrement_contacts_activities_list_foreach, conn);

      g_hash_table_remove (conn->olpc_contacts_activities,
          GUINT_TO_POINTER (handle));
    }
}

void
conn_olpc_activity_properties_init (GabbleConnection *conn)
{
  /* associate the room handle of an activity with its
   * information */
  conn->olpc_activities_info = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) activity_info_free);

  /* associate the handle of a contact with a list
   * of activity information published by him */
  conn->olpc_contacts_activities = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_slist_free);

  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), NULL);

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
