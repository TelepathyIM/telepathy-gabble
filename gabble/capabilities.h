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

#include <gabble/capabilities-set.h>

/* Pseudo-capabilities for buggy or strange implementations, represented as
 * strings starting with a character not allowed in XML (the ASCII beep :-) */
#define QUIRK_PREFIX_CHAR '\x07'
#define QUIRK_PREFIX "\x07"
/* Gabble 0.7.x with 16 <= x < 29 omits @creator on <content/> */
#define QUIRK_OMITS_CONTENT_CREATORS "\x07omits-content-creators"
/* The Google Webmail client doesn't support some features */
#define QUIRK_GOOGLE_WEBMAIL_CLIENT "\x07google-webmail-client"
/* The Android GTalk client needs a quirk for component names */
#define QUIRK_ANDROID_GTALK_CLIENT "\x07android-gtalk-client"

/* Some useful capability sets for Jingle etc. */
const GabbleCapabilitySet *gabble_capabilities_get_legacy (void);
const GabbleCapabilitySet *gabble_capabilities_get_any_audio (void);
const GabbleCapabilitySet *gabble_capabilities_get_any_video (void);
const GabbleCapabilitySet *gabble_capabilities_get_any_audio_video (void);
const GabbleCapabilitySet *gabble_capabilities_get_any_google_av (void);
const GabbleCapabilitySet *gabble_capabilities_get_any_jingle_av (void);
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
#define BUNDLE_SHARE_V1         "share-v1"
#define BUNDLE_VOICE_V1         "voice-v1"
#define BUNDLE_VIDEO_V1         "video-v1"
#define BUNDLE_CAMERA_V1        "camera-v1"
#define BUNDLE_PMUC_V1          "pmuc-v1"

const GabbleCapabilitySet *gabble_capabilities_get_bundle_share_v1 (void);
const GabbleCapabilitySet *gabble_capabilities_get_bundle_voice_v1 (void);
const GabbleCapabilitySet *gabble_capabilities_get_bundle_video_v1 (void);

/* Return the capabilities we always have */
const GabbleCapabilitySet *gabble_capabilities_get_fixed_caps (void);

void gabble_capabilities_init (gpointer conn);
void gabble_capabilities_finalize (gpointer conn);

#endif  /* __GABBLE_CAPABILITIES__H__ */

