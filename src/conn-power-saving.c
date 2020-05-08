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

#include "config.h"
#include "conn-power-saving.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION
#include "debug.h"
#include "namespaces.h"
#include "util.h"
#include "conn-util.h"

#include <wocky/wocky.h>

enum
{
  PROP_POWER_SAVING_ACTIVE,
  NUM_OF_PROP,
};

typedef struct {
  DBusGMethodInvocation *dbus_context;
  GabbleConnection *conn;
  gboolean enabling;
} ConnPowerSaveContext;

static void
google_queueing_send_command (
    GabbleConnection *conn,
    const gchar *command,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockyStanza *stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
      '(', "query",
        ':', NS_GOOGLE_QUEUE,
        '(', command, ')',
      ')',
      NULL);

  conn_util_send_iq_async (conn, stanza, NULL, callback, user_data);

  g_object_unref (stanza);
}

static void
client_state_indication_send (
    GabbleConnection *conn,
    const gchar *state,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockyStanza *stanza = wocky_stanza_new (state, NS_CSI);
  WockyPorter *porter = wocky_session_get_porter (conn->session);

  wocky_porter_send_async (porter, stanza, NULL, callback, user_data);

  g_object_unref (stanza);
}

static void
maybe_emit_power_saving_changed (GabbleConnection *self,
    gboolean enabling)
{
  gboolean enabled;

  g_object_get (self, "power-saving", &enabled, NULL);

  if (enabling != enabled)
    {
      g_object_set (self, "power-saving", enabling, NULL);
      tp_svc_connection_interface_power_saving_emit_power_saving_changed (
          self, enabling);
    }
}

static void
toggle_client_state_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source_object);
  ConnPowerSaveContext *cps_context = (ConnPowerSaveContext *) user_data;
  GError *error = NULL;
  gboolean enabling;

  enabling = cps_context->enabling;

  if (!wocky_porter_send_finish (porter, res, &error))
    {
      DEBUG ("Failed to indicate %sactive state: %s",
          enabling ? "in" : "", error->message);

      enabling = FALSE;

      dbus_g_method_return_error (cps_context->dbus_context, error);

      g_error_free (error);
    }
  else
    {
      DEBUG ("Indicated %sactive Client State", enabling ? "in" : "");

      tp_svc_connection_interface_power_saving_return_from_set_power_saving (
          cps_context->dbus_context);
    }

  maybe_emit_power_saving_changed (cps_context->conn, enabling);

  g_slice_free (ConnPowerSaveContext, cps_context);
}

static void
toggle_google_queueing_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  ConnPowerSaveContext *queueing_context = (ConnPowerSaveContext *) user_data;
  GError *error = NULL;
  gboolean enabling;

  enabling = queueing_context->enabling;

  if (!conn_util_send_iq_finish (self, res, NULL, &error))
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

      tp_svc_connection_interface_power_saving_return_from_set_power_saving (
          queueing_context->dbus_context);

      if (!enabling)
        google_queueing_send_command (self, "flush", NULL, NULL);
    }

  maybe_emit_power_saving_changed (self, enabling);

  g_slice_free (ConnPowerSaveContext, queueing_context);
}

static void
conn_power_saving_set_power_saving (
    TpSvcConnectionInterfacePowerSaving *conn,
    gboolean enable,
    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (conn);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  gboolean enabled;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  g_object_get (G_OBJECT (self), "power-saving", &enabled, NULL);

  if (enable == enabled)
    {
      /* no-op */
      tp_svc_connection_interface_power_saving_return_from_set_power_saving (
          context);
      return;
    }


  /* google:queue is loosely described here:
   * <http://mail.jabber.org/pipermail/summit/2010-February/000528.html>. Since
   * April 2011, it is advertised as a stream feature by the Google Talk
   * server; the development version of M-Link, and possibly other servers,
   * also implement the protocol and advertise this stream feature. */
  /* XEP-0352 is neutral on server implementation, so implementations _may_ be
   * identical to google:queue */
  if (self->features & GABBLE_CONNECTION_FEATURES_GOOGLE_QUEUE ||
      self->features & GABBLE_CONNECTION_FEATURES_CSI)
    {
      ConnPowerSaveContext *cps_context;
      cps_context = g_slice_new0 (ConnPowerSaveContext);
      cps_context->enabling = enable;
      cps_context->dbus_context = context;
      cps_context->conn = self;

      if (self->features & GABBLE_CONNECTION_FEATURES_CSI)
        {
          DEBUG ("Indicating %sactive Client State", enable ? "in" : "");

          client_state_indication_send (self, enable ? "inactive" : "active",
            toggle_client_state_cb, cps_context);
        }
      else
        {
          DEBUG ("%sabling presence queueing", enable ? "en" : "dis");

          google_queueing_send_command (self, enable ? "enable" : "disable",
            toggle_google_queueing_cb, cps_context);
        }
    }
  else
    {
      /* If the server doesn't support any method of queueing, we can still
       * do it locally by enabling power save mode on Wocky. */
      WockyPorter *porter = gabble_connection_dup_porter (self);

      wocky_c2s_porter_enable_power_saving_mode (WOCKY_C2S_PORTER (porter), enable);
      DEBUG ("%sabled local stanza queueing", enable ? "En" : "Dis");
      g_object_unref (porter);
      maybe_emit_power_saving_changed (self, enable);

      tp_svc_connection_interface_power_saving_return_from_set_power_saving (
          context);
    }
}

void
conn_power_saving_iface_init (gpointer g_iface,
    gpointer iface_data)
{
#define IMPLEMENT(x) \
  tp_svc_connection_interface_power_saving_implement_##x (\
  g_iface, conn_power_saving_##x)
  IMPLEMENT (set_power_saving);
#undef IMPLEMENT
}
