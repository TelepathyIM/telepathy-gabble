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

/* XEP-0115 version 1.3:
 *
 * "The names of the feature bundles MUST NOT be used for semantic purposes:
 * they are merely opaque identifiers"
 *
 * However, some old Jabber clients (e.g. Gabble 0.2) and various Google
 * clients require the bundle name "voice-v1" and "video-v1". We keep these
 * names for compatibility.
 */
#define BUNDLE_VOICE_V1         "voice-v1"
#define BUNDLE_VIDEO_V1         "video-v1"

typedef struct _Feature Feature;

struct _Feature
{
  enum {
    FEATURE_FIXED,
    FEATURE_OPTIONAL,
    FEATURE_BUNDLE_COMPAT   /* just for voice-v1 */
  } feature_type;
  gchar *ns;
  GabblePresenceCapabilities caps;
};

/*
 * capabilities_get_features
 *
 * Return a linked list of const Feature structs corresponding to the given
 * GabblePresenceCapabilities.
 */
GSList *capabilities_get_features (GabblePresenceCapabilities caps,
    GHashTable *per_channel_manager_caps);

/*
 * capabilities_fill_cache
 *
 * Fill up the given GabblePresenceCache with known feature nodes
 */
void capabilities_fill_cache (GabblePresenceCache *cache);

/*
 * capabilities_get_initial_caps
 *
 * Return the GabblePresenceCapabilities we always have
 */
GabblePresenceCapabilities capabilities_get_initial_caps (void);

GabblePresenceCapabilities capabilities_parse (LmMessageNode *query_result);

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

