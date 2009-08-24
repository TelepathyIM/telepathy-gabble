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
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/handle-repo-dynamic.h>

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

static const Feature quirks[] = {
      { 0, QUIRK_OMITS_CONTENT_CREATORS,
        PRESENCE_CAP_JINGLE_OMITS_CONTENT_CREATOR },
      { 0, NULL, 0 }
};

static GabbleCapabilitySet *legacy_caps = NULL;
static GabbleCapabilitySet *voice_v1_caps = NULL;
static GabbleCapabilitySet *video_v1_caps = NULL;
static GabbleCapabilitySet *any_audio_caps = NULL;
static GabbleCapabilitySet *any_video_caps = NULL;
static GabbleCapabilitySet *any_transport_caps = NULL;
static GabbleCapabilitySet *initial_caps = NULL;
static GabbleCapabilitySet *geoloc_caps = NULL;
static GabbleCapabilitySet *olpc_caps = NULL;

const GabbleCapabilitySet *
gabble_capabilities_get_legacy (void)
{
  return legacy_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_bundle_voice_v1 (void)
{
  return voice_v1_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_bundle_video_v1 (void)
{
  return video_v1_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_any_audio (void)
{
  return any_audio_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_any_video (void)
{
  return any_video_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_any_transport (void)
{
  return any_transport_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_initial_caps (void)
{
  return initial_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_geoloc_notify (void)
{
  return geoloc_caps;
}

const GabbleCapabilitySet *
gabble_capabilities_get_olpc_notify (void)
{
  return olpc_caps;
}

static gboolean
omits_content_creators (LmMessageNode *identity)
{
  const gchar *name, *suffix;
  gchar *end;
  int ver;

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

gsize feature_handles_refcount = 0;
static TpHandleRepoIface *feature_handles = NULL;

void
gabble_capabilities_init (GabbleConnection *conn)
{
  DEBUG ("%p", conn);

  if (feature_handles_refcount++ == 0)
    {
      const Feature *feat;

      g_assert (feature_handles == NULL);
      /* TpDynamicHandleRepo wants a handle type, which isn't relevant here
       * (we're just using it as a string pool). Use an arbitrary handle type
       * to shut it up. */
      feature_handles = tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT,
          NULL, NULL);

      /* make the pre-cooked bundles */

      legacy_caps = gabble_capability_set_new ();

      for (feat = self_advertised_features; feat->ns != NULL; feat++)
        {
          gabble_capability_set_add (legacy_caps, feat->ns);
        }

      voice_v1_caps = gabble_capability_set_new ();
      gabble_capability_set_add (voice_v1_caps, NS_GOOGLE_FEAT_VOICE);

      video_v1_caps = gabble_capability_set_new ();
      gabble_capability_set_add (video_v1_caps, NS_GOOGLE_FEAT_VIDEO);

      any_audio_caps = gabble_capability_set_new ();
      gabble_capability_set_add (any_audio_caps, NS_JINGLE_RTP_AUDIO);
      gabble_capability_set_add (any_audio_caps, NS_JINGLE_DESCRIPTION_AUDIO);
      gabble_capability_set_add (any_audio_caps, NS_GOOGLE_FEAT_VOICE);

      any_video_caps = gabble_capability_set_new ();
      gabble_capability_set_add (any_video_caps, NS_JINGLE_RTP_VIDEO);
      gabble_capability_set_add (any_video_caps, NS_JINGLE_DESCRIPTION_VIDEO);
      gabble_capability_set_add (any_video_caps, NS_GOOGLE_FEAT_VIDEO);

      any_transport_caps = gabble_capability_set_new ();
      gabble_capability_set_add (any_transport_caps, NS_GOOGLE_TRANSPORT_P2P);
      gabble_capability_set_add (any_transport_caps, NS_JINGLE_TRANSPORT_ICEUDP);
      gabble_capability_set_add (any_transport_caps, NS_JINGLE_TRANSPORT_RAWUDP);

      initial_caps = gabble_capability_set_new ();

      for (feat = self_advertised_features; feat->ns != NULL; feat++)
        {
          if (feat->feature_type == FEATURE_FIXED)
            gabble_capability_set_add (initial_caps, feat->ns);
        }

      geoloc_caps = gabble_capability_set_new_from_flags (
          PRESENCE_CAP_GEOLOCATION);
      olpc_caps = gabble_capability_set_new_from_flags (PRESENCE_CAP_OLPC_1);
    }

  g_assert (feature_handles != NULL);
}

void
gabble_capabilities_finalize (GabbleConnection *conn)
{
  DEBUG ("%p", conn);

  g_assert (feature_handles_refcount > 0);

  if (--feature_handles_refcount == 0)
    {
      gabble_capability_set_free (legacy_caps);
      gabble_capability_set_free (voice_v1_caps);
      gabble_capability_set_free (video_v1_caps);
      gabble_capability_set_free (any_audio_caps);
      gabble_capability_set_free (any_video_caps);
      gabble_capability_set_free (any_transport_caps);
      gabble_capability_set_free (initial_caps);
      gabble_capability_set_free (geoloc_caps);
      gabble_capability_set_free (olpc_caps);

      legacy_caps = NULL;
      voice_v1_caps = NULL;
      video_v1_caps = NULL;
      any_audio_caps = NULL;
      any_video_caps = NULL;
      any_transport_caps = NULL;
      initial_caps = NULL;
      geoloc_caps = NULL;
      olpc_caps = NULL;

      g_object_unref (feature_handles);
      feature_handles = NULL;
    }
}

struct _GabbleCapabilitySet {
    TpHandleSet *handles;
};

void
capabilities_fill_cache (GabblePresenceCache *cache)
{
  /* Cache this bundle from the Google Talk client as trusted. So Gabble will
   * not send any discovery request for this bundle.
   *
   * XMPP does not require to cache this bundle but some old versions of
   * Google Talk do not reply correctly to discovery requests. */
  gabble_presence_cache_add_bundle_caps (cache,
    "http://www.google.com/xmpp/client/caps#voice-v1",
    PRESENCE_CAP_GOOGLE_VOICE, NS_GOOGLE_FEAT_VOICE);
}

const CapabilityConversionData capabilities_conversions[] =
{
  { TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
    _gabble_media_channel_typeflags_to_caps,
    _gabble_media_channel_caps_to_typeflags },
  { NULL, NULL, NULL}
};

GabbleCapabilitySet *
gabble_capability_set_new (void)
{
  GabbleCapabilitySet *ret = g_slice_new0 (GabbleCapabilitySet);

  g_assert (feature_handles != NULL);
  ret->handles = tp_handle_set_new (feature_handles);
  return ret;
}

GabbleCapabilitySet *
gabble_capability_set_new_from_stanza (LmMessageNode *query_result)
{
  GabbleCapabilitySet *ret;
  const gchar *var;
  NodeIter ni;

  g_return_val_if_fail (query_result != NULL, NULL);

  ret = gabble_capability_set_new ();

  for (ni = node_iter (query_result); ni != NULL; ni = node_iter_next (ni))
    {
      LmMessageNode *child = node_iter_data (ni);

      if (!tp_strdiff (child->name, "identity"))
        {
          if (omits_content_creators (child))
            {
              gabble_capability_set_add (ret, QUIRK_OMITS_CONTENT_CREATORS);
            }

          continue;
        }

      if (tp_strdiff (child->name, "feature"))
        continue;

      var = lm_message_node_get_attribute (child, "var");

      if (NULL == var)
        continue;

      if (G_UNLIKELY (var[0] == QUIRK_PREFIX_CHAR))
        {
          /* I think not! (It's not allowed in XML...) */
          continue;
        }

      /* TODO: only store namespaces we understand. */
      gabble_capability_set_add (ret, var);
    }

  return ret;
}

/* This function should disappear when GabbleCapabilitySet replaces
 * GabblePresenceCapabilities.
 */
GabbleCapabilitySet *
gabble_capability_set_new_from_flags (GabblePresenceCapabilities caps)
{
  GabbleCapabilitySet *ret = gabble_capability_set_new ();
  const Feature *i;

  for (i = self_advertised_features; NULL != i->ns; i++)
    if ((i->caps & caps) == i->caps)
      gabble_capability_set_add (ret, i->ns);

  for (i = quirks; NULL != i->ns; i++)
    if ((i->caps & caps) == i->caps)
      gabble_capability_set_add (ret, i->ns);

  return ret;
}

GabbleCapabilitySet *
gabble_capability_set_copy (const GabbleCapabilitySet *caps)
{
  GabbleCapabilitySet *ret;

  g_return_val_if_fail (caps != NULL, NULL);

  ret = gabble_capability_set_new ();

  gabble_capability_set_update (ret, caps);

  return ret;
}

void
gabble_capability_set_update (GabbleCapabilitySet *target,
    const GabbleCapabilitySet *source)
{
  g_return_if_fail (target != NULL);
  g_return_if_fail (source != NULL);

  tp_handle_set_update (target->handles, tp_handle_set_peek (source->handles));
}

typedef struct {
    GSList *deleted;
    TpHandleSet *intersect_with;
} IntersectHelper;

static void
intersect_helper (TpHandleSet *unused G_GNUC_UNUSED,
    TpHandle handle,
    gpointer p)
{
  IntersectHelper *data = p;

  if (!tp_handle_set_is_member (data->intersect_with, handle))
    data->deleted = g_slist_prepend (data->deleted, GUINT_TO_POINTER (handle));
}

void
gabble_capability_set_intersect (GabbleCapabilitySet *target,
    const GabbleCapabilitySet *source)
{
  IntersectHelper data = { NULL, NULL };

  g_return_if_fail (target != NULL);
  g_return_if_fail (source != NULL);

  if (target == source)
    return;

  data.intersect_with = source->handles;

  tp_handle_set_foreach (target->handles, intersect_helper, &data);

  while (data.deleted != NULL)
    {
      DEBUG ("dropping %s", tp_handle_inspect (feature_handles,
            GPOINTER_TO_UINT (data.deleted->data)));
      tp_handle_set_remove (target->handles,
          GPOINTER_TO_UINT (data.deleted->data));
      data.deleted = g_slist_delete_link (data.deleted, data.deleted);
    }
}

static void
remove_from_set (TpHandleSet *unused G_GNUC_UNUSED,
    TpHandle handle,
    gpointer handles)
{
  tp_handle_set_remove (handles, handle);
}

void
gabble_capability_set_exclude (GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *removed)
{
  g_return_if_fail (caps != NULL);
  g_return_if_fail (removed != NULL);

  if (caps == removed)
    {
      gabble_capability_set_clear (caps);
      return;
    }

  tp_handle_set_foreach (removed->handles, remove_from_set, caps->handles);
}

void
gabble_capability_set_add (GabbleCapabilitySet *caps,
    const gchar *cap)
{
  TpHandle handle;

  g_return_if_fail (caps != NULL);
  g_return_if_fail (cap != NULL);

  handle = tp_handle_ensure (feature_handles, cap, NULL, NULL);

  tp_handle_set_add (caps->handles, handle);
  tp_handle_unref (feature_handles, handle);
}

gboolean
gabble_capability_set_remove (GabbleCapabilitySet *caps,
    const gchar *cap)
{
  TpHandle handle;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (cap != NULL, FALSE);

  handle = tp_handle_lookup (feature_handles, cap, NULL, NULL);

  if (handle == 0)
    return FALSE;

  return tp_handle_set_remove (caps->handles, handle);
}

void
gabble_capability_set_clear (GabbleCapabilitySet *caps)
{
  g_return_if_fail (caps != NULL);

  /* There is no tp_handle_set_clear, so do the next best thing */
  tp_handle_set_destroy (caps->handles);
  caps->handles = tp_handle_set_new (feature_handles);
}

void
gabble_capability_set_free (GabbleCapabilitySet *caps)
{
  g_return_if_fail (caps != NULL);

  tp_handle_set_destroy (caps->handles);
  g_slice_free (GabbleCapabilitySet, caps);
}

/* By design, this function can be used as a GabbleCapabilitySetPredicate */
gboolean
gabble_capability_set_has (const GabbleCapabilitySet *caps,
    const gchar *cap)
{
  TpHandle handle;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (cap != NULL, FALSE);

  handle = tp_handle_lookup (feature_handles, cap, NULL, NULL);

  if (handle == 0)
    {
      /* nobody in the whole CM has this capability */
      return FALSE;
    }

  return tp_handle_set_is_member (caps->handles, handle);
}

/* By design, this function can be used as a GabbleCapabilitySetPredicate */
gboolean
gabble_capability_set_has_one (const GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *alternatives)
{
  TpIntSetIter iter;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (alternatives != NULL, FALSE);

  tp_intset_iter_init (&iter, tp_handle_set_peek (alternatives->handles));

  while (tp_intset_iter_next (&iter))
    {
      if (tp_handle_set_is_member (caps->handles, iter.element))
        {
          return TRUE;
        }
    }

  return FALSE;
}

/* By design, this function can be used as a GabbleCapabilitySetPredicate */
gboolean
gabble_capability_set_at_least (const GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *query)
{
  TpIntSetIter iter;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (query != NULL, FALSE);

  tp_intset_iter_init (&iter, tp_handle_set_peek (query->handles));

  while (tp_intset_iter_next (&iter))
    {
      if (!tp_handle_set_is_member (caps->handles, iter.element))
        {
          return FALSE;
        }
    }

  return TRUE;
}

gboolean
gabble_capability_set_equals (const GabbleCapabilitySet *a,
    const GabbleCapabilitySet *b)
{
  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (b != NULL, FALSE);

  return tp_intset_is_equal (tp_handle_set_peek (a->handles),
      tp_handle_set_peek (b->handles));
}

/* Does not iterate over quirks, only real features. */
void
gabble_capability_set_foreach (const GabbleCapabilitySet *caps,
    GFunc func, gpointer user_data)
{
  TpIntSetIter iter;

  g_return_if_fail (caps != NULL);
  g_return_if_fail (func != NULL);

  tp_intset_iter_init (&iter, tp_handle_set_peek (caps->handles));

  while (tp_intset_iter_next (&iter))
    {
      const gchar *var = tp_handle_inspect (feature_handles, iter.element);

      g_return_if_fail (var != NULL);

      if (var[0] != QUIRK_PREFIX_CHAR)
        func ((gchar *) var, user_data);
    }
}

gchar *
gabble_capability_set_dump (const GabbleCapabilitySet *caps,
    const gchar *indent)
{
  GString *ret;
  TpIntSetIter iter;

  g_return_val_if_fail (caps != NULL, NULL);

  if (indent == NULL)
    indent = "";

  ret = g_string_new (indent);
  g_string_append (ret, "--begin--\n");

  tp_intset_iter_init (&iter, tp_handle_set_peek (caps->handles));

  while (tp_intset_iter_next (&iter))
    {
      const gchar *var = tp_handle_inspect (feature_handles, iter.element);

      g_return_val_if_fail (var != NULL, NULL);

      if (var[0] == QUIRK_PREFIX_CHAR)
        {
          g_string_append_printf (ret, "%sQuirk:   %s\n", indent, var + 1);
        }
      else
        {
          g_string_append_printf (ret, "%sFeature: %s\n", indent, var);
        }
    }

  g_string_append (ret, indent);
  g_string_append (ret, "--end--\n");
  return g_string_free (ret, FALSE);
}
