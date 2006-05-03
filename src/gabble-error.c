/*
 * gabble-error.c - Source for Gabble's error handling API
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#define MAX_LEGACY_ERRORS 3

typedef struct {
    const gchar *name;
    const gchar *type;
    const guint16 legacy_errors[MAX_LEGACY_ERRORS];
} XmppErrorSpec;

static const XmppErrorSpec xmpp_errors[NUM_XMPP_ERRORS] =
{
    { "redirect",                     "modify",         { 302, 0, },        },
    { "gone",                         "modify",         { 302, 0, },        },

    { "bad-request",                  "modify",         { 400, 0, },        },
    { "unexpected-request",           "wait",           { 400, 0, },        },
    { "jid-malformed",                "modify",         { 400, 0, },        },

    { "not-authorized",               "auth",           { 401, 0, },        },

    { "payment-required",             "auth",           { 402, 0, },        },

    { "forbidden",                    "auth",           { 403, 0, },        },

    { "item-not-found",               "cancel",         { 404, 0, },        },
    { "recipient-unavailable",        "wait",           { 404, 0, },        },
    { "remote-server-not-found",      "cancel",         { 404, 0, },        },

    { "not-allowed",                  "cancel",         { 405, 0, },        },

    { "not-acceptable",               "modify",         { 406, 0, },        },

    { "registration-required",        "auth",           { 407, 0, },        },
    { "subscription-required",        "auth",           { 407, 0, },        },

    { "remote-server-timeout",        "wait",           { 408, 504, 0, },   },

    { "conflict",                     "cancel",         { 409, 0, },        },

    { "internal-server-error",        "wait",           { 500, 0, },        },
    { "undefined-condition",          NULL,             { 500, 0, },        },
    { "resource-constraint",          "wait",           { 500, 0, },        },

    { "feature-not-implemented",      "cancel",         { 501, 0, },        },

    { "service-unavailable",          "cancel",         { 502, 503, 510, }, },
};

GabbleXmppError
gabble_xmpp_error_from_node (LmMessageNode *error_node)
{
  gint i, j;
  const gchar *error_code_str;

  /* First, try to look it up the modern way */
  if (error_node->children)
    {
      for (i = 0; i < NUM_XMPP_ERRORS; i++)
        {
          if (lm_message_node_get_child (error_node, xmpp_errors[i].name))
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

      for (i = 0; i < NUM_XMPP_ERRORS; i++)
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

  return INVALID_XMPP_ERROR;
}

LmMessageNode *
gabble_xmpp_error_to_node (GabbleXmppError error,
                           LmMessageNode *parent_node)
{
  const XmppErrorSpec *spec;
  LmMessageNode *error_node, *node;
  gchar str[6];

  if (error < 0 || error >= NUM_XMPP_ERRORS)
    return NULL;

  spec = &xmpp_errors[error];

  error_node = lm_message_node_add_child (parent_node, "error", NULL);

  sprintf (str, "%d", spec->legacy_errors[0]);
  lm_message_node_set_attribute (error_node, "code", str);

  if (spec->type)
    {
      lm_message_node_set_attribute (error_node, "type", spec->type);
    }

  node = lm_message_node_add_child (error_node, spec->name, NULL);
  lm_message_node_set_attribute (node, "xmlns",
                                 "urn:ietf:params:xml:ns:xmpp-stanzas");

  return error_node;
}

const gchar *
gabble_xmpp_error_string (GabbleXmppError error)
{
  if (error >= 0 && error < NUM_XMPP_ERRORS)
    return xmpp_errors[error].name;
  else
    return NULL;
}

