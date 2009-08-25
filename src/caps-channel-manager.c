/*
 * caps-channel-manager.c - interface holding capabilities functions for
 * channel managers
 *
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
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
#include "caps-channel-manager.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/channel-manager.h>


#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE
#include "debug.h"

GType
gabble_caps_channel_manager_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo info = {
        sizeof (GabbleCapsChannelManagerIface),
        NULL,   /* base_init */
        NULL,   /* base_finalize */
        NULL,   /* class_init */
        NULL,   /* class_finalize */
        NULL,   /* class_data */
        0,
        0,      /* n_preallocs */
        NULL    /* instance_init */
      };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "GabbleCapsChannelManager", &info, 0);

      g_type_interface_add_prerequisite (type, TP_TYPE_CHANNEL_MANAGER);
    }

  return type;
}

/* Virtual-method wrappers */

void
gabble_caps_channel_manager_get_contact_capabilities (
    GabbleCapsChannelManager *caps_manager,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  GabbleCapsChannelManagerIface *iface =
    GABBLE_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  GabbleCapsChannelManagerGetContactCapsFunc method = iface->get_contact_caps;

  if (method != NULL)
    {
      method (caps_manager, handle, caps, arr);
    }
  /* ... else assume there is not caps for this kind of channels */
}

/**
 * gabble_caps_channel_manager_add_capability:
 * @cap: the Telepathy-level capability to add
 * @cap_set: a set of XMPP namespaces, to which the namespaces corresponding to
 *           @cap should be added
 *
 * Used to advertise that we support the XMPP capabilities corresponding to the
 * Telepathy capability supplied.
 */
void
gabble_caps_channel_manager_add_capability (
    GabbleCapsChannelManager *caps_manager,
    GHashTable *cap,
    GabbleCapabilitySet *cap_set)
{
  GabbleCapsChannelManagerIface *iface =
    GABBLE_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  GabbleCapsChannelManagerAddCapFunc method = iface->add_cap;

  if (method != NULL)
    {
      method (caps_manager, cap, cap_set);
    }
  /* ... else, nothing to do */
}

/**
 * gabble_caps_channel_manager_represent_client:
 * @self: a channel manager
 * @client_name: the name of the client, for any debug messages
 * @filters: the channel classes accepted by the client, as an array of
 *  GHashTable with string keys and GValue values
 * @cap_tokens: the handler capability tokens supported by the client
 * @cap_set: a set into which to merge additional XMPP capabilities
 *
 * Convert the capabilities of a Telepathy client into XMPP capabilities to be
 * advertised.
 *
 * (The actual XMPP capabilities advertised will be the union of the XMPP
 * capabilities of every installed client.)
 */
void
gabble_caps_channel_manager_represent_client (
    GabbleCapsChannelManager *caps_manager,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens,
    GabbleCapabilitySet *cap_set)
{
  GabbleCapsChannelManagerIface *iface =
    GABBLE_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  GabbleCapsChannelManagerRepresentClientFunc method = iface->represent_client;

  if (method != NULL)
    {
      method (caps_manager, client_name, filters, cap_tokens, cap_set);
    }
}
