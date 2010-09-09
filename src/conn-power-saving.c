/*
 * conn-power-saving.c - Header for Gabble connection code handling power saving
 * Copyright (C) 2010 Collabora Ltd.
 * Copyright (C) 2010 Nokia Corporation
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

#include "conn-power-saving.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION
#include "debug.h"
#include "namespaces.h"
#include "util.h"
#include "conn-util.h"

#include "extensions/extensions.h"

enum
{
  PROP_POWER_SAVING_ACTIVE,
  NUM_OF_PROP,
};

typedef struct {
  DBusGMethodInvocation *dbus_context;
  gboolean enabling;
} ToggleQueueingContext;

static void
conn_power_saving_send_command (
    GabbleConnection *conn,
    const gchar *command,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockyStanza *stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, NULL,
      '(', "query",
        ':', NS_GOOGLE_QUEUE,
        '(', command, ')',
      ')',
      NULL);

  conn_util_send_iq_async (conn, stanza, NULL, callback, user_data);

  g_object_unref (stanza);
}

static void
toggle_queueing_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  ToggleQueueingContext *queueing_context = (ToggleQueueingContext *) user_data;
  GError *error = NULL;
  gboolean enabling;
  gboolean enabled;
  WockyStanza *reply;

  g_object_get (source_object, "power-saving", &enabled, NULL);

  reply = conn_util_send_iq_finish_harder (self, res, &error);

  enabling = queueing_context->enabling;

  if (reply == NULL)
    {
      DEBUG ("Failed to %sable queueing: %s",
          enabling ? "en" : "dis", error->message);

      enabling = FALSE;

      dbus_g_method_return_error (queueing_context->dbus_context, error);

      g_error_free (error);
    }
  else
    {
      DEBUG ("%sabled queueing", enabling ? "en" : "dis");

      gabble_svc_connection_interface_power_saving_return_from_set_power_saving (
          queueing_context->dbus_context);

      if (!enabling)
        conn_power_saving_send_command (self, "flush", NULL, NULL);

      g_object_unref (reply);
    }

  if (enabling != enabled)
    {
      g_object_set (source_object, "power-saving", enabling, NULL);
      gabble_svc_connection_interface_power_saving_emit_power_saving_changed (
          self, enabling);
    }

  g_slice_free (ToggleQueueingContext, queueing_context);
}

static void
conn_power_saving_set_power_saving (
    GabbleSvcConnectionInterfacePowerSaving *conn,
    gboolean enable,
    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (conn);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  ToggleQueueingContext *queueing_context;
  gboolean enabled;

  g_object_get (G_OBJECT (self), "power-saving", &enabled, NULL);

  if (base->status != TP_CONNECTION_STATUS_CONNECTED ||
      enable == enabled)
    {
      gabble_svc_connection_interface_power_saving_return_from_set_power_saving (
          context);

      if (enable != enabled)
        {
          gabble_svc_connection_interface_power_saving_emit_power_saving_changed (
              self, enable);

          g_object_set (G_OBJECT (self), "power-saving", enable, NULL);
        }

      return;
    }

  queueing_context = g_slice_new0 (ToggleQueueingContext);
  queueing_context->enabling = enable;
  queueing_context->dbus_context = context;

  DEBUG ("%sabling presence queueing", enable ? "en" : "dis");

  conn_power_saving_send_command (self, enable ? "enable" : "disable",
      toggle_queueing_cb, queueing_context);
}

void
conn_power_saving_iface_init (gpointer g_iface,
    gpointer iface_data)
{
#define IMPLEMENT(x) \
  gabble_svc_connection_interface_power_saving_implement_##x (\
  g_iface, conn_power_saving_##x)
  IMPLEMENT (set_power_saving);
#undef IMPLEMENT
}

static void
conn_power_saving_enable_on_connect_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GError *error = NULL;
  WockyStanza *reply;

  DEBUG (" ");

  reply = conn_util_send_iq_finish_harder (self, res, &error);

  if (reply == NULL)
    {
      DEBUG ("Failed to enter power saving mode when connected: %s",
             error->message);
      g_object_set (source_object, "power-saving", FALSE, NULL);
      g_error_free (error);
      gabble_svc_connection_interface_power_saving_emit_power_saving_changed (
          self, FALSE);
    }
  else
    {
      g_object_unref (reply);
    }
}

static void
conn_power_saving_status_changed_cb (GabbleConnection *self,
    guint status,
    guint reason,
    gpointer user_data)
{
  gboolean enabled;

  g_object_get (G_OBJECT (self), "power-saving", &enabled, NULL);

  if (enabled && status == TP_CONNECTION_STATUS_CONNECTED)
    conn_power_saving_send_command (self, "enable",
        conn_power_saving_enable_on_connect_cb, NULL);
}

void
conn_power_saving_init (GabbleConnection *self)
{
  g_signal_connect (self, "status-changed",
      G_CALLBACK (conn_power_saving_status_changed_cb), NULL);

}
