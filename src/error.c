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

#define MAX_LEGACY_ERRORS 3

typedef struct {
    const gchar *name;
    const gchar *description;
    const gchar *type;
    guint specialises;
    const gchar *namespace;
    const guint16 legacy_errors[MAX_LEGACY_ERRORS];
} XmppErrorSpec;

static const XmppErrorSpec xmpp_errors[NUM_XMPP_ERRORS] =
{
    {
      "undefined-condition",
      "application-specific condition",
      NULL,
      0,
      NS_XMPP_STANZAS,
      { 500, 0, },
    },

    {
      "redirect",
      "the recipient or server is redirecting requests for this information "
      "to another entity",
      "modify",
      0,
      NS_XMPP_STANZAS,
      { 302, 0, },
    },

    {
      "gone",
      "the recipient or server can no longer be contacted at this address",
      "modify",
      0,
      NS_XMPP_STANZAS,
      { 302, 0, },
    },

    {
      "bad-request",
      "the sender has sent XML that is malformed or that cannot be processed",
      "modify",
      0,
      NS_XMPP_STANZAS,
      { 400, 0, },
    },
    {
      "unexpected-request",
      "the recipient or server understood the request but was not expecting "
      "it at this time",
      "wait",
      0,
      NS_XMPP_STANZAS,
      { 400, 0, },
    },
    {
      "jid-malformed",
      "the sending entity has provided or communicated an XMPP address or "
      "aspect thereof (e.g., a resource identifier) that does not adhere "
      "to the syntax defined in Addressing Scheme (Section 3)",
      "modify",
      0,
      NS_XMPP_STANZAS,
      { 400, 0, },
    },

    {
      "not-authorized",
      "the sender must provide proper credentials before being allowed to "
      "perform the action, or has provided improper credentials",
      "auth",
      0,
      NS_XMPP_STANZAS,
      { 401, 0, },
    },

    {
      "payment-required",
      "the requesting entity is not authorized to access the requested "
      "service because payment is required",
      "auth",
      0,
      NS_XMPP_STANZAS,
      { 402, 0, },
    },

    {
      "forbidden",
      "the requesting entity does not possess the required permissions to "
      "perform the action",
      "auth",
      0,
      NS_XMPP_STANZAS,
      { 403, 0, },
    },

    {
      "item-not-found",
      "the addressed JID or item requested cannot be found",
      "cancel",
      0,
      NS_XMPP_STANZAS,
      { 404, 0, },
    },
    {
      "recipient-unavailable",
      "the intended recipient is temporarily unavailable",
      "wait",
      0,
      NS_XMPP_STANZAS,
      { 404, 0, },
    },
    {
      "remote-server-not-found",
      "a remote server or service specified as part or all of the JID of the "
      "intended recipient (or required to fulfill a request) could not be "
      "contacted within a reasonable amount of time",
      "cancel",
      0,
      NS_XMPP_STANZAS,
      { 404, 0, },
    },

    {
      "not-allowed",
      "the recipient or server does not allow any entity to perform the action",
      "cancel",
      0,
      NS_XMPP_STANZAS,
      { 405, 0, },
    },

    {
      "not-acceptable",
      "the recipient or server understands the request but is refusing to "
      "process it because it does not meet criteria defined by the recipient "
      "or server (e.g., a local policy regarding acceptable words in messages)",
      "modify",
      0,
      NS_XMPP_STANZAS,
      { 406, 0, },
    },

    {
      "registration-required",
      "the requesting entity is not authorized to access the requested service "
      "because registration is required",
      "auth",
      0,
      NS_XMPP_STANZAS,
      { 407, 0, },
    },
    {
      "subscription-required",
      "the requesting entity is not authorized to access the requested service "
      "because a subscription is required",
      "auth",
      0,
      NS_XMPP_STANZAS,
      { 407, 0, },
    },

    {
      "remote-server-timeout",
      "a remote server or service specified as part or all of the JID of the "
      "intended recipient (or required to fulfill a request) could not be "
      "contacted within a reasonable amount of time",
      "wait",
      0,
      NS_XMPP_STANZAS,
      { 408, 504, 0, },
    },

    {
      "conflict",
      "access cannot be granted because an existing resource or session exists "
      "with the same name or address",
      "cancel",
      0,
      NS_XMPP_STANZAS,
      { 409, 0, },
    },

    {
      "internal-server-error",
      "the server could not process the stanza because of a misconfiguration "
      "or an otherwise-undefined internal server error",
      "wait",
      0,
      NS_XMPP_STANZAS,
      { 500, 0, },
    },
    {
      "resource-constraint",
      "the server or recipient lacks the system resources necessary to service "
      "the request",
      "wait",
      0,
      NS_XMPP_STANZAS,
      { 500, 0, },
    },

    {
      "feature-not-implemented",
      "the feature requested is not implemented by the recipient or server and "
      "therefore cannot be processed",
      "cancel",
      0,
      NS_XMPP_STANZAS,
      { 501, 0, },
    },

    {
      "service-unavailable",
      "the server or recipient does not currently provide the requested "
      "service",
      "cancel",
      0,
      NS_XMPP_STANZAS,
      { 502, 503, 510, },
    },

    {
      "out-of-order",
      "the request cannot occur at this point in the state machine",
      "cancel",
      XMPP_ERROR_UNEXPECTED_REQUEST,
      NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "unknown-session",
      "the 'sid' attribute specifies a session that is unknown to the "
      "recipient",
      "cancel",
      XMPP_ERROR_ITEM_NOT_FOUND,
      NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "tie-break",
      "The request is rejected because it was sent while the initiator was "
      "awaiting a reply on a similar request.",
      "cancel",
      XMPP_ERROR_CONFLICT,
      NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "unsupported-info",
      "The recipient does not support the informational payload of a "
      "session-info action.",
      "cancel",
      XMPP_ERROR_FEATURE_NOT_IMPLEMENTED,
      NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "no-valid-streams",
      "None of the available streams are acceptable.",
      "cancel",
      XMPP_ERROR_BAD_REQUEST,
      NS_SI,
      { 400, 0 },
    },

    {
      "bad-profile",
      "The profile is not understood or invalid.",
      "modify",
      XMPP_ERROR_BAD_REQUEST,
      NS_SI,
      { 400, 0 },
    },
};


GabbleXmppErrorType
gabble_xmpp_error_type_to_enum (const gchar *error_type)
{
  if (!tp_strdiff (error_type, "cancel"))
    return XMPP_ERROR_TYPE_CANCEL;

  if (!tp_strdiff (error_type, "continue"))
    return XMPP_ERROR_TYPE_CONTINUE;

  if (!tp_strdiff (error_type, "modify"))
    return XMPP_ERROR_TYPE_MODIFY;

  if (!tp_strdiff (error_type, "auth"))
    return XMPP_ERROR_TYPE_AUTH;

  if (!tp_strdiff (error_type, "wait"))
    return XMPP_ERROR_TYPE_WAIT;

  return XMPP_ERROR_TYPE_UNDEFINED;
}


GQuark
gabble_xmpp_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-xmpp-error");
  return quark;
}

GabbleXmppError
gabble_xmpp_error_from_node (WockyNode *error_node,
                             GabbleXmppErrorType *type_out)
{
  gint i, j;
  const gchar *error_code_str;

  g_return_val_if_fail (error_node != NULL, XMPP_ERROR_UNDEFINED_CONDITION);

  /* First, try to look it up the modern way */
  if (node_iter (error_node))
    {
      if (type_out != NULL)
        *type_out = gabble_xmpp_error_type_to_enum (
            wocky_node_get_attribute (error_node, "type"));

      /* we loop backwards because the most specific errors are the larger
       * numbers; the >= 0 test is OK because i is signed */
      for (i = NUM_XMPP_ERRORS - 1; i >= 0; i--)
        {
          if (lm_message_node_get_child_with_namespace (error_node,
                xmpp_errors[i].name, xmpp_errors[i].namespace))
            {
              return i;
            }
        }
    }

  /* Ok, do it the legacy way */
  error_code_str = wocky_node_get_attribute (error_node, "code");
  if (error_code_str)
    {
      gint error_code;

      error_code = atoi (error_code_str);

      /* skip UNDEFINED_CONDITION, we want code 500 to be translated
       * to INTERNAL_SERVER_ERROR */
      for (i = 1; i < NUM_XMPP_ERRORS; i++)
        {
          const XmppErrorSpec *spec = &xmpp_errors[i];

          for (j = 0; j < MAX_LEGACY_ERRORS; j++)
            {
              gint cur_code = spec->legacy_errors[j];
              if (cur_code == 0)
                break;

              if (cur_code == error_code)
                {
                  if (type_out != NULL)
                    *type_out = gabble_xmpp_error_type_to_enum (spec->type);

                  return i;
                }
            }
        }
    }

  if (type_out != NULL)
    *type_out = XMPP_ERROR_TYPE_UNDEFINED;

  return XMPP_ERROR_UNDEFINED_CONDITION;
}

static GError *
gabble_xmpp_error_to_g_error (GabbleXmppError error)
{
  if (error >= NUM_XMPP_ERRORS)
      return g_error_new (GABBLE_XMPP_ERROR, XMPP_ERROR_UNDEFINED_CONDITION,
          "Unknown or invalid XMPP error");

  return g_error_new (GABBLE_XMPP_ERROR,
                      error,
                      "%s", xmpp_errors[error].description);
}

/*
 * See RFC 3920: 4.7 Stream Errors, 9.3 Stanza Errors.
 */
WockyNode *
gabble_xmpp_error_to_node (GabbleXmppError error,
                           WockyNode *parent_node,
                           const gchar *errmsg)
{
  const XmppErrorSpec *spec, *extra;
  WockyNode *error_node, *node;
  gchar str[6];

  g_return_val_if_fail (error != XMPP_ERROR_UNDEFINED_CONDITION &&
      error < NUM_XMPP_ERRORS, NULL);

  if (xmpp_errors[error].specialises)
    {
      extra = &xmpp_errors[error];
      spec = &xmpp_errors[extra->specialises];
    }
  else
    {
      extra = NULL;
      spec = &xmpp_errors[error];
    }

  error_node = wocky_node_add_child_with_content (parent_node, "error", NULL);

  sprintf (str, "%d", spec->legacy_errors[0]);
  wocky_node_set_attribute (error_node, "code", str);

  if (spec->type)
    {
      wocky_node_set_attribute (error_node, "type", spec->type);
    }

  node = wocky_node_add_child_with_content (error_node, spec->name, NULL);
  node->ns = g_quark_from_string (NS_XMPP_STANZAS);

  if (extra != NULL)
    {
      node = wocky_node_add_child_with_content (error_node, extra->name, NULL);
      node->ns = g_quark_from_string (extra->namespace);
    }

  if (NULL != errmsg)
    wocky_node_add_child_with_content (error_node, "text", errmsg);

  return error_node;
}

const gchar *
gabble_xmpp_error_string (GabbleXmppError error)
{
  if (error < NUM_XMPP_ERRORS)
    return xmpp_errors[error].name;
  else
    return NULL;
}

const gchar *
gabble_xmpp_error_description (GabbleXmppError error)
{
  if (error < NUM_XMPP_ERRORS)
    return xmpp_errors[error].description;
  else
    return NULL;
}

GError *
gabble_message_get_xmpp_error (LmMessage *msg)
{
  g_return_val_if_fail (msg != NULL, NULL);

  if (lm_message_get_sub_type (msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      WockyNode *error_node = wocky_node_get_child (
          wocky_stanza_get_top_node (msg), "error");

      if (error_node != NULL)
        {
          return gabble_xmpp_error_to_g_error
              (gabble_xmpp_error_from_node (error_node, NULL));
        }
      else
        {
          return g_error_new (GABBLE_XMPP_ERROR,
              XMPP_ERROR_UNDEFINED_CONDITION, "Unknown or invalid XMPP error");
        }
    }

  /* no error */
  return NULL;
}

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
