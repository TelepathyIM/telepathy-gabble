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

#include "gabble-error.h"

#include <stdlib.h>
#include <stdio.h>

#include "namespaces.h"
#include "util.h"

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
      XMPP_ERROR_BAD_REQUEST,
      NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "unsupported-transports",
      "the recipient does not support any of the desired content transport "
      "methods",
      "cancel",
      XMPP_ERROR_FEATURE_NOT_IMPLEMENTED,
      NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "unsupported-content",
      "the recipient does not support any of the desired content description"
      "formats",
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

GQuark
gabble_xmpp_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-xmpp-error");
  return quark;
}

GabbleXmppError
gabble_xmpp_error_from_node (LmMessageNode *error_node)
{
  gint i, j;
  const gchar *error_code_str;

  g_return_val_if_fail (error_node != NULL, XMPP_ERROR_UNDEFINED_CONDITION);

  /* First, try to look it up the modern way */
  if (error_node->children)
    {
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
  error_code_str = lm_message_node_get_attribute (error_node, "code");
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
                return i;
            }
        }
    }

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
                      xmpp_errors[error].description);
}

/*
 * See RFC 3920: 4.7 Stream Errors, 9.3 Stanza Errors.
 */
LmMessageNode *
gabble_xmpp_error_to_node (GabbleXmppError error,
                           LmMessageNode *parent_node,
                           const gchar *errmsg)
{
  const XmppErrorSpec *spec, *extra;
  LmMessageNode *error_node, *node;
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

  error_node = lm_message_node_add_child (parent_node, "error", NULL);

  sprintf (str, "%d", spec->legacy_errors[0]);
  lm_message_node_set_attribute (error_node, "code", str);

  if (spec->type)
    {
      lm_message_node_set_attribute (error_node, "type", spec->type);
    }

  node = lm_message_node_add_child (error_node, spec->name, NULL);
  lm_message_node_set_attribute (node, "xmlns", NS_XMPP_STANZAS);

  if (extra != NULL)
    {
      node = lm_message_node_add_child (error_node, extra->name, NULL);
      lm_message_node_set_attribute (node, "xmlns", extra->namespace);
    }

  if (NULL != errmsg)
    lm_message_node_add_child (error_node, "text", errmsg);

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
      LmMessageNode *error_node = lm_message_node_get_child (msg->node,
          "error");

      if (error_node != NULL)
        {
          return gabble_xmpp_error_to_g_error
              (gabble_xmpp_error_from_node (error_node));
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
