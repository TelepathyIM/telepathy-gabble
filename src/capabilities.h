/*
 * capabilities.h - Connection.Interface.Capabilities constants and utilities
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __GABBLE_CAPABILITIES__H__
#define __GABBLE_CAPABILITIES__H__

#include <glib-object.h>

#include "presence.h"

#define BUNDLE_VOICE_V1         "voice-v1"
#define BUNDLE_JINGLE_AUDIO     "jingle-audio"
#define BUNDLE_JINGLE_VIDEO     "jingle-video"

typedef struct _Feature Feature;

struct _Feature
{
  const gchar *bundle;
  const gchar *ns;
  GabblePresenceCapabilities caps;
};

/*
 * capabilities_get_features
 *
 * Return a linked list of const Feature structs corresponding to the given
 * GabblePresenceCapabilities.
 */
GSList *
capabilities_get_features (GabblePresenceCapabilities caps);

/*
 * capabilities_fill_cache
 *
 * Fill up the given GabblePresenceCache with known feature nodes
 */
void
capabilities_fill_cache (GabblePresenceCache *cache);

/*
 * capabilities_get_initial_caps
 *
 * Return the GabblePresenceCapabilities we always have
 */
GabblePresenceCapabilities
capabilities_get_initial_caps ();

typedef GabblePresenceCapabilities (*TypeFlagsToCapsFunc) (guint typeflags);
typedef guint (*CapsToTypeFlagsFunc) (GabblePresenceCapabilities caps);

typedef struct _CapabilityConversionData CapabilityConversionData;

struct _CapabilityConversionData
{
  const gchar *iface;
  TypeFlagsToCapsFunc tf2c_fn;
  CapsToTypeFlagsFunc c2tf_fn;
};

extern const CapabilityConversionData capabilities_conversions[];

#endif  /* __GABBLE_CAPABILITIES__H__ */

