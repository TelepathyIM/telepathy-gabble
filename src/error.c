/*
 * gabble-error.c - Source for Gabble's error handling API
 * Copyright (C) 2006-2007 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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
#include "error.h"

#include <stdio.h>
#include <stdlib.h>

#include "namespaces.h"
#include "util.h"

#include <wocky/wocky-auth-registry.h>
#include <wocky/wocky-auth-registry-enumtypes.h>
#include <wocky/wocky-connector.h>
#include <wocky/wocky-connector-enumtypes.h>
#include <wocky/wocky-tls.h>
#include <wocky/wocky-tls-enumtypes.h>
#include <wocky/wocky-xmpp-error.h>
#include <wocky/wocky-xmpp-error-enumtypes.h>

static inline TpError
set_conn_reason (TpConnectionStatusReason *p,
    TpConnectionStatusReason r,
    TpError e)
{
  if (p != NULL)
    *p = r;

  return e;
}

#define set_easy_conn_reason(p, suffix) \
  set_conn_reason (p, TP_CONNECTION_STATUS_REASON_ ## suffix, \
      TP_ERROR_ ## suffix)

static TpError
map_wocky_xmpp_error (const GError *error,
    TpConnectionStatusReason *conn_reason)
{
  g_return_val_if_fail (error->domain == WOCKY_XMPP_ERROR,
      TP_ERROR_NOT_AVAILABLE);

  switch (error->code)
    {
    case WOCKY_XMPP_ERROR_REDIRECT:
    case WOCKY_XMPP_ERROR_GONE:
      /* FIXME: wild guess at the right error */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED, TP_ERROR_DOES_NOT_EXIST);

    case WOCKY_XMPP_ERROR_BAD_REQUEST:
    case WOCKY_XMPP_ERROR_UNEXPECTED_REQUEST:
      /* probably an internal error in Gabble/Wocky */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR, TP_ERROR_CONFUSED);

    case WOCKY_XMPP_ERROR_JID_MALFORMED:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR, TP_ERROR_INVALID_HANDLE);

    case WOCKY_XMPP_ERROR_NOT_AUTHORIZED:
    case WOCKY_XMPP_ERROR_PAYMENT_REQUIRED:
    case WOCKY_XMPP_ERROR_FORBIDDEN:
      /* FIXME: the closest we've got for these, I think? */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED,
          TP_ERROR_PERMISSION_DENIED);

    case WOCKY_XMPP_ERROR_ITEM_NOT_FOUND:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED, TP_ERROR_DOES_NOT_EXIST);

    case WOCKY_XMPP_ERROR_RECIPIENT_UNAVAILABLE:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED, TP_ERROR_OFFLINE);

    case WOCKY_XMPP_ERROR_REMOTE_SERVER_NOT_FOUND:
      /* FIXME: or NetworkError? */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED, TP_ERROR_DOES_NOT_EXIST);

    case WOCKY_XMPP_ERROR_NOT_ALLOWED:
    case WOCKY_XMPP_ERROR_NOT_ACCEPTABLE:
    case WOCKY_XMPP_ERROR_REGISTRATION_REQUIRED:
    case WOCKY_XMPP_ERROR_SUBSCRIPTION_REQUIRED:
      /* FIXME: the closest we've got for all these, I think? */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED,
          TP_ERROR_PERMISSION_DENIED);

    case WOCKY_XMPP_ERROR_REMOTE_SERVER_TIMEOUT:
      return set_easy_conn_reason (conn_reason, NETWORK_ERROR);

    case WOCKY_XMPP_ERROR_CONFLICT:
      /* this is the best we can do in general - callers should
       * special-case <conflict/> according to their domain knowledge,
       * to turn it into RegistrationExists, ConnectionReplaced, etc. */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NAME_IN_USE,
          TP_ERROR_NOT_AVAILABLE);

    case WOCKY_XMPP_ERROR_INTERNAL_SERVER_ERROR:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
          TP_ERROR_SERVICE_CONFUSED);

    case WOCKY_XMPP_ERROR_RESOURCE_CONSTRAINT:
      /* FIXME: Telepathy's ServiceBusy means the server, but the remote
       * client can also raise <resource-constraint/> */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
          TP_ERROR_SERVICE_BUSY);

    case WOCKY_XMPP_ERROR_FEATURE_NOT_IMPLEMENTED:
    case WOCKY_XMPP_ERROR_SERVICE_UNAVAILABLE:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
          TP_ERROR_NOT_AVAILABLE);

    case WOCKY_XMPP_ERROR_UNDEFINED_CONDITION:
    default:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
          TP_ERROR_NOT_AVAILABLE);
    }
}

static TpError
map_wocky_auth_error (const GError *error,
    TpConnectionStatusReason *conn_reason)
{
  g_return_val_if_fail (error->domain == WOCKY_AUTH_ERROR,
      TP_ERROR_NOT_AVAILABLE);

  switch (error->code)
    {
    case WOCKY_AUTH_ERROR_CONNRESET:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR,
          TP_ERROR_CONNECTION_LOST);

    case WOCKY_AUTH_ERROR_NETWORK:
    case WOCKY_AUTH_ERROR_STREAM:
      return set_easy_conn_reason (conn_reason, NETWORK_ERROR);

    case WOCKY_AUTH_ERROR_RESOURCE_CONFLICT:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NAME_IN_USE,
          TP_ERROR_ALREADY_CONNECTED);

    case WOCKY_AUTH_ERROR_NOT_SUPPORTED:
    case WOCKY_AUTH_ERROR_NO_SUPPORTED_MECHANISMS:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED,
          TP_ERROR_NOT_IMPLEMENTED);

    case WOCKY_AUTH_ERROR_INVALID_REPLY:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
          TP_ERROR_SERVICE_CONFUSED);

    case WOCKY_AUTH_ERROR_INIT_FAILED:
    case WOCKY_AUTH_ERROR_NO_CREDENTIALS:
    case WOCKY_AUTH_ERROR_NOT_AUTHORIZED:
    case WOCKY_AUTH_ERROR_FAILURE:
    default:
      return set_easy_conn_reason (conn_reason, AUTHENTICATION_FAILED);
    }
}

static TpError
map_wocky_connector_error (const GError *error,
    TpConnectionStatusReason *conn_reason)
{
  g_return_val_if_fail (error->domain == WOCKY_CONNECTOR_ERROR,
      TP_ERROR_NOT_AVAILABLE);

  switch (error->code)
    {
    case WOCKY_CONNECTOR_ERROR_SESSION_DENIED:
      return set_easy_conn_reason (conn_reason, AUTHENTICATION_FAILED);

    case WOCKY_CONNECTOR_ERROR_BIND_CONFLICT:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NAME_IN_USE,
          TP_ERROR_ALREADY_CONNECTED);

    case WOCKY_CONNECTOR_ERROR_REGISTRATION_CONFLICT:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_NAME_IN_USE,
          TP_ERROR_REGISTRATION_EXISTS);

    case WOCKY_CONNECTOR_ERROR_REGISTRATION_REJECTED:
      /* AuthenticationFailed is the closest ConnectionStatusReason to
       * "I tried but couldn't register you an account." */
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED,
          TP_ERROR_PERMISSION_DENIED);

    case WOCKY_CONNECTOR_ERROR_REGISTRATION_UNSUPPORTED:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED,
          TP_ERROR_NOT_AVAILABLE);

    default:
      return set_easy_conn_reason (conn_reason, NETWORK_ERROR);
    }
}

static TpError
map_wocky_stream_error (const GError *error,
    TpConnectionStatus previous_status,
    TpConnectionStatusReason *conn_reason)
{
  g_return_val_if_fail (error->domain == WOCKY_XMPP_STREAM_ERROR,
      TP_ERROR_NOT_AVAILABLE);

  switch (error->code)
    {
    case WOCKY_XMPP_STREAM_ERROR_HOST_UNKNOWN:
      /* If we get this while we're logging in, it's because we're trying
       * to connect to foo@bar.com but the server doesn't know about
       * bar.com, probably because the user entered a non-GTalk JID into
       * a GTalk profile that forces the server. */
      return set_easy_conn_reason (conn_reason, AUTHENTICATION_FAILED);

    case WOCKY_XMPP_STREAM_ERROR_CONFLICT:
      if (previous_status == TP_CONNECTION_STATUS_CONNECTED)
        {
          return set_conn_reason (conn_reason,
              TP_CONNECTION_STATUS_REASON_NAME_IN_USE,
              TP_ERROR_CONNECTION_REPLACED);
        }
      else
        {
          return set_conn_reason (conn_reason,
              TP_CONNECTION_STATUS_REASON_NAME_IN_USE,
              TP_ERROR_ALREADY_CONNECTED);
        }

    default:
      return set_easy_conn_reason (conn_reason, NETWORK_ERROR);
    }
}

static TpError
map_wocky_tls_cert_error (const GError *error,
    TpConnectionStatusReason *conn_reason)
{
  g_return_val_if_fail (error->domain == WOCKY_TLS_CERT_ERROR,
      TP_ERROR_NOT_AVAILABLE);

  switch (error->code)
    {
    case WOCKY_TLS_CERT_NO_CERTIFICATE:
      return set_easy_conn_reason (conn_reason, CERT_NOT_PROVIDED);

    case WOCKY_TLS_CERT_INSECURE:
    case WOCKY_TLS_CERT_SIGNER_UNKNOWN:
    case WOCKY_TLS_CERT_SIGNER_UNAUTHORISED:
    case WOCKY_TLS_CERT_REVOKED:
    case WOCKY_TLS_CERT_MAYBE_DOS:
      return set_easy_conn_reason (conn_reason, CERT_UNTRUSTED);

    case WOCKY_TLS_CERT_EXPIRED:
      return set_easy_conn_reason (conn_reason, CERT_EXPIRED);

    case WOCKY_TLS_CERT_NOT_ACTIVE:
      return set_easy_conn_reason (conn_reason, CERT_NOT_ACTIVATED);

    case WOCKY_TLS_CERT_NAME_MISMATCH:
      return set_easy_conn_reason (conn_reason, CERT_HOSTNAME_MISMATCH);

    case WOCKY_TLS_CERT_INTERNAL_ERROR:
    case WOCKY_TLS_CERT_UNKNOWN_ERROR:
    default:
      return set_conn_reason (conn_reason,
          TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR,
          TP_ERROR_ENCRYPTION_ERROR);
    }
}

static TpError
map_connection_error (const GError *error)
{
  switch (error->code)
    {
      case WOCKY_XMPP_CONNECTION_ERROR_EOS:
      case WOCKY_XMPP_CONNECTION_ERROR_CLOSED:
        return TP_ERROR_CANCELLED;
      case WOCKY_XMPP_CONNECTION_ERROR_NOT_OPEN:
      case WOCKY_XMPP_CONNECTION_ERROR_IS_CLOSED:
      case WOCKY_XMPP_CONNECTION_ERROR_IS_OPEN:
      default:
        return TP_ERROR_DISCONNECTED;
    }
}

static const gchar *
get_error_prefix (GEnumClass *klass,
    gint code,
    const gchar *fallback)
{
  GEnumValue *value;

  if (klass == NULL)
    return fallback;

  value = g_enum_get_value (klass, code);

  if (value == NULL || value->value_name == NULL)
    return fallback;

  return value->value_name;
}

void
gabble_set_tp_conn_error_from_wocky (const GError *wocky_error,
    TpConnectionStatus previous_status,
    TpConnectionStatusReason *conn_reason,
    GError **error)
{
  GEnumClass *klass;
  const gchar *name;

  if (conn_reason != NULL)
    *conn_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;

  g_return_if_fail (wocky_error != NULL);

  if (wocky_error->domain == WOCKY_XMPP_ERROR)
    {
      klass = g_type_class_ref (WOCKY_TYPE_XMPP_ERROR);
      name = get_error_prefix (klass, wocky_error->code,
          "unknown WockyXmppError code");
      g_set_error (error, TP_ERRORS,
          map_wocky_xmpp_error (wocky_error, conn_reason),
          "%s (#%d): %s", name, wocky_error->code, wocky_error->message);
      g_type_class_unref (klass);
    }
  else if (wocky_error->domain == G_IO_ERROR)
    {
      klass = g_type_class_ref (G_TYPE_IO_ERROR_ENUM);
      name = get_error_prefix (klass, wocky_error->code,
          "unknown GIOError code");
      /* FIXME: is it safe to assume that every GIOError we encounter from
       * Wocky is a NetworkError? */
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "%s (#%d): %s", name, wocky_error->code, wocky_error->message);
      g_type_class_unref (klass);

      if (conn_reason != NULL)
        *conn_reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
    }
  else if (wocky_error->domain == WOCKY_AUTH_ERROR)
    {
      klass = g_type_class_ref (WOCKY_TYPE_AUTH_ERROR);
      name = get_error_prefix (klass, wocky_error->code,
          "unknown WockyAuthError code");
      g_set_error (error, TP_ERRORS,
          map_wocky_auth_error (wocky_error, conn_reason),
          "%s (#%d): %s", name, wocky_error->code, wocky_error->message);
      g_type_class_unref (klass);
    }
  else if (wocky_error->domain == WOCKY_CONNECTOR_ERROR)
    {
      klass = g_type_class_ref (WOCKY_TYPE_CONNECTOR_ERROR);
      name = get_error_prefix (klass, wocky_error->code,
          "unknown WockyConnectorError code");
      g_set_error (error, TP_ERRORS,
          map_wocky_connector_error (wocky_error, conn_reason),
          "%s (#%d): %s", name, wocky_error->code, wocky_error->message);
      g_type_class_unref (klass);
    }
  else if (wocky_error->domain == WOCKY_XMPP_STREAM_ERROR)
    {
      klass = g_type_class_ref (WOCKY_TYPE_XMPP_STREAM_ERROR);
      name = get_error_prefix (klass, wocky_error->code,
          "unknown WockyXmppStreamError code");
      g_set_error (error, TP_ERRORS,
          map_wocky_stream_error (wocky_error, previous_status, conn_reason),
          "%s (#%d): %s", name, wocky_error->code, wocky_error->message);
      g_type_class_unref (klass);
    }
  else if (wocky_error->domain == WOCKY_TLS_CERT_ERROR)
    {
      klass = g_type_class_ref (WOCKY_TYPE_TLS_CERT_STATUS);
      name = get_error_prefix (klass, wocky_error->code,
          "unknown WockyTLSCertStatus code");
      g_set_error (error, TP_ERRORS,
          map_wocky_tls_cert_error (wocky_error, conn_reason),
          "%s (#%d): %s", name, wocky_error->code, wocky_error->message);
      g_type_class_unref (klass);
    }
  else if (wocky_error->domain == WOCKY_XMPP_CONNECTION_ERROR)
    {
      /* FIXME: there's no GEnum for WockyXmppConnectionError. */
      g_set_error_literal (error, TP_ERRORS,
          map_connection_error (wocky_error),
          wocky_error->message);
    }
  else
    {
      /* best we can do... */
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s (#%d): %s", g_quark_to_string (wocky_error->domain),
          wocky_error->code, wocky_error->message);
    }
}

void
gabble_set_tp_error_from_wocky (const GError *wocky_error,
    GError **error)
{
  gabble_set_tp_conn_error_from_wocky (wocky_error,
      TP_CONNECTION_STATUS_CONNECTED, NULL, error);
}
