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

G_DEFINE_INTERFACE (GabbleCapsChannelManager, gabble_caps_channel_manager,
    TP_TYPE_CHANNEL_MANAGER);

/* stub function needed for the G_DEFINE_INTERFACE macro above */
static void
gabble_caps_channel_manager_default_init (
    GabbleCapsChannelManagerInterface *interface)
{
}

/* Virtual-method wrappers */
void
gabble_caps_channel_manager_reset_capabilities (
    GabbleCapsChannelManager *caps_manager)
{
  GabbleCapsChannelManagerInterface *iface =
    GABBLE_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  GabbleCapsChannelManagerResetCapsFunc method = iface->reset_caps;

  if (method != NULL)
    {
      method (caps_manager);
    }
  /* ... else assume there is no need to reset the caps */
}

void
gabble_caps_channel_manager_get_contact_capabilities (
    GabbleCapsChannelManager *caps_manager,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  GabbleCapsChannelManagerInterface *iface =
    GABBLE_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  GabbleCapsChannelManagerGetContactCapsFunc method = iface->get_contact_caps;

  if (method != NULL)
    {
      method (caps_manager, handle, caps, arr);
    }
  /* ... else assume there are no caps for this kind of channel */
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
  GabbleCapsChannelManagerInterface *iface =
    GABBLE_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  GabbleCapsChannelManagerRepresentClientFunc method = iface->represent_client;

  if (method != NULL)
    {
      method (caps_manager, client_name, filters, cap_tokens, cap_set);
    }
}
