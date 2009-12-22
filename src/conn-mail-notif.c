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

static void
gabble_mail_notification_subscribe (GabbleSvcConnectionInterfaceMailNotification *self,
    DBusGMethodInvocation *context)
{
  /* TODO */
}

static void
gabble_mail_notification_unsubscribe (GabbleSvcConnectionInterfaceMailNotification *self,
    DBusGMethodInvocation *context)
{
  /* TODO */
}

void
conn_mail_notif_init (GabbleConnection *conn)
{
  /* TODO */
}


void
conn_mail_notif_dispose (GabbleConnection *conn)
{
  /* TODO */
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
    g_value_set_uint (value, 0);
  else if (name == prop_quarks[PROP_INBOX_URL])
    g_value_set_string (value, "");
  else if (name == prop_quarks[PROP_METHOD])
    g_value_set_uint (value, GABBLE_HTTP_METHOD_GET);
  else if (name == prop_quarks[PROP_POST_DATA])
    g_value_set_static_boxed (value, &empty_array);
  else if (name == prop_quarks[PROP_UNREAD_MAILS])
    g_value_set_boxed (value, &empty_array);
  else
    g_assert (!"Unkown mail notification property, please file a bug.");
}
