/*
 * conn-mail-notif - Gabble mail notification interface
 * Copyright (C) 2009 Collabora Ltd.
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
#include "conn-mail-notif.h"
#include "namespaces.h"

#include <string.h>

#include "extensions/extensions.h"
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#define DEBUG_FLAG GABBLE_DEBUG_MAIL_NOTIF

#include "connection.h"
#include "debug.h"
#include "util.h"


enum
{
  PROP_CAPABILITIES,
  PROP_UNREAD_MAIL_COUNT,
  PROP_INBOX_URL,
  PROP_METHOD,
  PROP_POST_DATA,
  PROP_UNREAD_MAILS,
  NUM_OF_PROP,
};

struct MailsHelperData
{
  GabbleConnection *conn;
  GHashTable *unread_mails;
  GHashTable *old_mails;
  GPtrArray *mails_added;
};


static GPtrArray empty_array = { 0 };


static void unsubscribe (GabbleConnection *conn, const gchar *name);
static void update_unread_mails (GabbleConnection *conn);


static void
sender_name_owner_changed (TpDBusDaemon *dbus_daemon,
                           const gchar *name,
                           const gchar *new_owner,
                           gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  if (CHECK_STR_EMPTY (new_owner))
    {
      DEBUG ("Sender removed: %s", name);
      unsubscribe (conn, name);
    }
}


static void
unsubscribe (GabbleConnection *conn,
    const gchar *name)
{
  tp_dbus_daemon_cancel_name_owner_watch (conn->daemon, name,
      sender_name_owner_changed, conn);

  g_return_if_fail (g_hash_table_size (conn->mail_subscribers) > 0);

  g_hash_table_remove (conn->mail_subscribers, name);

  if (g_hash_table_size (conn->mail_subscribers) == 0)
    {
      DEBUG ("Last sender unsubscribed, cleaning up!");
      g_free (conn->inbox_url);
      conn->inbox_url = NULL;
      if (conn->unread_mails)
        {
          g_hash_table_unref (conn->unread_mails);
          conn->unread_mails = NULL;
        }
    }
}


static void
gabble_mail_notification_subscribe (GabbleSvcConnectionInterfaceMailNotification *iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  gchar *sender = dbus_g_method_get_sender (context);

  DEBUG ("Subscribe called by: %s", sender);

  if (!(conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY)
      || !conn->daemon)
    {
      tp_dbus_g_method_return_not_implemented (context);
      g_free (sender);
      return;
    }

  if (g_hash_table_lookup_extended (conn->mail_subscribers, sender, NULL, NULL))
    {
      DEBUG ("Sender '%s' is already subscribed!", sender);
      goto done;
    }

  g_hash_table_insert (conn->mail_subscribers, sender, NULL);

  if (g_hash_table_size (conn->mail_subscribers) == 1)
    update_unread_mails(conn);

  tp_dbus_daemon_watch_name_owner (conn->daemon, sender,
      sender_name_owner_changed, conn, NULL);

done:
  gabble_svc_connection_interface_mail_notification_return_from_subscribe (context);
}


static void
gabble_mail_notification_unsubscribe (GabbleSvcConnectionInterfaceMailNotification *iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  gchar *sender =  dbus_g_method_get_sender (context);

  DEBUG ("Unsubscribe called by: %s", sender);

  if (!(conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY)
      || !conn->daemon)
    {
      tp_dbus_g_method_return_not_implemented (context);
      g_free (sender);
      return;
    }

  if (!g_hash_table_lookup_extended (conn->mail_subscribers, sender,
                                    NULL, NULL))
    {
      DEBUG ("Sender '%s' is not subscribed!", sender);
      goto done;
    }

  unsubscribe (conn, sender);

done:
  g_free (sender);
  gabble_svc_connection_interface_mail_notification_return_from_unsubscribe (context);
}


static void
handle_url (GHashTable *mail,
    guint64 tid,
    const gchar *base_url)
{
  gchar *url;

  /* The URL in result is broken. The th=<tid> parameter should be in hexadecimal
   * but it's set in decimal. We could try and fix the string, but the URL does
   * not point exactly where we expect it to point. Let's craft a different
   * URL that do a better job.*/

  /* TODO Make sure we don't have to authenticate again */

  url = g_strdup_printf ("%s/#inbox/%" G_GINT64_MODIFIER "x", base_url, tid);

  tp_asv_take_string (mail, "url", url);
  tp_asv_set_uint32 (mail, "method", GABBLE_HTTP_METHOD_GET);
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

      g_value_init (&sender, addr_type);
      g_value_set_static_boxed (&sender,
          dbus_g_type_specialized_construct (addr_type));

      dbus_g_type_struct_set (&sender,
          0, wocky_xmpp_node_get_attribute (node, "name") ?: "",
          1, wocky_xmpp_node_get_attribute (node, "address") ?: "",
          G_MAXUINT);

      g_ptr_array_add (senders, g_value_get_boxed(&sender));
      g_value_unset (&sender);
    }
  return TRUE;
}


static void
handle_senders (WockyXmppNode *parent_node,
    GHashTable *mail,
    gboolean *dirty)
{
  WockyXmppNode *node;

  node = wocky_xmpp_node_get_child (parent_node, "senders");
  if (node)
    {
      GType addr_list_type = GABBLE_ARRAY_TYPE_MAIL_ADDRESS_LIST;
      GPtrArray *senders, *old_senders;

      senders = g_ptr_array_new ();
      wocky_xmpp_node_each_child (node, sender_each, senders);

      old_senders = tp_asv_get_boxed (mail, "senders", addr_list_type);
      if (!old_senders || senders->len != old_senders->len)
            *dirty = TRUE;

      tp_asv_take_boxed (mail, "senders", addr_list_type, senders);
    }
}


static void
handle_subject (WockyXmppNode *parent_node,
    GHashTable *mail,
    gboolean *dirty)
{
  WockyXmppNode *node;

  node = wocky_xmpp_node_get_child (parent_node, "subject");
  if (node)
    {
      if (tp_strdiff (node->content, tp_asv_get_string (mail, "subject")))
        {
          *dirty = TRUE;
          tp_asv_set_string (mail, "subject", node->content);
        }
    }
}


static void
handle_snippet (WockyXmppNode *parent_node,
    GHashTable *mail,
    gboolean *dirty)
{
  WockyXmppNode *node;

  node = wocky_xmpp_node_get_child (parent_node, "snippet");
  if (node)
    {
      if (tp_strdiff (node->content, tp_asv_get_string (mail, "snippet")))
        {
          *dirty = TRUE;
          tp_asv_set_string (mail, "snippet", node->content);
        }
    }
}


static gboolean
mail_thread_info_each (WockyXmppNode *node,
    gpointer user_data)
{
  struct MailsHelperData *data = user_data;

  if (!tp_strdiff (node->name, "mail-thread-info"))
    {
      GHashTable *mail = NULL;
      const gchar *val_str;
      guint64 *tid;
      gboolean dirty = FALSE;

      val_str = wocky_xmpp_node_get_attribute (node, "tid");

      /* We absolutly need an ID */
      if (!val_str)
        return TRUE;

      tid = g_new (guint64, 1);
      *tid = g_ascii_strtoull (val_str, NULL, 0);

      if (data->old_mails)
        mail = g_hash_table_lookup (data->old_mails, tid);

      if (mail)
        {
          g_hash_table_ref (mail);
          g_hash_table_remove (data->old_mails, tid);
        }
      else
        {
          mail = tp_asv_new ("id", G_TYPE_UINT64, *tid,
                             "type", G_TYPE_UINT, GABBLE_MAIL_TYPE_THREAD,
                             NULL);
          dirty = TRUE;
        }

      val_str = wocky_xmpp_node_get_attribute (node, "date");
      if (val_str)
        {
          guint date;

          date = (guint)(g_ascii_strtoull (val_str, NULL, 0) / 1000l);
          if (date != tp_asv_get_uint32 (mail, "received-timestamp", NULL))
            dirty = TRUE;

          tp_asv_set_uint32 (mail, "received-timestamp", date);
        }

      handle_url (mail, *tid, data->conn->inbox_url);
      handle_senders (node, mail, &dirty);
      handle_subject (node, mail, &dirty);
      handle_snippet (node, mail, &dirty);

      g_hash_table_insert (data->unread_mails, tid, mail);
      if (dirty)
        g_ptr_array_add (data->mails_added, mail);
    }

  return TRUE;
}


static void
store_unread_mails (GabbleConnection *conn,
    WockyXmppNode *mailbox)
{
  GHashTableIter iter;
  GArray *mails_removed;
  struct MailsHelperData data;
  const gchar *url;

  data.unread_mails = g_hash_table_new_full (g_int64_hash,
                                             g_int64_equal,
                                             g_free,
                                             (GDestroyNotify)g_hash_table_unref);
  data.conn = conn;
  data.old_mails = conn->unread_mails;
  conn->unread_mails = data.unread_mails;
  data.mails_added = g_ptr_array_new ();

  url = wocky_xmpp_node_get_attribute (mailbox, "url");
  if (url && tp_strdiff (url, conn->inbox_url))
    {
      /* FIXME figure-out how to use POST data to garantee authentication */
      g_free (conn->inbox_url);
      conn->inbox_url = g_strdup (url);
      gabble_svc_connection_interface_mail_notification_emit_inbox_url_changed (
          conn, conn->inbox_url, GABBLE_HTTP_METHOD_GET, &empty_array);
    }

  /* Store new mails */
  wocky_xmpp_node_each_child (mailbox, mail_thread_info_each, &data);

  /* Generate the list of removed thread IDs */
  if (data.old_mails)
    {
      gpointer key;

      mails_removed = g_array_sized_new (TRUE, TRUE,
          sizeof (guint64), g_hash_table_size (data.old_mails));

      g_hash_table_iter_init (&iter, data.old_mails);
      while (g_hash_table_iter_next (&iter, &key, NULL))
        {
          guint64 tid = *((guint64*)key);
          g_array_append_val (mails_removed, tid);
        }

      g_hash_table_unref (data.old_mails);
    }
  else
      mails_removed = g_array_new (TRUE, TRUE, sizeof (guint64));

  if (data.mails_added->len || mails_removed->len)
    gabble_svc_connection_interface_mail_notification_emit_unread_mails_changed (conn,
        g_hash_table_size (conn->unread_mails), data.mails_added, mails_removed);

  g_ptr_array_free (data.mails_added, TRUE);
  g_array_free (mails_removed, TRUE);
}


static void
query_unread_mails_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  gchar *result_str;
  WockyXmppNode *node;
  WockyPorter *porter = WOCKY_PORTER (source_object);
  WockyXmppStanza *reply = wocky_porter_send_iq_finish (porter, res, &error);

  if (error)
    {
      DEBUG ("Failed retreive unread emails information: %s", error->message);
      g_error_free (error);
      goto end;
    }

  DEBUG ("Got unread mail details");

  result_str = wocky_xmpp_node_to_string (reply->node);
  DEBUG ("%s", result_str);
  g_free (result_str);

  node = wocky_xmpp_node_get_child (reply->node, "mailbox");
  if (node)
    {
      GabbleConnection *conn = GABBLE_CONNECTION (user_data);
      store_unread_mails (conn, node);
    }

end:
  if (reply)
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

  if (g_hash_table_size(conn->mail_subscribers) > 0)
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

  if (!conn->daemon)
    {
      DEBUG ("Failed to connect to dbus daemon: %s", error->message);
      g_error_free (error);
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
  const gchar *sender_name = key;
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  tp_dbus_daemon_cancel_name_owner_watch (conn->daemon,
      sender_name, sender_name_owner_changed, conn);

  return TRUE;
}


void
conn_mail_notif_dispose (GabbleConnection *conn)
{
  if (conn->daemon)
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
  if (conn->unread_mails)
    g_hash_table_unref (conn->unread_mails);
  conn->unread_mails = NULL;

  if (conn->new_mail_handler_id)
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
  IMPLEMENT(subscribe);
  IMPLEMENT(unsubscribe);
#undef IMPLEMENT
}


static GPtrArray *
get_unread_mails (GabbleConnection *conn)
{
  GPtrArray *mails = g_ptr_array_new ();
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, conn->unread_mails);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      GHashTable *mail = value;
      g_ptr_array_add (mails, mail);
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
      prop_quarks[PROP_CAPABILITIES] = g_quark_from_static_string ("Capabilities");
      prop_quarks[PROP_UNREAD_MAIL_COUNT] = g_quark_from_static_string ("UnreadMailCount");
      prop_quarks[PROP_INBOX_URL] = g_quark_from_static_string ("InboxURL");
      prop_quarks[PROP_METHOD] = g_quark_from_static_string ("Method");
      prop_quarks[PROP_POST_DATA] = g_quark_from_static_string ("PostData");
      prop_quarks[PROP_UNREAD_MAILS] = g_quark_from_static_string ("UnreadMails");
    }

  DEBUG ("MailNotification get property %s", g_quark_to_string (name));

  if (name == prop_quarks[PROP_CAPABILITIES])
    g_value_set_uint (value,
        GABBLE_MAIL_NOTIFICATION_HAS_PROP_UNREADMAILCOUNT
        | GABBLE_MAIL_NOTIFICATION_HAS_PROP_UNREADMAILS);
  else if (name == prop_quarks[PROP_UNREAD_MAIL_COUNT])
    g_value_set_uint (value,
        conn->unread_mails ? g_hash_table_size (conn->unread_mails) : 0);
  else if (name == prop_quarks[PROP_INBOX_URL])
    g_value_set_string (value, conn->inbox_url ?: "");
  else if (name == prop_quarks[PROP_METHOD])
    g_value_set_uint (value, GABBLE_HTTP_METHOD_GET);
  else if (name == prop_quarks[PROP_POST_DATA])
    g_value_set_static_boxed (value, &empty_array);
  else if (name == prop_quarks[PROP_UNREAD_MAILS])
    {
      GPtrArray *mails = get_unread_mails(conn);
      g_value_set_boxed (value, mails);
      g_ptr_array_free (mails, TRUE);
    }
  else
    g_assert (!"Unkown mail notification property, please file a bug.");
}
