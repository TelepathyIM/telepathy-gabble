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

#include <loudmouth/loudmouth.h>

#include "types.h"

typedef struct _Feature Feature;

struct _Feature
{
  enum {
    FEATURE_FIXED,
    FEATURE_OPTIONAL,
    FEATURE_BUNDLE_COMPAT   /* just for voice-v1/video-v1 */
  } feature_type;
  gchar *ns;
  GabblePresenceCapabilities caps;
};

/* Pseudo-capabilities for buggy or strange implementations, represented as
 * strings starting with a character not allowed in XML (the ASCII beep :-) */
#define QUIRK_PREFIX_CHAR '\x07'
#define QUIRK_PREFIX "\x07"
/* Gabble 0.7.x with 16 <= x < 29 omits @creator on <content/> */
#define QUIRK_OMITS_CONTENT_CREATORS "\x07omits-content-creators"

/**
 * GabbleCapabilitySet:
 *
 * A set of capabilities.
 */
typedef struct _GabbleCapabilitySet GabbleCapabilitySet;

GabbleCapabilitySet *gabble_capability_set_new (void);
GabbleCapabilitySet *gabble_capability_set_new_from_stanza (
    LmMessageNode *query_result);
GabbleCapabilitySet *gabble_capability_set_new_from_flags (
    GabblePresenceCapabilities caps);
GabbleCapabilitySet *gabble_capability_set_copy (
    const GabbleCapabilitySet *caps);
void gabble_capability_set_update (GabbleCapabilitySet *target,
    const GabbleCapabilitySet *source);
void gabble_capability_set_add (GabbleCapabilitySet *caps,
    const gchar *cap);
gboolean gabble_capability_set_has (const GabbleCapabilitySet *caps,
    const gchar *cap);
gboolean gabble_capability_set_has_one (const GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *alternatives);
gboolean gabble_capability_set_at_least (const GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *query);
gboolean gabble_capability_set_equals (const GabbleCapabilitySet *a,
    const GabbleCapabilitySet *b);
void gabble_capability_set_clear (GabbleCapabilitySet *caps);
void gabble_capability_set_free (GabbleCapabilitySet *caps);
void gabble_capability_set_foreach (const GabbleCapabilitySet *caps,
    GFunc func, gpointer user_data);
gchar *gabble_capability_set_dump (const GabbleCapabilitySet *caps,
    const gchar *indent);

/* A predicate used by the presence code to select suitable resources */
typedef gboolean (*GabbleCapabilitySetPredicate) (
    const GabbleCapabilitySet *set, gconstpointer user_data);
/* These two functions are compatible with GabbleCapabilitySetPredicate;
 * pass in the desired capabilities as the user_data */
#define gabble_capability_set_predicate_has \
  ((GabbleCapabilitySetPredicate) gabble_capability_set_has)
#define gabble_capability_set_predicate_has_one \
  ((GabbleCapabilitySetPredicate) gabble_capability_set_has_one)
#define gabble_capability_set_predicate_at_least \
  ((GabbleCapabilitySetPredicate) gabble_capability_set_at_least)

/* Some useful capability sets for Jingle etc. */
const GabbleCapabilitySet *gabble_capabilities_get_any_audio (void);
const GabbleCapabilitySet *gabble_capabilities_get_any_video (void);
const GabbleCapabilitySet *gabble_capabilities_get_any_transport (void);
const GabbleCapabilitySet *gabble_capabilities_get_geoloc_notify (void);
const GabbleCapabilitySet *gabble_capabilities_get_olpc_notify (void);

/* XEP-0115 version 1.3:
 *
 * "The names of the feature bundles MUST NOT be used for semantic purposes:
 * they are merely opaque identifiers"
 *
 * However, some old Jabber clients (e.g. Gabble 0.2) and various Google
 * clients require the bundle names "voice-v1" and "video-v1". We keep these
 * names for compatibility.
 */
#define BUNDLE_VOICE_V1         "voice-v1"
#define BUNDLE_VIDEO_V1         "video-v1"

const GabbleCapabilitySet *gabble_capabilities_get_bundle_voice_v1 (void);
const GabbleCapabilitySet *gabble_capabilities_get_bundle_video_v1 (void);

/*
 * capabilities_fill_cache
 *
 * Fill up the given GabblePresenceCache with known feature nodes
 */
void capabilities_fill_cache (GabblePresenceCache *cache);

/* Return the capabilities we always have */
const GabbleCapabilitySet *gabble_capabilities_get_initial_caps (void);

GabblePresenceCapabilities capabilities_parse (const GabbleCapabilitySet *cap_set);

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

void gabble_capabilities_init (GabbleConnection *conn);
void gabble_capabilities_finalize (GabbleConnection *conn);

#endif  /* __GABBLE_CAPABILITIES__H__ */

