/*
 * caps-hash.c - Computing verification string hash (XEP-0115 v1.5)
 * Copyright (C) 2008 Collabora Ltd.
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

/* Computing verification string hash (XEP-0115 v1.5)
 *
 * Gabble does not do anything with dataforms (XEP-0128) included in
 * capabilities.  However, it needs to parse them in order to compute the hash
 * according to XEP-0115.
 */

#include "config.h"
#include "caps-hash.h"

#include <string.h>

#include <wocky/wocky-disco-identity.h>
#include <wocky/wocky-caps-hash.h>

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "base64.h"
#include "gabble/capabilities.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "presence.h"
#include "util.h"

static void
ptr_array_add_str (gpointer str,
    gpointer array)
{
  g_ptr_array_add (array, str);
}

/**
 * Compute our hash as defined by the XEP-0115.
 *
 * Returns: the hash. The called must free the returned hash with g_free().
 */
gchar *
caps_hash_compute_from_self_presence (GabbleConnection *self)
{
  GabblePresence *presence = self->self_presence;
  const GabbleCapabilitySet *cap_set;
  GPtrArray *features = g_ptr_array_new ();
  GPtrArray *identities = wocky_disco_identity_array_new ();
  GPtrArray *data_forms;
  gchar *str;

  /* XEP-0030 requires at least 1 identity. We don't need more. */
  g_ptr_array_add (identities,
      wocky_disco_identity_new ("client", CLIENT_TYPE,
          NULL, PACKAGE_STRING));

  cap_set = gabble_presence_peek_caps (presence);
  gabble_capability_set_foreach (cap_set, ptr_array_add_str, features);

  data_forms = gabble_presence_peek_data_forms (presence);

  str = wocky_caps_hash_compute_from_lists (features, identities, data_forms);

  g_ptr_array_free (features, TRUE);
  wocky_disco_identity_array_free (identities);

  return str;
}

/**
 * Compute the hash as defined by the XEP-0115 from a received GabbleCapabilitySet
 *
 * Returns: the hash. The called must free the returned hash with g_free().
 */
gchar *
gabble_caps_hash_compute (const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities)
{
  GPtrArray *features = g_ptr_array_new ();
  GPtrArray *identities_copy = ((identities == NULL) ?
      wocky_disco_identity_array_new () :
      wocky_disco_identity_array_copy (identities));
  gchar *str;

  gabble_capability_set_foreach (cap_set, ptr_array_add_str, features);

  str = wocky_caps_hash_compute_from_lists (features, identities_copy, NULL);

  g_ptr_array_free (features, TRUE);
  wocky_disco_identity_array_free (identities_copy);

  return str;
}
