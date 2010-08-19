/*
 * server-tls-manager.c - Source for GabbleServerTLSManager
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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
#include "server-tls-manager.h"

#define DEBUG_FLAG GABBLE_DEBUG_TLS
#include "debug.h"
#include "caps-channel-manager.h"
#include "connection.h"
#include "server-tls-channel.h"
#include "util.h"

#include "extensions/extensions.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleServerTLSManager, gabble_server_tls_manager,
    WOCKY_TYPE_TLS_HANDLER,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      NULL));

enum {
  PROP_CONNECTION = 1,
  LAST_PROPERTY,
};

struct _GabbleServerTLSManagerPrivate {
  GabbleConnection *connection;
  GabbleServerTLSChannel *channel;

  gchar *peername;
  WockyTLSSession *tls_session;

  GSimpleAsyncResult *async_result;
  GAsyncReadyCallback async_callback;
  gpointer async_data;

  gboolean verify_async_called;
  gboolean tls_state_changed;

  gboolean dispose_has_run;
};

static void
gabble_server_tls_manager_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->connection);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gabble_server_tls_manager_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      self->priv->connection = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
connection_status_changed_cb (GabbleConnection *conn,
    guint status,
    guint reason,
    gpointer user_data)
{
  GabbleServerTLSManager *self = user_data;

  DEBUG ("Connection status changed, now %d", status);

  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      if (self->priv->channel != NULL)
        gabble_server_tls_channel_close (self->priv->channel);

      tp_clear_object (&self->priv->connection);
    }
}

static void
server_tls_channel_closed_cb (GabbleServerTLSChannel *channel,
    gpointer user_data)
{
  GabbleServerTLSManager *self = user_data;

  DEBUG ("Server TLS channel closed.");

  if (!self->priv->tls_state_changed)
    {
      /* fallback to the old-style non interactive TLS verification */
      DEBUG ("Channel closed, but unhandled, falling back...");

      WOCKY_TLS_HANDLER_CLASS
        (gabble_server_tls_manager_parent_class)->verify_async_func (
            WOCKY_TLS_HANDLER (self), self->priv->tls_session,
            self->priv->peername, self->priv->async_callback,
            self->priv->async_data);
    }

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (channel));

  tp_clear_object (&self->priv->channel);
}

GQuark
gabble_server_tls_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("server-tls-error");

  return quark;
}

static void
tls_certificate_accepted_cb (GabbleTLSCertificate *certificate,
    gpointer user_data)
{
  GabbleServerTLSManager *self = user_data;

  DEBUG ("TLS certificate accepted");

  self->priv->tls_state_changed = TRUE;

  g_simple_async_result_complete_in_idle (self->priv->async_result);
  tp_clear_object (&self->priv->async_result);
}

static void
tls_certificate_rejected_cb (GabbleTLSCertificate *certificate,
    GabbleTLSCertificateRejectReason reason,
    const gchar *dbus_error,
    GHashTable *details,
    gpointer user_data)
{
  GError *error = NULL;
  GabbleServerTLSManager *self = user_data;

  DEBUG ("TLS certificate rejected with reason %u, dbus error %s "
      "and details map %p.", reason, dbus_error, details);

  self->priv->tls_state_changed = TRUE;
  g_set_error (&error, GABBLE_SERVER_TLS_ERROR, reason,
      "TLS certificate rejected with reason %u", reason);
  g_simple_async_result_set_from_error (self->priv->async_result, error);

  g_simple_async_result_complete_in_idle (self->priv->async_result);
  tp_clear_object (&self->priv->async_result);
  g_clear_error (&error);
}

static void
gabble_server_tls_manager_verify_async (WockyTLSHandler *handler,
    WockyTLSSession *tls_session,
    const gchar *peername,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (handler);
  gchar *object_path;
  GabbleTLSCertificate *certificate;

  /* this should be called only once per-connection. */
  g_return_if_fail (!self->priv->verify_async_called);

  DEBUG ("verify_async() called on the GabbleServerTLSManager.");

  self->priv->verify_async_called = TRUE;
  self->priv->async_result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, wocky_tls_handler_verify_finish);
  self->priv->tls_session = g_object_ref (tls_session);
  self->priv->peername = g_strdup (peername);
  self->priv->async_callback = callback;
  self->priv->async_data = user_data;

  object_path = g_strdup_printf ("%s/ServerTLSChannel",
      ((TpBaseConnection *) self->priv->connection)->object_path);
  self->priv->channel = g_object_new (GABBLE_TYPE_SERVER_TLS_CHANNEL,
      "connection", self->priv->connection,
      "object-path", object_path,
      "tls-session", tls_session,
      "hostname", peername,
      NULL);

  g_signal_connect (self->priv->channel, "closed",
      G_CALLBACK (server_tls_channel_closed_cb), self);

  certificate = gabble_server_tls_channel_get_certificate
    (self->priv->channel);

  g_signal_connect (certificate, "accepted",
      G_CALLBACK (tls_certificate_accepted_cb), self);
  g_signal_connect (certificate, "rejected",
      G_CALLBACK (tls_certificate_rejected_cb), self);

  /* emit NewChannel on the ChannelManager iface */
  tp_channel_manager_emit_new_channel (self,
      (TpExportableChannel *) self->priv->channel, NULL);

  g_free (object_path);
}

static void
gabble_server_tls_manager_init (GabbleServerTLSManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_SERVER_TLS_MANAGER, GabbleServerTLSManagerPrivate);
}

static void
gabble_server_tls_manager_dispose (GObject *object)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (object);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_clear_object (&self->priv->connection);

  G_OBJECT_CLASS (gabble_server_tls_manager_parent_class)->dispose (object);
}

static void
gabble_server_tls_manager_finalize (GObject *object)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (object);

  if (self->priv->channel != NULL)
    gabble_server_tls_channel_close (self->priv->channel);

  G_OBJECT_CLASS (gabble_server_tls_manager_parent_class)->finalize (object);
}

static void
gabble_server_tls_manager_constructed (GObject *object)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (object);

  DEBUG ("Server TLS Manager constructed");

  gabble_signal_connect_weak (self->priv->connection, "status-changed",
      G_CALLBACK (connection_status_changed_cb), object);

  if (G_OBJECT_CLASS
      (gabble_server_tls_manager_parent_class)->constructed != NULL)
    {
      G_OBJECT_CLASS
        (gabble_server_tls_manager_parent_class)->constructed (object);
    }
}

static void
gabble_server_tls_manager_class_init (GabbleServerTLSManagerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  WockyTLSHandlerClass *hclass = WOCKY_TLS_HANDLER_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (GabbleServerTLSManagerPrivate));

  oclass->dispose = gabble_server_tls_manager_dispose;
  oclass->finalize = gabble_server_tls_manager_finalize;
  oclass->constructed = gabble_server_tls_manager_constructed;
  oclass->set_property = gabble_server_tls_manager_set_property;
  oclass->get_property = gabble_server_tls_manager_get_property;

  hclass->verify_async_func = gabble_server_tls_manager_verify_async;

  pspec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this manager.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CONNECTION, pspec);
}

static void
gabble_server_tls_manager_foreach_channel (TpChannelManager *manager,
    TpExportableChannelFunc func,
    gpointer user_data)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (manager);
  gboolean closed;

  DEBUG ("Foreach channel");

  if (self->priv->channel == NULL)
    return;

  /* there's only one channel of this kind */
  g_object_get (self->priv->channel, "channel-destroyed", &closed, NULL);

  if (!closed)
    func (TP_EXPORTABLE_CHANNEL (self->priv->channel), user_data);
}

static void
channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_server_tls_manager_foreach_channel;

  /* these channels are not requestable. */
  iface->ensure_channel = NULL;
  iface->create_channel = NULL;
  iface->request_channel = NULL;
  iface->foreach_channel_class = NULL;
}

static TpConnectionStatusReason
cert_reject_reason_to_conn_reason (GabbleTLSCertificateRejectReason tls_reason)
{
  TpConnectionStatusReason retval;

  switch (tls_reason)
    {
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_UNTRUSTED:
      retval = TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_EXPIRED:
      retval = TP_CONNECTION_STATUS_REASON_CERT_EXPIRED;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_NOT_ACTIVATED:
      retval = TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_FINGERPRINT_MISMATCH:
      retval = TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_HOSTNAME_MISMATCH:
      retval = TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_SELF_SIGNED:
      retval = TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_REVOKED:
      retval = TP_CONNECTION_STATUS_REASON_CERT_REVOKED;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_INSECURE:
      retval = TP_CONNECTION_STATUS_REASON_CERT_INSECURE;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_LIMIT_EXCEEDED:
      retval = TP_CONNECTION_STATUS_REASON_CERT_LIMIT_EXCEEDED;
      break;
    case GABBLE_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN:
    default:
      retval = TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
      break;
    }

  return retval;
}

void
gabble_server_tls_manager_get_rejection_details (GabbleServerTLSManager *self,
    gchar **dbus_error,
    GHashTable **details,
    TpConnectionStatusReason *reason)
{
  GabbleTLSCertificate *certificate;
  GabbleTLSCertificateRejectReason tls_reason;

  certificate = gabble_server_tls_channel_get_certificate
    (self->priv->channel);

  g_object_get (certificate,
      "reject-reason", &tls_reason,
      "reject-error", dbus_error,
      "reject-details", details,
      NULL);

  *reason = cert_reject_reason_to_conn_reason (tls_reason);
}
