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
conn_mail_notif_class_init (GabbleConnectionClass *klass)
{
  /* TODO */
}


void
conn_mail_notif_init (GabbleConnection *conn)
{
  /* TODO */
}


void
conn_mail_notif_finalize (GabbleConnection *conn)
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
