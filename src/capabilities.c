/*
 * capabilities.c - Connection.Interface.Capabilities constants and utilities
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

#include "config.h"
#include "capabilities.h"

#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-manager.h>

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "caps-channel-manager.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "media-channel.h"
#include "util.h"

static const Feature self_advertised_features[] =
{
  { FEATURE_FIXED, NS_GOOGLE_FEAT_SESSION, 0},
  { FEATURE_FIXED, NS_GOOGLE_TRANSPORT_P2P, PRESENCE_CAP_GOOGLE_TRANSPORT_P2P},
  { FEATURE_FIXED, NS_JINGLE_TRANSPORT_RAWUDP, PRESENCE_CAP_JINGLE_TRANSPORT_RAWUDP},
  { FEATURE_FIXED, NS_JINGLE_TRANSPORT_ICEUDP, PRESENCE_CAP_JINGLE_TRANSPORT_ICEUDP},
  { FEATURE_FIXED, NS_JINGLE015, PRESENCE_CAP_JINGLE015},
  { FEATURE_FIXED, NS_JINGLE032, PRESENCE_CAP_JINGLE032},
  { FEATURE_FIXED, NS_CHAT_STATES, PRESENCE_CAP_CHAT_STATES},
  { FEATURE_FIXED, NS_NICK, 0},
  { FEATURE_FIXED, NS_NICK "+notify", 0},
  { FEATURE_FIXED, NS_SI, PRESENCE_CAP_SI},
  { FEATURE_FIXED, NS_IBB, PRESENCE_CAP_IBB},
  { FEATURE_FIXED, NS_TUBES, PRESENCE_CAP_SI_TUBES},
  { FEATURE_FIXED, NS_BYTESTREAMS, PRESENCE_CAP_BYTESTREAMS},
  { FEATURE_FIXED, NS_FILE_TRANSFER, PRESENCE_CAP_SI_FILE_TRANSFER},

  { FEATURE_BUNDLE_COMPAT, NS_GOOGLE_FEAT_VOICE, PRESENCE_CAP_GOOGLE_VOICE},
  { FEATURE_OPTIONAL, NS_GOOGLE_FEAT_VIDEO, PRESENCE_CAP_GOOGLE_VIDEO },
  { FEATURE_OPTIONAL, NS_JINGLE_DESCRIPTION_AUDIO,
    PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO},
  { FEATURE_OPTIONAL, NS_JINGLE_DESCRIPTION_VIDEO,
    PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO},
  { FEATURE_OPTIONAL, NS_JINGLE_RTP, PRESENCE_CAP_JINGLE_RTP },
  { FEATURE_OPTIONAL, NS_JINGLE_RTP_AUDIO, PRESENCE_CAP_JINGLE_RTP_AUDIO },
  { FEATURE_OPTIONAL, NS_JINGLE_RTP_VIDEO, PRESENCE_CAP_JINGLE_RTP_VIDEO },

  { FEATURE_OPTIONAL, NS_OLPC_BUDDY_PROPS "+notify", PRESENCE_CAP_OLPC_1},
  { FEATURE_OPTIONAL, NS_OLPC_ACTIVITIES "+notify", PRESENCE_CAP_OLPC_1},
  { FEATURE_OPTIONAL, NS_OLPC_CURRENT_ACTIVITY "+notify", PRESENCE_CAP_OLPC_1},
  { FEATURE_OPTIONAL, NS_OLPC_ACTIVITY_PROPS "+notify", PRESENCE_CAP_OLPC_1},

  { FEATURE_OPTIONAL, NS_GEOLOC "+notify", PRESENCE_CAP_GEOLOCATION},

  { 0, NULL, 0}
};

GSList *
capabilities_get_features (GabblePresenceCapabilities caps,
                           GHashTable *per_channel_manager_caps)
{
  GHashTableIter channel_manager_iter;
  GSList *features = NULL;
  const Feature *i;

  for (i = self_advertised_features; NULL != i->ns; i++)
    if ((i->caps & caps) == i->caps)
      features = g_slist_append (features, (gpointer) i);

  if (per_channel_manager_caps != NULL)
    {
      gpointer manager;
      gpointer cap;

      g_hash_table_iter_init (&channel_manager_iter, per_channel_manager_caps);
      while (g_hash_table_iter_next (&channel_manager_iter,
                 &manager, &cap))
        {
          gabble_caps_channel_manager_get_feature_list (manager, cap,
              &features);
        }
    }

  return features;
}

static gboolean
omits_content_creators (LmMessageNode *identity)
{
  const gchar *name, *suffix;
  gchar *end;
  int ver;

  if (tp_strdiff (identity->name, "identity"))
    return FALSE;

  name = lm_message_node_get_attribute (identity, "name");

  if (name == NULL)
    return FALSE;

#define PREFIX "Telepathy Gabble 0.7."

  if (!g_str_has_prefix (name, PREFIX))
    return FALSE;

  suffix = name + strlen (PREFIX);
  ver = strtol (suffix, &end, 10);

  if (*end != '\0')
    return FALSE;

  /* Gabble versions since 0.7.16 did not send the creator='' attribute for
   * contents. The bug is fixed in 0.7.29.
   */
  if (ver >= 16 && ver < 29)
    {
      DEBUG ("contact is using '%s' which omits 'creator'", name);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

GabblePresenceCapabilities
capabilities_parse (LmMessageNode *query_result)
{
  GabblePresenceCapabilities ret = PRESENCE_CAP_NONE;
  const gchar *var;
  const Feature *i;
  NodeIter j;

  for (j = node_iter (query_result); j; j = node_iter_next (j))
    {
      LmMessageNode *child = node_iter_data (j);

      if (0 != strcmp (child->name, "feature"))
        {
          if (omits_content_creators (child))
            ret |= PRESENCE_CAP_JINGLE_OMITS_CONTENT_CREATOR;

          continue;
        }

      var = lm_message_node_get_attribute (child, "var");

      if (NULL == var)
        continue;

      for (i = self_advertised_features; i->ns != NULL; i++)
        {
          if (0 == strcmp (var, i->ns))
            {
              ret |= i->caps;
              break;
            }
        }

      if (i->ns == NULL)
        DEBUG ("ignoring unknown capability %s", var);
    }

  return ret;
}

void
capabilities_fill_cache (GabblePresenceCache *cache)
{
#define GOOGLE_BUNDLE(cap, features) \
  gabble_presence_cache_add_bundle_caps (cache, \
      "http://www.google.com/xmpp/client/caps#" cap, features); \
  gabble_presence_cache_add_bundle_caps (cache, \
      "http://talk.google.com/xmpp/client/caps#" cap, features);

  /* Cache various bundle from the Google Talk clients as trusted.  Some old
   * versions of Google Talk do not reply correctly to discovery requests.
   * Plus, we know what Google's bundles mean, so it's a waste of time to disco
   * them, particularly the ones for features we don't support. The desktop
   * client doesn't currently have all of these, but it doesn't hurt to cache
   * them anyway.
   */
  GOOGLE_BUNDLE ("voice-v1", PRESENCE_CAP_GOOGLE_VOICE);
  GOOGLE_BUNDLE ("video-v1", PRESENCE_CAP_GOOGLE_VIDEO);

  /* Not really sure what these ones are. */
  GOOGLE_BUNDLE ("share-v1", 0);
  GOOGLE_BUNDLE ("sms-v1", 0);

  /* TODO: remove this when we fix fd.o#22768. */
  GOOGLE_BUNDLE ("pmuc-v1", 0);

  /* The camera-v1 bundle seems to mean "I have a camera plugged in". Not
   * having it doesn't seem to affect anything, and we have no way of exposing
   * that information anyway.
   */
  GOOGLE_BUNDLE ("camera-v1", 0);

#undef GOOGLE_BUNDLE

  /* We should also cache the ext='' bundles Gabble advertises: older Gabbles
   * advertise these and don't support hashed caps, and we shouldn't need to
   * disco them.
   */
  gabble_presence_cache_add_bundle_caps (cache,
      NS_GABBLE_CAPS "#" BUNDLE_VOICE_V1, PRESENCE_CAP_GOOGLE_VOICE);
  gabble_presence_cache_add_bundle_caps (cache,
      NS_GABBLE_CAPS "#" BUNDLE_VIDEO_V1, PRESENCE_CAP_GOOGLE_VIDEO);
}

GabblePresenceCapabilities
capabilities_get_initial_caps ()
{
  GabblePresenceCapabilities ret = 0;
  const Feature *feat;

  for (feat = self_advertised_features; NULL != feat->ns; feat++)
    {
      if (feat->feature_type == FEATURE_FIXED)
        {
          ret |= feat->caps;
        }
    }

  return ret;
}

const CapabilityConversionData capabilities_conversions[] =
{
  { TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
    _gabble_media_channel_typeflags_to_caps,
    _gabble_media_channel_caps_to_typeflags },
  { NULL, NULL, NULL}
};

