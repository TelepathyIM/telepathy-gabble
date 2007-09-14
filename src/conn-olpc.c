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
activity_info_contribute_properties (ActivityInfo *info,
                                     LmMessageNode *parent,
                                     gboolean only_public)
{
  LmMessageNode *props_node;

  if (info->id == NULL || info->properties == NULL)
    return FALSE;

  if (only_public && !activity_info_is_visible (info))
    return FALSE;

  props_node = lm_message_node_add_child (parent,
      "properties", "");
  lm_message_node_set_attributes (props_node,
      "xmlns", NS_OLPC_ACTIVITY_PROPS,
      "room", activity_info_get_room (info),
      "activity", info->id,
      NULL);
  lm_message_node_add_children_from_properties (props_node, info->properties,
      "property");
  return TRUE;
}

static void
decrement_contacts_activities_set_foreach (TpHandleSet *set,
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

static GQuark
invitees_quark ()
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
          ActivityInfo *info = g_hash_table_lookup (
              conn->olpc_activities_info, GUINT_TO_POINTER (iter.element));
          GValue gvalue = { 0 };

          g_assert (info != NULL);
          if (info->id == NULL)
            {
              DEBUG ("... activity #%u has no ID, skipping", iter.element);
              continue;
            }

          g_value_init (&gvalue, ACTIVITY_PAIR_TYPE);
          g_value_take_boxed (&gvalue, dbus_g_type_specialized_construct
              (ACTIVITY_PAIR_TYPE));
          dbus_g_type_struct_set (&gvalue,
              0, info->id,
              1, info->handle,
              G_MAXUINT);
          DEBUG ("... activity #%u (ID %s)",
              info->handle, info->id);
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
          info->refcount++;

          DEBUG ("ref: %s (%d) refcount: %d\n",
              activity_info_get_room (info),
              info->handle, info->refcount);
        }
      /* pass ownership to the activities_set */
      tp_handle_set_add (activities_set, room_handle);
      tp_handle_unref (room_repo, room_handle);

      if (tp_strdiff (info->id, act_id))
        {
          DEBUG ("Assigning new ID <%s> to room #%u <%s>", act_id, room_handle,
              room);
          g_free (info->id);
          info->id = g_strdup (act_id);
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
  TpHandle from_handle;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);

  if (!check_query_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

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
          ActivityInfo *info = g_hash_table_lookup (conn->olpc_activities_info,
              GUINT_TO_POINTER (iter.element));
          LmMessageNode *activity_node;

          g_assert (info != NULL);
          if (!activity_info_is_visible (info))
            continue;

          activity_node = lm_message_node_add_child (publish,
              "activity", "");
          lm_message_node_set_attributes (activity_node,
              "type", info->id,
              "room", activity_info_get_room (info),
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
  TpBaseConnection *base = (TpBaseConnection*) conn;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_ROOM);
  guint i;
  TpHandleSet *activities_set, *old_activities;

  DEBUG ("called");

  if (!check_pep (conn, context))
    return;

  activities_set = tp_handle_set_new (room_repo);

  for (i = 0; i < activities->len; i++)
    {
      GValue pair = {0,};
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
          tp_handle_set_foreach (activities_set,
              decrement_contacts_activities_set_foreach, conn);

          /* set_activities failed so we don't unref old activities
           * of the local user */

          tp_handle_set_destroy (activities_set);
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
          if (tp_handle_set_is_member (activities_set, channel))
            {
              GError *error = g_error_new (TP_ERRORS,
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

          info->refcount++;

          DEBUG ("ref: %s (%d) refcount: %d\n",
              activity_info_get_room (info),
              info->handle, info->refcount);
        }
      g_free (info->id);
      info->id = activity;

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
  ActivityInfo *info;
  TpHandle from_handle;
  TpHandleSet *activities_set;
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
  activities_set = g_hash_table_lookup (table, GUINT_TO_POINTER (
        from_handle));
  if (activities_set == NULL)
    {
      activities_set = tp_handle_set_new (room_repo);
      tp_handle_set_add (activities_set, room_handle);
      g_hash_table_insert (table, GUINT_TO_POINTER (from_handle),
          activities_set);
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
  if (room == NULL || room[0] == '\0')
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

      info = add_activity_info_in_set (conn, room_handle, from,
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
  TpBaseConnection *base = (TpBaseConnection*) conn;
  TpHandleSet *activities_set;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    /* If activity's information was in the list, we would
     * have found the handle as ActivityInfo keep a ref on it */
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

  if (extract_current_activity (conn, msg, &activity, &room_handle))
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
          ActivityInfo *info = g_hash_table_lookup (conn->olpc_activities_info,
              GUINT_TO_POINTER (iter.element));

          activity_info_contribute_properties (info, publish, TRUE);
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
set_activity_properties_activities_reply_cb (GabbleConnection *conn,
                                             LmMessage *sent_msg,
                                             LmMessage *reply_msg,
                                             GObject *object,
                                             gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;

  /* if the SetProperties() call was skipped, both messages are NULL */
  g_assert ((sent_msg == NULL) == (reply_msg == NULL));

  if (reply_msg != NULL && !check_publish_reply_msg (reply_msg, context))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  /* FIXME: emit ActivityPropertiesChanged? */

  gabble_svc_olpc_activity_properties_return_from_set_properties (context);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

typedef struct {
    DBusGMethodInvocation *context;
    gboolean visibility_changed;
} set_properties_ctx;

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

  if (reply_msg == NULL ||
      check_publish_reply_msg (reply_msg, context->context))
    {
      /* FIXME: set the activities list if needed */
      if (context->visibility_changed)
        {
          GError *err = NULL;

          if (!upload_activities_pep (conn,
                set_activity_properties_activities_reply_cb,
                context->context, &err))
            {
              dbus_g_method_return_error (context->context, err);
              g_error_free (err);
            }
        }
      else
        {
          /* nothing to do, so just "succeed" */
          set_activity_properties_activities_reply_cb (conn, NULL, NULL, NULL,
              context->context);
        }
    }

  g_slice_free (set_properties_ctx, context);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
refresh_invitations (GabbleMucChannel *chan,
                     ActivityInfo *info,
                     GError **error)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) info->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleSet *invitees = g_object_get_qdata ((GObject *) chan,
      invitees_quark ());

  if (invitees != NULL && tp_handle_set_size (invitees) > 0)
    {
      LmMessage *msg = lm_message_new (NULL, LM_MESSAGE_TYPE_MESSAGE);
      TpIntSetIter iter = TP_INTSET_ITER_INIT (tp_handle_set_peek
          (invitees));

      activity_info_contribute_properties (info, msg->node, FALSE);

      while (tp_intset_iter_next (&iter))
        {
          const gchar *to = tp_handle_inspect (contact_repo, iter.element);

          lm_message_node_set_attribute (msg->node, "to", to);
          if (!_gabble_connection_send (info->conn, msg, error))
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
  ActivityInfo *info;
  GabbleMucChannel *muc_channel;
  guint state;
  gboolean was_visible, is_visible;
  set_properties_ctx *ctx;
  GError *err = NULL;

  DEBUG ("called");

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
  gabble_g_hash_table_update (properties_copied, properties,
      (GBoxedCopyFunc) g_strdup, (GBoxedCopyFunc) tp_g_value_slice_dup);

  info = g_hash_table_lookup (conn->olpc_activities_info,
      GUINT_TO_POINTER (room));

  was_visible = activity_info_is_visible (info);

  activity_info_set_properties (info, properties_copied);

  is_visible = activity_info_is_visible (info);

  msg = lm_message_new (jid, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_node_set_attribute (msg->node, "type", "groupchat");
  activity_info_contribute_properties (info, msg->node, FALSE);
  if (!_gabble_connection_send (info->conn, msg, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Failed to send property change notification to chatroom" };

      lm_message_unref (msg);
      dbus_g_method_return_error (context, &error);
      return;
    }
  lm_message_unref (msg);

  if (!refresh_invitations (muc_channel, info, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  ctx = g_slice_new (set_properties_ctx);
  ctx->context = context;
  ctx->visibility_changed = (was_visible != is_visible);

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

          info = add_activity_info_in_set (conn, room_handle, from,
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

      _gabble_connection_send (conn, msg, NULL);

      lm_message_unref (msg);
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
  ActivityInfo *info;
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

  info = g_hash_table_lookup (conn->olpc_activities_info,
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

      if (info == NULL)
        {
          DEBUG ("... creating new ActivityInfo");
          info = add_activity_info (conn, room_handle);
          tp_handle_set_add (their_invites, room_handle);
        }
      else if (!tp_handle_set_is_member (their_invites, room_handle))
        {
          DEBUG ("... it's the first time that contact invited me, "
              "referencing ActivityInfo on their behalf");
          info->refcount++;
          tp_handle_set_add (their_invites, room_handle);
        }
      tp_handle_unref (room_repo, room_handle);
    }
  else
    {
      activities_changed = FALSE;
      /* we're in the room, so it ought to have an ActivityInfo ref'd */
      g_assert (info != NULL);
    }

  new_properties = lm_message_node_extract_properties (node,
      "property");
  g_assert (new_properties);

  /* before applying the changes, gather enough information to work out
   * whether anything changed */

  old_properties = info->properties;

  was_visible = activity_info_is_visible (info);

  properties_changed = old_properties == NULL
    || properties_contains_new_infos (old_properties, new_properties);

  /* apply the info we found */

  if (tp_strdiff (info->id, id))
    {
      DEBUG ("... recording new activity ID %s", id);
      g_free (info->id);
      info->id = g_strdup (id);
    }

  activity_info_set_properties (info, new_properties);

  /* emit signals and amend our PEP nodes, if necessary */

  is_visible = activity_info_is_visible (info);

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
    refresh_invitations (muc_channel, info, NULL);

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
revoke_invitations (GabbleMucChannel *chan,
                    ActivityInfo *info,
                    GError **error)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) info->conn, TP_HANDLE_TYPE_CONTACT);
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
          activity_info_get_room (info));
      lm_message_node_set_attribute (uninvite_node, "id",
          info->id);

      while (tp_intset_iter_next (&iter))
        {
          const gchar *to = tp_handle_inspect (contact_repo, iter.element);

          lm_message_node_set_attribute (msg->node, "to", to);
          if (!_gabble_connection_send (info->conn, msg, error))
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
      ActivityInfo *info;

      info = g_hash_table_lookup (conn->olpc_activities_info,
          GUINT_TO_POINTER (room_handle));

      if (info == NULL)
        {
          DEBUG ("No info about activity associated with room %s", room);
          return TRUE;
        }

      if (tp_strdiff (id, info->id))
        {
          DEBUG ("Uninvite's activity id (%s) doesn't match our "
              "activity id (%s)", id, info->id);
          return TRUE;
        }

      DEBUG ("remove invite from %s", from);
      activity_info_unref (info);
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
                       ActivityInfo *info)
{
  GabbleConnection *conn = info->conn;
  TpBaseConnection *base = (TpBaseConnection *) info->conn;
  TpHandleSet *my_activities;
  gboolean was_in_our_pep = FALSE;

  /* Revoke invitations we sent for this activity */
  revoke_invitations (chan, info, NULL);

  /* remove it from our advertised activities list, unreffing it in the
   * process if it was in fact advertised */
  my_activities = g_hash_table_lookup (conn->olpc_pep_activities,
      GUINT_TO_POINTER (base->self_handle));
  if (my_activities != NULL)
    {
      if (tp_handle_set_remove (my_activities, info->handle))
        {
          was_in_our_pep = activity_info_is_visible (info);
          activity_info_unref (info);
        }
    }

  /* unref it again (it was referenced on behalf of the channel) */
  activity_info_unref (info);

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
}

static void
muc_channel_pre_invite_cb (GabbleMucChannel *chan,
                           TpHandle invitee,
                           ActivityInfo *info)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) info->conn, TP_HANDLE_TYPE_CONTACT);
  GQuark quark = invitees_quark ();
  TpHandleSet *invitees;
  /* send them the properties */
  LmMessage *msg;

  msg = lm_message_new (tp_handle_inspect (contact_repo, invitee),
      LM_MESSAGE_TYPE_MESSAGE);

  if (activity_info_contribute_properties (info, msg->node, FALSE))
    {
      /* not much we can do about errors - but if this fails, the invitation
       * will too, unless something extremely strange is going on */
      if (!_gabble_connection_send (info->conn, msg, NULL))
        {
          DEBUG ("Unable to send activity properties to invitee");
        }
    }
  lm_message_unref (msg);

  invitees = g_object_get_qdata ((GObject *) chan, quark);
  if (invitees == NULL)
    {
      invitees = tp_handle_set_new (contact_repo);
      g_object_set_qdata_full ((GObject *) chan, quark, invitees,
          (GDestroyNotify) tp_handle_set_destroy);
    }
  tp_handle_set_add (invitees, invitee);
}

static void
muc_channel_contact_join_cb (GabbleMucChannel *chan,
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
  TpHandleSet *rooms = (TpHandleSet *) value;
  remove_invite_foreach_ctx *ctx = (remove_invite_foreach_ctx *) user_data;

  if (tp_handle_set_remove (rooms, ctx->room_handle))
    {
      ActivityInfo *info;

      info = g_hash_table_lookup (ctx->conn->olpc_activities_info,
          GUINT_TO_POINTER (ctx->room_handle));

      g_assert (info != NULL);
      activity_info_unref (info);
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

  /* Forget about all invites we received about this activity */
  forget_activity_invites (conn, room_handle);

  g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb),
      info);
  g_signal_connect (chan, "pre-invite", G_CALLBACK (muc_channel_pre_invite_cb),
      info);
  g_signal_connect (chan, "contact-join",
      G_CALLBACK (muc_channel_contact_join_cb), NULL);
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
