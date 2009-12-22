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

static GPtrArray empty_array = { 0 };

static void unsubscribe (GabbleConnection *conn, const gchar *name);

static void
sender_name_owner_changed (TpDBusDaemon *dbus_daemon,
                           const gchar *name,
                           const gchar *new_owner,
                           gpointer user_data)
{
  GabbleConnection *conn = user_data;

  if (new_owner == NULL || new_owner[0] == '\0')
    {
      DEBUG ("Sender removed: %s", name);
      unsubscribe (conn, name);
    }
}

static void
unsubscribe (GabbleConnection *conn, const gchar *name)
{
  tp_dbus_daemon_cancel_name_owner_watch (conn->daemon, name,
      sender_name_owner_changed, conn);

  g_return_if_fail (conn->mail_subscribers_count > 0);

  conn->mail_subscribers_count -= 1;
  g_datalist_remove_data (&conn->mail_subscribers, name);

  if (conn->mail_subscribers_count == 0)
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
  const gchar *sender = dbus_g_method_get_sender (context);

  DEBUG ("Subscribe called by: %s", sender);

  if (!(conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY)
      || !conn->daemon)
    {
      tp_dbus_g_method_return_not_implemented (context);
      return;
    }

  if (g_datalist_get_data (&conn->mail_subscribers, sender))
    {
      DEBUG ("Sender '%s' is already subscribed!", sender);
      goto done;
    }

  conn->mail_subscribers_count += 1;
  g_datalist_set_data (&conn->mail_subscribers, sender, conn);

  /* TODO Get Mails Data */
  
  tp_dbus_daemon_watch_name_owner (conn->daemon,
      dbus_g_method_get_sender (context),
      sender_name_owner_changed, conn, NULL);

done:
  gabble_svc_connection_interface_mail_notification_return_from_subscribe (context);
}

static void
gabble_mail_notification_unsubscribe (GabbleSvcConnectionInterfaceMailNotification *iface,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  const gchar *sender =  dbus_g_method_get_sender (context);

  DEBUG ("Unsubscribe called by: %s", sender);

  if (!(conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY)
      || !conn->daemon)
    {
      tp_dbus_g_method_return_not_implemented (context);
      return;
    }

  if (!g_datalist_get_data (&conn->mail_subscribers, sender))
    {
      DEBUG ("Sender '%s' is not subscribed!", sender);
      goto done;
    }

  unsubscribe (conn, sender);

done:
  gabble_svc_connection_interface_mail_notification_return_from_unsubscribe (context);
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

  conn->mail_subscribers_count = 0;
  conn->mail_subscribers = NULL;
  conn->inbox_url = NULL;
  conn->unread_mails = NULL;
}

static void
foreach_cancel_watch (GQuark key_id,
    gpointer handle_set,
    gpointer user_data)
{
  GabbleConnection *conn = user_data;

  tp_dbus_daemon_cancel_name_owner_watch (conn->daemon,
      g_quark_to_string (key_id), sender_name_owner_changed, conn);
}

void
conn_mail_notif_dispose (GabbleConnection *conn)
{
  if (conn->daemon)
    {
      conn->mail_subscribers_count = 0;
      g_datalist_clear (&conn->mail_subscribers);
      g_datalist_foreach (&conn->mail_subscribers, foreach_cancel_watch, conn);
      g_object_unref (conn->daemon);
      conn->daemon = NULL;
    }

  g_free (conn->inbox_url);
  conn->inbox_url = NULL;
  if (conn->unread_mails)
    g_hash_table_unref (conn->unread_mails);
  conn->unread_mails = NULL;
}


void
conn_mail_notif_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcConnectionInterfaceMailNotificationClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_connection_interface_mail_notification_implement_##x (\
    klass, gabble_mail_notification_##x)
  IMPLEMENT(subscribe);
  IMPLEMENT(unsubscribe);
#undef IMPLEMENT
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
    g_value_set_static_boxed (value, &empty_array); /* TODO */
  else if (name == prop_quarks[PROP_UNREAD_MAILS])
    g_value_set_boxed (value, &empty_array); /* TODO */
  else
    g_assert (!"Unkown mail notification property, please file a bug.");
}
