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
#include "namespaces.h"
#include "util.h"

/* The Google server stops pushing <new-mail> updates for the periode of
 * POLL_DURATION seconds. To ensure that MailNotification remains accurate,
 * we manually update every POLL_DELAY second the mail information.
 */
#define POLL_DELAY 5
#define POLL_DURATION 60

enum
{
  PROP_MAIL_NOTIFICATION_FLAGS,
  PROP_UNREAD_MAIL_COUNT,
  PROP_UNREAD_MAILS,
  PROP_MAIL_ADDRESS,
  NUM_OF_PROP,
};

struct _GabbleConnectionMailNotificationPrivate
{
  gboolean interested;
  gchar *inbox_url;
  GHashTable *unread_mails;
  guint unread_count;
  guint new_mail_handler_id;
  guint poll_timeout_id;
  guint poll_count;
  GList *inbox_url_requests; /* list of DBusGMethodInvocation */
};


static void update_unread_mails (GabbleConnection *conn);

static void
return_from_request_inbox_url (GabbleConnection *conn)
{
  GValueArray *result = NULL;
  GPtrArray *empty_array = NULL;
  GError *error = NULL;
  GList *it;
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;

  if (priv->inbox_url != NULL && priv->inbox_url[0] == '\0')
      {
        error = g_error_new (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
            "Server did not provide base URL.");
      }
  else if (priv->inbox_url == NULL)
      {
        error = g_error_new (TP_ERRORS, TP_ERROR_DISCONNECTED,
            "Connection was disconnected during request.");
      }
  else
    {
      empty_array = g_ptr_array_new ();
      result = tp_value_array_build (3,
          G_TYPE_STRING, priv->inbox_url,
          G_TYPE_UINT, TP_HTTP_METHOD_GET,
          TP_ARRAY_TYPE_HTTP_POST_DATA_LIST, empty_array,
          G_TYPE_INVALID);
    }

  it = priv->inbox_url_requests;

  while (it != NULL)
  {
    DBusGMethodInvocation *context = it->data;

    if (error != NULL)
      dbus_g_method_return_error (context, error);
    else
      tp_svc_connection_interface_mail_notification_return_from_request_inbox_url (
          context, result);

    it = g_list_next (it);
  }

  if (error == NULL)
    {
      g_value_array_free (result);
      g_ptr_array_free (empty_array, TRUE);
    }
  else
    {
      g_error_free (error);
    }

  g_list_free (priv->inbox_url_requests);
  priv->inbox_url_requests = NULL;
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
gabble_mail_notification_request_inbox_url (
    TpSvcConnectionInterfaceMailNotification *iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;

  if (check_supported_or_dbus_return (conn, context))
    return;

  /* TODO Make sure we don't have to authenticate again */

  priv->inbox_url_requests = g_list_append (priv->inbox_url_requests, context);

  if (priv->inbox_url != NULL)
    return_from_request_inbox_url (conn);
}


static void
gabble_mail_notification_request_mail_url (
    TpSvcConnectionInterfaceMailNotification *iface,
    const gchar *in_id,
    const GValue *in_url_data,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;

  if (check_supported_or_dbus_return (conn, context))
    return;

  /* TODO Make sure we don't have to authenticate again */

  if (priv->inbox_url != NULL && priv->inbox_url[0] != 0)
    {
      GValueArray *result;
      gchar *url = NULL;
      GPtrArray *empty_array;

      /* IDs are decimal on the XMPP side and hexadecimal on the wemail side. */
      guint64 tid = g_ascii_strtoull (in_id, NULL, 0);
      url = g_strdup_printf ("%s/#inbox/%" G_GINT64_MODIFIER "x",
          priv->inbox_url, tid);

      empty_array = g_ptr_array_new ();

      result = tp_value_array_build (3,
          G_TYPE_STRING, url ? url : "",
          G_TYPE_UINT, TP_HTTP_METHOD_GET,
          TP_ARRAY_TYPE_HTTP_POST_DATA_LIST, empty_array,
          G_TYPE_INVALID);

      tp_svc_connection_interface_mail_notification_return_from_request_mail_url (
          context, result);

      g_value_array_free (result);
      g_ptr_array_free (empty_array, TRUE);
      g_free (url);
    }
  else
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Failed to retrieve URL from server."};
      dbus_g_method_return_error (context, &error);
    }
}


static gboolean
sender_each (WockyNode *node,
    gpointer user_data)
{
  GPtrArray *senders = user_data;

  if (!tp_strdiff ("1", wocky_node_get_attribute (node, "unread")))
    {
      GValueArray *sender;
      const gchar *name;
      const gchar *address;

      name = wocky_node_get_attribute (node, "name");
      address = wocky_node_get_attribute (node, "address");

      sender = tp_value_array_build (2,
          G_TYPE_STRING, name ? name : "",
          G_TYPE_STRING, address ? address : "",
          G_TYPE_INVALID);

      g_ptr_array_add (senders, sender);
    }
  return TRUE;
}


static gboolean
handle_senders (WockyNode *parent_node,
    GHashTable *mail)
{
  gboolean dirty = FALSE;
  WockyNode *node;

  node = wocky_node_get_child (parent_node, "senders");
  if (node != NULL)
    {
      GType addr_list_type = TP_ARRAY_TYPE_MAIL_ADDRESS_LIST;
      GPtrArray *senders, *old_senders;

      senders = g_ptr_array_new ();
      wocky_node_each_child (node, sender_each, senders);

      old_senders = tp_asv_get_boxed (mail, "senders", addr_list_type);

      if (old_senders == NULL || senders->len != old_senders->len)
            dirty = TRUE;

      tp_asv_take_boxed (mail, "senders", addr_list_type, senders);
    }

  return dirty;
}


static gboolean
handle_subject (WockyNode *parent_node,
    GHashTable *mail)
{
  gboolean dirty = FALSE;
  WockyNode *node;

  node = wocky_node_get_child (parent_node, "subject");
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
handle_snippet (WockyNode *parent_node,
    GHashTable *mail)
{
  gboolean dirty = FALSE;
  WockyNode *node;

  node = wocky_node_get_child (parent_node, "snippet");
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
mail_thread_info_each (WockyNode *node,
    gpointer user_data)
{
  MailThreadCollector *collector = user_data;

  if (!tp_strdiff (node->name, "mail-thread-info"))
    {
      GHashTable *mail = NULL;
      const gchar *val_str;
      gchar *tid;
      gboolean dirty = FALSE;

      val_str = wocky_node_get_attribute (node, "tid");

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

      val_str = wocky_node_get_attribute (node, "date");

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
      g_hash_table_insert (collector->conn->mail_priv->unread_mails, tid, mail);

      if (dirty)
        g_ptr_array_add (collector->mails_added, mail);
    }

  return TRUE;
}


static void
store_unread_mails (GabbleConnection *conn,
    WockyNode *mailbox)
{
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;
  GHashTableIter iter;
  GPtrArray *mails_removed;
  MailThreadCollector collector;
  const gchar *url, *unread_count;

  collector.conn = conn;
  collector.old_mails = priv->unread_mails;
  priv->unread_mails = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_hash_table_unref);
  collector.mails_added = g_ptr_array_new ();

  url = wocky_node_get_attribute (mailbox, "url");
  g_free (priv->inbox_url);

  /* Use empty string to differentiate pending request from server failing to
     provide an URL.*/
  if (url != NULL)
    priv->inbox_url = g_strdup (url);
  else
    priv->inbox_url = g_strdup ("");

  /* Store new mails */
  wocky_node_each_child (mailbox, mail_thread_info_each, &collector);

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

  unread_count = wocky_node_get_attribute (mailbox, "total-matched");

  if (unread_count != NULL)
    priv->unread_count = (guint)g_ascii_strtoll (unread_count, NULL, 0);
  else
    priv->unread_count = g_hash_table_size (priv->unread_mails);

  tp_svc_connection_interface_mail_notification_emit_unread_mails_changed (
      conn, priv->unread_count, collector.mails_added,
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
  WockyPorter *porter = WOCKY_PORTER (source_object);
  WockyStanza *reply = wocky_porter_send_iq_finish (porter, res, &error);
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  if (reply == NULL ||
      wocky_stanza_extract_errors (reply, NULL, &error, NULL, NULL))
    {
      DEBUG ("Failed retreive unread emails information: %s", error->message);
      g_error_free (error);
    }
  else if (conn->mail_priv->interested)
    {
      WockyNode *node = wocky_node_get_child (
          wocky_stanza_get_top_node (reply), "mailbox");

      DEBUG ("Got unread mail details");

      if (node != NULL)
        store_unread_mails (conn, node);
    }
  /* else we no longer care about unread mail, so ignore it */

  tp_clear_object (&reply);

  return_from_request_inbox_url (conn);
}


static void
update_unread_mails (GabbleConnection *conn)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  WockyStanza *query;
  WockyPorter *porter = wocky_session_get_porter (conn->session);

  if (base_conn->status != TP_CONNECTION_STATUS_CONNECTED)
    return;

  if (!(conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY))
    return;

  DEBUG ("Updating unread mails information");

  query = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, NULL,
      '(', "query",
        ':', NS_GOOGLE_MAIL_NOTIFY,
      ')',
      NULL);
  wocky_porter_send_iq_async (porter, query, NULL,
      query_unread_mails_cb, conn);
  g_object_unref (query);
}

static gboolean
poll_unread_mails_cb (gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;

  if (priv->poll_count * POLL_DELAY >= POLL_DURATION)
    {
      DEBUG ("%i seconds since <new-mail>, stopping polling",
          priv->poll_count * POLL_DELAY);
      priv->poll_timeout_id = 0;
      priv->poll_count = 0;
      return FALSE;
    }

  priv->poll_count++;

  /* When no subscriber, keep counting time, but don't actually update the
   * data since nobody would care about it */
  if (priv->interested)
    {
      update_unread_mails (conn);
      DEBUG ("%i seconds since <new-mail>, still polling",
          priv->poll_count * POLL_DELAY);
    }

  return TRUE;
}


static gboolean
new_mail_handler (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  if (conn->mail_priv->interested)
    {
      DEBUG ("Got Google <new-mail> notification");
      update_unread_mails (conn);

      conn->mail_priv->poll_count = 0;
      if (conn->mail_priv->poll_timeout_id == 0)
        {
          DEBUG ("Starting to poll mail for next %i seconds", POLL_DURATION);
          conn->mail_priv->poll_timeout_id = g_timeout_add_seconds (
              POLL_DELAY,
              (GSourceFunc) poll_unread_mails_cb, conn);
        }
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
      && (conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY))
    {
      DEBUG ("Connected, registering Google 'new-mail' notification");

      conn->mail_priv->new_mail_handler_id =
        wocky_porter_register_handler_from_server (
            wocky_session_get_porter (conn->session),
            WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
            WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
            new_mail_handler, conn,
            '(', "new-mail",
              ':', NS_GOOGLE_MAIL_NOTIFY,
            ')',
            NULL);

      if (conn->mail_priv->interested)
        {
          DEBUG ("Someone is already interested in MailNotification");
          update_unread_mails (conn);
        }
    }
}

/* called on transition from 0 to 1 interested clients */
static void
mail_clients_interested_cb (GabbleConnection *self,
    const gchar *token G_GNUC_UNUSED,
    gpointer nil G_GNUC_UNUSED)
{
  DEBUG ("The first client is interested");
  self->mail_priv->interested = TRUE;

  update_unread_mails (self);
}

/* called on transition from 1 to 0 interested clients */
static void
mail_clients_uninterested_cb (GabbleConnection *self,
    const gchar *token G_GNUC_UNUSED,
    gpointer nil G_GNUC_UNUSED)
{
  DEBUG ("All clients lost interest, cleaning up");
  self->mail_priv->interested = FALSE;
  self->mail_priv->unread_count = 0;

  tp_clear_pointer (&self->mail_priv->inbox_url, g_free);
  return_from_request_inbox_url (self);

  tp_clear_pointer (&self->mail_priv->unread_mails, g_hash_table_unref);
}

void
conn_mail_notif_init (GabbleConnection *conn)
{
  GabbleConnectionMailNotificationPrivate *priv;

  conn->mail_priv = g_slice_new0(GabbleConnectionMailNotificationPrivate);
  priv = conn->mail_priv;

  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed), conn);

  g_signal_connect (conn,
      "clients-interested::" TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION,
      G_CALLBACK (mail_clients_interested_cb), NULL);
  g_signal_connect (conn,
      "clients-uninterested::" TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION,
      G_CALLBACK (mail_clients_uninterested_cb), NULL);
}

void
conn_mail_notif_dispose (GabbleConnection *conn)
{
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;

  if (priv == NULL)
    return;

  tp_clear_pointer (&priv->inbox_url, g_free);

  return_from_request_inbox_url (conn);

  tp_clear_pointer (&priv->unread_mails, g_hash_table_unref);

  priv->unread_count = 0;

  if (priv->new_mail_handler_id != 0)
    {
      WockyPorter *porter = wocky_session_get_porter (conn->session);
      wocky_porter_unregister_handler (porter, priv->new_mail_handler_id);
      priv->new_mail_handler_id = 0;
    }

  if (priv->poll_timeout_id != 0)
    g_source_remove (priv->poll_timeout_id);

  g_slice_free (GabbleConnectionMailNotificationPrivate, priv);
  conn->mail_priv = NULL;
}


void
conn_mail_notif_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceMailNotificationClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_mail_notification_implement_##x (\
    klass, gabble_mail_notification_##x)
  IMPLEMENT (request_inbox_url);
  IMPLEMENT (request_mail_url);
#undef IMPLEMENT
}


static GPtrArray *
get_unread_mails (GabbleConnection *conn)
{
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;
  GPtrArray *mails = g_ptr_array_new ();
  GHashTableIter iter;
  gpointer value;

  if (priv->unread_mails != NULL)
    {
      g_hash_table_iter_init (&iter, priv->unread_mails);

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
  GabbleConnectionMailNotificationPrivate *priv = conn->mail_priv;

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
            TP_MAIL_NOTIFICATION_FLAG_SUPPORTS_UNREAD_MAIL_COUNT
            | TP_MAIL_NOTIFICATION_FLAG_SUPPORTS_UNREAD_MAILS
            | TP_MAIL_NOTIFICATION_FLAG_SUPPORTS_REQUEST_INBOX_URL
            | TP_MAIL_NOTIFICATION_FLAG_SUPPORTS_REQUEST_MAIL_URL
            | TP_MAIL_NOTIFICATION_FLAG_THREAD_BASED
            );
      else
        g_value_set_uint (value, 0);
    }
  else if (name == prop_quarks[PROP_UNREAD_MAIL_COUNT])
    {
      g_value_set_uint (value, priv->unread_count);
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
