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
#include "gabble/caps-channel-manager.h"
#include "connection.h"
#include "server-tls-channel.h"
#include "util.h"

#include "extensions/extensions.h"

#include <wocky/wocky-utils.h>

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleServerTLSManager, gabble_server_tls_manager,
    WOCKY_TYPE_TLS_HANDLER,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      NULL));

enum {
  PROP_CONNECTION = 1,
  PROP_INTERACTIVE_TLS,
  NUM_PROPERTIES
};

struct _GabbleServerTLSManagerPrivate {
  GabbleConnection *connection;
  GabbleServerTLSChannel *channel;

  gchar *peername;
  GStrv reference_identities;
  WockyTLSSession *tls_session;

  GSimpleAsyncResult *async_result;
  GAsyncReadyCallback async_callback;
  gpointer async_data;

  gboolean verify_async_called;
  gboolean tls_state_changed;
  gboolean interactive_tls;

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
    case PROP_INTERACTIVE_TLS:
      g_value_set_boolean (value, self->priv->interactive_tls);
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
    case PROP_INTERACTIVE_TLS:
      self->priv->interactive_tls = g_value_get_boolean (value);
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
        tp_base_channel_close (TP_BASE_CHANNEL (self->priv->channel));

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
            self->priv->peername, self->priv->reference_identities,
            self->priv->async_callback, self->priv->async_data);
    }

  tp_clear_object (&self->priv->async_result);

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
    GPtrArray *rejections,
    gpointer user_data)
{
  GError *error = NULL;
  GabbleServerTLSManager *self = user_data;

  DEBUG ("TLS certificate rejected with rejections %p, length %u.",
      rejections, rejections->len);

  self->priv->tls_state_changed = TRUE;
  g_set_error (&error, GABBLE_SERVER_TLS_ERROR, 0,
      "TLS certificate rejected");
  g_simple_async_result_set_from_error (self->priv->async_result, error);

  g_simple_async_result_complete_in_idle (self->priv->async_result);
  tp_clear_object (&self->priv->async_result);
  g_clear_error (&error);
}

static void
extend_string_ptr_array (GPtrArray *array,
    GStrv new_elements)
{
  gint i;

  if (new_elements != NULL)
    {
      for (i = 0; new_elements[i] != NULL; i++)
        {
          if (!tp_str_empty (new_elements[i]))
            g_ptr_array_add (array, g_strdup (new_elements[i]));
        }
    }
}

static void
fill_reference_identities (GabbleServerTLSManager *self,
    const gchar *peername,
    GStrv original_extra_identities)
{
  GPtrArray *identities;
  gchar *connect_server = NULL;
  gchar *explicit_server = NULL;
  GStrv extra_certificate_identities = NULL;

  g_return_if_fail (self->priv->reference_identities == NULL);

  g_object_get (self->priv->connection,
      "connect-server", &connect_server,
      "explicit-server", &explicit_server,
      "extra-certificate-identities", &extra_certificate_identities,
      NULL);

  identities = g_ptr_array_new ();

  /* The peer name, i.e, the domain part of the JID */
  g_ptr_array_add (identities, g_strdup (peername));

  /* The extra identities that the caller of verify_async() passed */
  extend_string_ptr_array (identities, original_extra_identities);

  /* The explicitly overridden server (if in use) */
  if (!tp_str_empty (explicit_server) &&
      !tp_strdiff (connect_server, explicit_server))
    {
      g_ptr_array_add (identities, g_strdup (explicit_server));
    }

  /* Extra identities added to the account as a result of user choices */
  extend_string_ptr_array (identities, extra_certificate_identities);

  /* Null terminate, since this is a gchar** */
  g_ptr_array_add (identities, NULL);

  self->priv->reference_identities = (GStrv) g_ptr_array_free (identities,
      FALSE);

  g_strfreev (extra_certificate_identities);
  g_free (explicit_server);
  g_free (connect_server);
}

static void
gabble_server_tls_manager_verify_async (WockyTLSHandler *handler,
    WockyTLSSession *tls_session,
    const gchar *peername,
    GStrv extra_identities,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (handler);
  GabbleTLSCertificate *certificate;
  GSimpleAsyncResult *result;

  /* this should be called only once per-connection. */
  g_return_if_fail (!self->priv->verify_async_called);

  DEBUG ("verify_async() called on the GabbleServerTLSManager.");

  self->priv->verify_async_called = TRUE;

  result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, gabble_server_tls_manager_verify_async);

  if (self->priv->connection == NULL)
    {
      DEBUG ("connection already went away; failing immediately");
      g_simple_async_result_set_error (result, TP_ERRORS, TP_ERROR_CANCELLED,
          "The Telepathy connection has already been disconnected");
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
      return;
    }

  fill_reference_identities (self, peername, extra_identities);

  if (!self->priv->interactive_tls)
    {
      DEBUG ("ignore-ssl-errors is set, fallback to non-interactive "
          "verification.");

      g_object_unref (result);

      WOCKY_TLS_HANDLER_CLASS
        (gabble_server_tls_manager_parent_class)->verify_async_func (
            WOCKY_TLS_HANDLER (self), tls_session, peername,
            self->priv->reference_identities, callback, user_data);

      return;
    }

  self->priv->async_result = result;
  self->priv->tls_session = g_object_ref (tls_session);
  self->priv->peername = g_strdup (peername);
  self->priv->async_callback = callback;
  self->priv->async_data = user_data;

  self->priv->channel = g_object_new (GABBLE_TYPE_SERVER_TLS_CHANNEL,
      "connection", self->priv->connection,
      "tls-session", tls_session,
      "hostname", peername,
      "reference-identities", self->priv->reference_identities,
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
}

static gboolean
gabble_server_tls_manager_verify_finish (WockyTLSHandler *self,
    GAsyncResult *result,
    GError **error)
{
  if (G_IS_SIMPLE_ASYNC_RESULT (result) &&
      g_simple_async_result_get_source_tag ((GSimpleAsyncResult *) result) ==
        gabble_server_tls_manager_verify_async)
    {
      wocky_implement_finish_void (self,
          gabble_server_tls_manager_verify_async);
    }
  else
    {
      return WOCKY_TLS_HANDLER_CLASS
        (gabble_server_tls_manager_parent_class)->verify_finish_func (self,
            result, error);
    }
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

  DEBUG ("%p", self);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_clear_object (&self->priv->tls_session);
  tp_clear_object (&self->priv->connection);

  G_OBJECT_CLASS (gabble_server_tls_manager_parent_class)->dispose (object);
}

static void
gabble_server_tls_manager_finalize (GObject *object)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (object);

  DEBUG ("%p", self);

  if (self->priv->channel != NULL)
    tp_base_channel_close (TP_BASE_CHANNEL (self->priv->channel));

  g_free (self->priv->peername);
  g_strfreev (self->priv->reference_identities);

  G_OBJECT_CLASS (gabble_server_tls_manager_parent_class)->finalize (object);
}

static void
gabble_server_tls_manager_constructed (GObject *object)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_server_tls_manager_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  DEBUG ("Server TLS Manager constructed");

  gabble_signal_connect_weak (self->priv->connection, "status-changed",
      G_CALLBACK (connection_status_changed_cb), object);
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
  hclass->verify_finish_func = gabble_server_tls_manager_verify_finish;

  pspec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this manager.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CONNECTION, pspec);

  pspec = g_param_spec_boolean ("interactive-tls", "Interactive TLS setting",
      "Whether interactive TLS certificate verification is enabled.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_INTERACTIVE_TLS, pspec);
}

static void
gabble_server_tls_manager_foreach_channel (TpChannelManager *manager,
    TpExportableChannelFunc func,
    gpointer user_data)
{
  GabbleServerTLSManager *self = GABBLE_SERVER_TLS_MANAGER (manager);

  DEBUG ("Foreach channel");

  if (self->priv->channel == NULL)
    return;

  /* there's only one channel of this kind */
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
cert_reject_reason_to_conn_reason (TpTLSCertificateRejectReason tls_reason)
{
  #define EASY_CASE(x) \
    case TP_TLS_CERTIFICATE_REJECT_REASON_ ## x: \
      return TP_CONNECTION_STATUS_REASON_CERT_ ## x;

  switch (tls_reason)
    {
      EASY_CASE (UNTRUSTED);
      EASY_CASE (EXPIRED);
      EASY_CASE (NOT_ACTIVATED);
      EASY_CASE (FINGERPRINT_MISMATCH);
      EASY_CASE (HOSTNAME_MISMATCH);
      EASY_CASE (SELF_SIGNED);
      EASY_CASE (REVOKED);
      EASY_CASE (INSECURE);
      EASY_CASE (LIMIT_EXCEEDED);

      case TP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN:
      default:
        return TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
    }

  #undef EASY_CASE
}

void
gabble_server_tls_manager_get_rejection_details (GabbleServerTLSManager *self,
    gchar **dbus_error,
    GHashTable **details,
    TpConnectionStatusReason *reason)
{
  GabbleTLSCertificate *certificate;
  GPtrArray *rejections;
  GValueArray *rejection;
  TpTLSCertificateRejectReason tls_reason;

  certificate = gabble_server_tls_channel_get_certificate
    (self->priv->channel);

  g_object_get (certificate,
      "rejections", &rejections,
      NULL);

  /* we return 'Invalid_Argument' if Reject() is called with zero
   * reasons, so if this fails something bad happened.
   */
  g_assert (rejections->len >= 1);

  rejection = g_ptr_array_index (rejections, 0);

  tls_reason = g_value_get_uint (g_value_array_get_nth (rejection, 0));
  *dbus_error = g_value_dup_string (g_value_array_get_nth (rejection, 1));
  *details = g_value_dup_boxed (g_value_array_get_nth (rejection, 2));

  *reason = cert_reject_reason_to_conn_reason (tls_reason);

  tp_clear_boxed (TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      &rejections);
}
