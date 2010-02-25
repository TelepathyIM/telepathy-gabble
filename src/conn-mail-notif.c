/*
 * conn-mail-notif - Gabble mail notification interface
 * Copyright (C) 2009-2010 Collabora Ltd.
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

/* This is implementation of Gmail Notification protocol as documented at
 * http://code.google.com/apis/talk/jep_extensions/gmail.html
 * This is the only protocol supported at the moment. To add new protocol,
 * one would have to split the google specific parts wich are the
 * update_unread_mails() function and the new-mail signal on google xml
 * namespace. The data structure and suscription mechanism shall remain across
 * protocols.
 */

#include "config.h"

#include "conn-mail-notif.h"

#include <string.h>

#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_MAIL_NOTIF
#include "connection.h"
#include "debug.h"
#include "extensions/extensions.h"
#include "namespaces.h"
#include "util.h"


enum
{
  PROP_MAIL_NOTIFICATION_FLAGS,
  PROP_UNREAD_MAIL_COUNT,
  PROP_UNREAD_MAILS,
  PROP_MAIL_ADDRESS,
  NUM_OF_PROP,
};


static void unsubscribe (GabbleConnection *conn, const gchar *name,
    gboolean remove_all);
static void update_unread_mails (GabbleConnection *conn);


static void
subscriber_name_owner_changed (TpDBusDaemon *dbus_daemon,
                           const gchar *name,
                           const gchar *new_owner,
                           gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  if (CHECK_STR_EMPTY (new_owner))
    {
      DEBUG ("Sender removed: %s", name);
      unsubscribe (conn, name, TRUE);
    }
}


static void
unsubscribe (GabbleConnection *conn,
    const gchar *name, gboolean remove_all)
{
  guint count;

  g_return_if_fail (g_hash_table_size (conn->mail_subscribers) > 0);

  count = GPOINTER_TO_UINT (
          g_hash_table_lookup (conn->mail_subscribers, name));

  if (count == 1 || remove_all)
    {
      tp_dbus_daemon_cancel_name_owner_watch (conn->daemon, name,
          subscriber_name_owner_changed, conn);

      g_hash_table_remove (conn->mail_subscribers, name);

      if (g_hash_table_size (conn->mail_subscribers) == 0)
        {
          DEBUG ("Last subscriber unsubscribed, cleaning up!");
          g_free (conn->inbox_url);
          conn->inbox_url = NULL;

          if (conn->unread_mails != NULL)
            {
              g_hash_table_unref (conn->unread_mails);
              conn->unread_mails = NULL;
            }
        }
    }
  else
    {
      g_hash_table_insert (conn->mail_subscribers, g_strdup (name),
          GUINT_TO_POINTER (--count));
    }
}


static inline gboolean
check_supported_or_dbus_return (GabbleConnection *conn,
    DBusGMethodInvocation *context)
{
  if (TP_BASE_CONNECTION (conn)->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      GError e = { TP_ERRORS, TP_ERROR_DISCONNECTED, "Not connected" };
      dbus_g_method_return_error (context, &e);
      return TRUE;
    }

  if (!(conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY))
    {
      tp_dbus_g_method_return_not_implemented (context);
      return TRUE;
    }

  return FALSE;
}


static void
gabble_mail_notification_subscribe (GabbleSvcConnectionInterfaceMailNotification *iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  gchar *subscriber;
  guint count;

  if (check_supported_or_dbus_return (conn, context))
      return;

  subscriber = dbus_g_method_get_sender (context);

  DEBUG ("Subscribe called by: %s", subscriber);

  count = GPOINTER_TO_UINT (
      g_hash_table_lookup (conn->mail_subscribers, subscriber));

  /* Gives subscriber ownership to mail_subscribers hash table */
  g_hash_table_insert (conn->mail_subscribers, subscriber,
      GUINT_TO_POINTER (++count));

  if (count == 1)
    {
      if (g_hash_table_size (conn->mail_subscribers) == 1)
        update_unread_mails (conn);

      tp_dbus_daemon_watch_name_owner (conn->daemon, subscriber,
          subscriber_name_owner_changed, conn, NULL);
    }

  gabble_svc_connection_interface_mail_notification_return_from_subscribe (
      context);
}


static void
gabble_mail_notification_unsubscribe (GabbleSvcConnectionInterfaceMailNotification *iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  gchar *subscriber;

  if (check_supported_or_dbus_return (conn, context))
      return;

  subscriber = dbus_g_method_get_sender (context);

  DEBUG ("Unsubscribe called by: %s", subscriber);

  if (!g_hash_table_lookup_extended (conn->mail_subscribers, subscriber,
                                    NULL, NULL))
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Not subscribed" };

      DEBUG ("Subscriber '%s' is not subscribed!", subscriber);

      dbus_g_method_return_error (context, &e);
      g_free (subscriber);
      return;
    }

  unsubscribe (conn, subscriber, FALSE);

  g_free (subscriber);
  gabble_svc_connection_interface_mail_notification_return_from_unsubscribe (context);
}


static void
gabble_mail_notification_request_inbox_url (
    GabbleSvcConnectionInterfaceMailNotification *iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  GValueArray *result;
  GPtrArray *empty_array;

  if (check_supported_or_dbus_return (conn, context))
    return;

  /* TODO Make sure we don't have to authenticate again */

  empty_array = g_ptr_array_new ();

  result = tp_value_array_build (3,
      G_TYPE_STRING, conn->inbox_url ? conn->inbox_url : "",
      G_TYPE_UINT, GABBLE_HTTP_METHOD_GET,
      GABBLE_ARRAY_TYPE_HTTP_POST_DATA_LIST, empty_array,
      G_TYPE_INVALID);

  gabble_svc_connection_interface_mail_notification_return_from_request_inbox_url (
      context, result);

  g_value_array_free (result);
  g_ptr_array_free (empty_array, TRUE);
}


static void
gabble_mail_notification_request_mail_url (
    GabbleSvcConnectionInterfaceMailNotification *iface,
    const gchar *in_id,
    const GValue *in_url_data,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  GValueArray *result;
  gchar *url = NULL;
  GPtrArray *empty_array;

  if (check_supported_or_dbus_return (conn, context))
    return;

  /* TODO Make sure we don't have to authenticate again */

  if (conn->inbox_url != NULL)
    {
      /* IDs are decimal on the XMPP side and hexadecimal on the wemail side. */
      guint64 tid = g_ascii_strtoull (in_id, NULL, 0);
      url = g_strdup_printf ("%s/#inbox/%" G_GINT64_MODIFIER "x",
          conn->inbox_url, tid);
    }

  empty_array = g_ptr_array_new ();

  result = tp_value_array_build (3,
      G_TYPE_STRING, url ? url : "",
      G_TYPE_UINT, GABBLE_HTTP_METHOD_GET,
      GABBLE_ARRAY_TYPE_HTTP_POST_DATA_LIST, empty_array,
      G_TYPE_INVALID);

  gabble_svc_connection_interface_mail_notification_return_from_request_mail_url (
      context, result);

  g_value_array_free (result);
  g_ptr_array_free (empty_array, TRUE);
}


static gboolean
sender_each (WockyXmppNode *node,
    gpointer user_data)
{
  GPtrArray *senders = user_data;

  if (!tp_strdiff ("1", wocky_xmpp_node_get_attribute (node, "unread")))
    {
      GType addr_type = GABBLE_STRUCT_TYPE_MAIL_ADDRESS;
      GValue sender = {0};
      const gchar *name;
      const gchar *address;

      g_value_init (&sender, addr_type);
      g_value_set_static_boxed (&sender,
          dbus_g_type_specialized_construct (addr_type));

      name = wocky_xmpp_node_get_attribute (node, "name");

      if (name == NULL)
        name = "";

      address = wocky_xmpp_node_get_attribute (node, "address");

      if (address == NULL)
        address = "";

      dbus_g_type_struct_set (&sender,
          0, name,
          1, address,
          G_MAXUINT);

      g_ptr_array_add (senders, g_value_get_boxed (&sender));
      g_value_unset (&sender);
    }
  return TRUE;
}


static gboolean
handle_senders (WockyXmppNode *parent_node,
    GHashTable *mail)
{
  gboolean dirty = FALSE;
  WockyXmppNode *node;

  node = wocky_xmpp_node_get_child (parent_node, "senders");
  if (node != NULL)
    {
      GType addr_list_type = GABBLE_ARRAY_TYPE_MAIL_ADDRESS_LIST;
      GPtrArray *senders, *old_senders;

      senders = g_ptr_array_new ();
      wocky_xmpp_node_each_child (node, sender_each, senders);

      old_senders = tp_asv_get_boxed (mail, "senders", addr_list_type);

      if (old_senders == NULL || senders->len != old_senders->len)
            dirty = TRUE;

      tp_asv_take_boxed (mail, "senders", addr_list_type, senders);
    }

  return dirty;
}


static gboolean
handle_subject (WockyXmppNode *parent_node,
    GHashTable *mail)
{
  gboolean dirty = FALSE;
  WockyXmppNode *node;

  node = wocky_xmpp_node_get_child (parent_node, "subject");
  if (node != NULL)
    {
      if (tp_strdiff (node->content, tp_asv_get_string (mail, "subject")))
        {
          dirty = TRUE;
          tp_asv_set_string (mail, "subject", node->content);
        }
    }

  return dirty;
}


static gboolean
handle_snippet (WockyXmppNode *parent_node,
    GHashTable *mail)
{
  gboolean dirty = FALSE;
  WockyXmppNode *node;

  node = wocky_xmpp_node_get_child (parent_node, "snippet");
  if (node != NULL)
    {
      if (tp_strdiff (node->content, tp_asv_get_string (mail, "content")))
        {
          dirty = TRUE;
          tp_asv_set_boolean (mail, "truncated", TRUE);
          tp_asv_set_string (mail, "content", node->content);
        }
    }

  return dirty;
}


/* Structure used has user_data mail_thread_info_each callback */
typedef struct
{
  GabbleConnection *conn;
  /* stolen from conn -> unread_mails, the left items in this is 
   * represent the removed emails */
  GHashTable *old_mails;
  GPtrArray *mails_added;
} MailThreadCollector;

static gboolean
mail_thread_info_each (WockyXmppNode *node,
    gpointer user_data)
{
  MailThreadCollector *collector = user_data;

  if (!tp_strdiff (node->name, "mail-thread-info"))
    {
      GHashTable *mail = NULL;
      const gchar *val_str;
      gchar *tid;
      gboolean dirty = FALSE;

      val_str = wocky_xmpp_node_get_attribute (node, "tid");

      /* We absolutly need an ID */
      if (val_str == NULL)
        return TRUE;

      tid = g_strdup (val_str);

      if (collector->old_mails != NULL)
        {
          mail = g_hash_table_lookup (collector->old_mails, tid);
          g_hash_table_steal (collector->old_mails, tid);
        }

      if (mail == NULL)
        {
          mail = tp_asv_new ("id", G_TYPE_STRING, tid,
                             "url-data", G_TYPE_STRING, "",
                             NULL);
          dirty = TRUE;
        }

      val_str = wocky_xmpp_node_get_attribute (node, "date");

      if (val_str != NULL)
        {
          gint64 date;

          date = (g_ascii_strtoll (val_str, NULL, 0) / 1000l);
          if (date != tp_asv_get_int64 (mail, "received-timestamp", NULL))
            dirty = TRUE;

          tp_asv_set_int64 (mail, "received-timestamp", date);
        }

      if (handle_senders (node, mail))
        dirty = TRUE;

      if (handle_subject (node, mail))
        dirty = TRUE;

      if (handle_snippet (node, mail))
        dirty = TRUE;

      /* gives tid ownership to unread_mails hash table */
      g_hash_table_insert (collector->conn->unread_mails, tid, mail);

      if (dirty)
        g_ptr_array_add (collector->mails_added, mail);
    }

  return TRUE;
}


static void
store_unread_mails (GabbleConnection *conn,
    WockyXmppNode *mailbox)
{
  GHashTableIter iter;
  GPtrArray *mails_removed;
  MailThreadCollector collector;
  const gchar *url;

  collector.conn = conn;
  collector.old_mails = conn->unread_mails;
  conn->unread_mails = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_hash_table_unref);
  collector.mails_added = g_ptr_array_new ();

  url = wocky_xmpp_node_get_attribute (mailbox, "url");
  g_free (conn->inbox_url);
  conn->inbox_url = g_strdup (url);

  /* Store new mails */
  wocky_xmpp_node_each_child (mailbox, mail_thread_info_each, &collector);

  /* Generate the list of removed thread IDs */
  mails_removed = g_ptr_array_new_with_free_func (g_free);

  if (collector.old_mails != NULL)
    {
      gpointer key;

      g_hash_table_iter_init (&iter, collector.old_mails);

      while (g_hash_table_iter_next (&iter, &key, NULL))
        {
          gchar *tid = key;
          g_ptr_array_add (mails_removed, g_strdup (tid));
        }

      g_hash_table_unref (collector.old_mails);
    }
  g_ptr_array_add (mails_removed, NULL);

  if (collector.mails_added->len > 0 || mails_removed->len > 0)
    gabble_svc_connection_interface_mail_notification_emit_unread_mails_changed (
        conn, g_hash_table_size (conn->unread_mails), collector.mails_added,
        (const char **)mails_removed->pdata);

  g_ptr_array_free (collector.mails_added, TRUE);
  g_ptr_array_free (mails_removed, TRUE);
}


static void
query_unread_mails_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  WockyXmppNode *node;
  WockyPorter *porter = WOCKY_PORTER (source_object);
  WockyXmppStanza *reply = wocky_porter_send_iq_finish (porter, res, &error);

  if (error != NULL)
    {
      DEBUG ("Failed retreive unread emails information: %s", error->message);
      g_error_free (error);
      goto end;
    }

  DEBUG ("Got unread mail details");

  node = wocky_xmpp_node_get_child (reply->node, "mailbox");

  if (node != NULL)
    {
      GabbleConnection *conn = GABBLE_CONNECTION (user_data);
      store_unread_mails (conn, node);
    }

end:
  if (reply != NULL)
    g_object_unref (reply);
}


static void
update_unread_mails (GabbleConnection *conn)
{
  WockyXmppStanza *query;
  WockyPorter *porter = wocky_session_get_porter (conn->session);

  DEBUG ("Updating unread mails information");

  query = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, NULL,
      WOCKY_NODE, "query",
      WOCKY_NODE_XMLNS, NS_GOOGLE_MAIL_NOTIFY,
      WOCKY_NODE_END,
      WOCKY_STANZA_END);
  wocky_porter_send_iq_async (porter, query, NULL, query_unread_mails_cb, conn);
  g_object_unref (query);
}


static gboolean
new_mail_handler (WockyPorter *porter,
    WockyXmppStanza *stanza,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  if (g_hash_table_size (conn->mail_subscribers) > 0)
    {
      DEBUG ("Got Google <new-mail> notification");
      update_unread_mails (conn);
    }

  return TRUE;
}


static void
connection_status_changed (GabbleConnection *conn,
    TpConnectionStatus status,
    TpConnectionStatusReason reason,
    gpointer user_data)
{
  if (status == TP_CONNECTION_STATUS_CONNECTED
      && conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY)
    {
      DEBUG ("Connected, registering Google 'new-mail' notification");

      conn->new_mail_handler_id =
        wocky_porter_register_handler (wocky_session_get_porter (conn->session),
            WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
            NULL, WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
            new_mail_handler, conn,
            WOCKY_NODE, "new-mail",
              WOCKY_NODE_XMLNS, NS_GOOGLE_MAIL_NOTIFY,
              WOCKY_NODE_END,
              WOCKY_STANZA_END);
    }
}


void
conn_mail_notif_init (GabbleConnection *conn)
{
  GError *error = NULL;

  conn->daemon = tp_dbus_daemon_dup (&error);

  if (conn->daemon == NULL)
    {
      g_error ("Failed to connect to dbus daemon: %s", error->message);
    }

  conn->mail_subscribers = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, NULL);
  conn->inbox_url = NULL;
  conn->unread_mails = NULL;

  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed), conn);
}


static gboolean
foreach_cancel_watch (gpointer key,
    gpointer value,
    gpointer user_data)
{
  const gchar *subscriber = key;
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  tp_dbus_daemon_cancel_name_owner_watch (conn->daemon,
      subscriber, subscriber_name_owner_changed, conn);

  return TRUE;
}


void
conn_mail_notif_dispose (GabbleConnection *conn)
{
  if (conn->daemon != NULL)
    {
      g_hash_table_foreach_remove (conn->mail_subscribers,
          foreach_cancel_watch, conn);
      g_hash_table_unref (conn->mail_subscribers);
      conn->mail_subscribers = NULL;
      g_object_unref (conn->daemon);
      conn->daemon = NULL;
    }

  g_free (conn->inbox_url);
  conn->inbox_url = NULL;

  if (conn->unread_mails != NULL)
    g_hash_table_unref (conn->unread_mails);

  conn->unread_mails = NULL;

  if (conn->new_mail_handler_id != 0)
    {
      WockyPorter *porter = wocky_session_get_porter (conn->session);
      wocky_porter_unregister_handler (porter, conn->new_mail_handler_id);
      conn->new_mail_handler_id = 0;
    }
}


void
conn_mail_notif_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  GabbleSvcConnectionInterfaceMailNotificationClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_connection_interface_mail_notification_implement_##x (\
    klass, gabble_mail_notification_##x)
  IMPLEMENT (subscribe);
  IMPLEMENT (unsubscribe);
  IMPLEMENT (request_inbox_url);
  IMPLEMENT (request_mail_url);
#undef IMPLEMENT
}


static GPtrArray *
get_unread_mails (GabbleConnection *conn)
{
  GPtrArray *mails = g_ptr_array_new ();
  GHashTableIter iter;
  gpointer value;

  if (conn->unread_mails != NULL)
    {
      g_hash_table_iter_init (&iter, conn->unread_mails);

      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          GHashTable *mail = value;
          g_ptr_array_add (mails, mail);
        }
    }

  return mails;
}


void
conn_mail_notif_properties_getter (GObject *object,
    GQuark interface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  static GQuark prop_quarks[NUM_OF_PROP] = {0};
  GabbleConnection *conn = GABBLE_CONNECTION (object);

  if (G_UNLIKELY (prop_quarks[0] == 0))
    {
      prop_quarks[PROP_MAIL_NOTIFICATION_FLAGS] =
        g_quark_from_static_string ("MailNotificationFlags");
      prop_quarks[PROP_UNREAD_MAIL_COUNT] =
        g_quark_from_static_string ("UnreadMailCount");
      prop_quarks[PROP_UNREAD_MAILS] =
        g_quark_from_static_string ("UnreadMails");
      prop_quarks[PROP_MAIL_ADDRESS] =
        g_quark_from_static_string ("MailAddress");
    }

  DEBUG ("MailNotification get property %s", g_quark_to_string (name));

  if (name == prop_quarks[PROP_MAIL_NOTIFICATION_FLAGS])
    {
      if (conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY)
        g_value_set_uint (value,
            GABBLE_MAIL_NOTIFICATION_FLAG_SUPPORTS_UNREAD_MAIL_COUNT
            | GABBLE_MAIL_NOTIFICATION_FLAG_SUPPORTS_UNREAD_MAILS
            | GABBLE_MAIL_NOTIFICATION_FLAG_SUPPORTS_REQUEST_INBOX_URL
            | GABBLE_MAIL_NOTIFICATION_FLAG_SUPPORTS_REQUEST_MAIL_URL
            | GABBLE_MAIL_NOTIFICATION_FLAG_THREAD_BASED
            );
      else
        g_value_set_uint (value, 0);
    }
  else if (name == prop_quarks[PROP_UNREAD_MAIL_COUNT])
    {
      g_value_set_uint (value,
          conn->unread_mails ? g_hash_table_size (conn->unread_mails) : 0);
    }
  else if (name == prop_quarks[PROP_UNREAD_MAILS])
    {
      GPtrArray *mails = get_unread_mails (conn);
      g_value_set_boxed (value, mails);
      g_ptr_array_free (mails, TRUE);
    }
  else if (name == prop_quarks[PROP_MAIL_ADDRESS])
    {
      TpBaseConnection *base = TP_BASE_CONNECTION (object);
      TpHandleRepoIface *contact_handles = 
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
      TpHandle self = tp_base_connection_get_self_handle (base);
      const gchar *bare_jid = tp_handle_inspect (contact_handles, self);
      
      /* After some testing I found that the bare jid (username@stream_server)
       * always represent the e-mail address on Google account. */
      g_value_set_string (value, bare_jid);
    }
  else
    {
      g_assert_not_reached ();
    }
}
