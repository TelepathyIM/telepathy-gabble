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

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "base64.h"
#include "capabilities.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "presence.h"
#include "util.h"

static gint
char_cmp (gconstpointer a, gconstpointer b)
{
  gchar *left = *(gchar **) a;
  gchar *right = *(gchar **) b;

  return strcmp (left, right);
}

static gint
identity_cmp (gconstpointer a, gconstpointer b)
{
  WockyDiscoIdentity *left = *(WockyDiscoIdentity **) a;
  WockyDiscoIdentity *right = *(WockyDiscoIdentity **) b;
  gint ret;

  if ((ret = strcmp (left->category, right->category)) != 0)
    return ret;
  if ((ret = strcmp (left->type, right->type)) != 0)
    return ret;
  if ((ret = strcmp (left->lang, right->lang)) != 0)
    return ret;
  return strcmp (left->name, right->name);
}

static void
gabble_presence_free_xep0115_hash (
    GPtrArray *features,
    GPtrArray *identities)
{
  g_ptr_array_foreach (features, (GFunc) g_free, NULL);
  wocky_disco_identity_array_free (identities);

  g_ptr_array_free (features, TRUE);
}

static gchar *
caps_hash_compute (
    GPtrArray *features,
    GPtrArray *identities)
{
  GString *s;
  gchar sha1[SHA1_HASH_SIZE];
  guint i;
  gchar *encoded;

  g_ptr_array_sort (identities, identity_cmp);
  g_ptr_array_sort (features, char_cmp);

  s = g_string_new ("");

  for (i = 0 ; i < identities->len ; i++)
    {
      const WockyDiscoIdentity *identity = g_ptr_array_index (identities, i);
      gchar *str = g_strdup_printf ("%s/%s/%s/%s",
          identity->category, identity->type,
          identity->lang ? identity->lang : "",
          identity->name ? identity->name : "");
      g_string_append (s, str);
      g_string_append_c (s, '<');
      g_free (str);
    }

  for (i = 0 ; i < features->len ; i++)
    {
      g_string_append (s, g_ptr_array_index (features, i));
      g_string_append_c (s, '<');
    }

  sha1_bin (s->str, s->len, (guchar *) sha1);
  g_string_free (s, TRUE);

  encoded = base64_encode (SHA1_HASH_SIZE, sha1, FALSE);

  return encoded;
}

static void
ptr_array_strdup (gpointer str,
    gpointer array)
{
  g_ptr_array_add (array, g_strdup (str));
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
  gchar *str;

  /* XEP-0030 requires at least 1 identity. We don't need more. */
  g_ptr_array_add (identities,
      wocky_disco_identity_new ("client", CLIENT_TYPE,
          NULL, PACKAGE_STRING));

  /* FIXME: allow iteration over the strings without copying */
  cap_set = gabble_presence_peek_caps (presence);
  gabble_capability_set_foreach (cap_set, ptr_array_strdup, features);

  str = caps_hash_compute (features, identities);

  gabble_presence_free_xep0115_hash (features, identities);

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

  /* FIXME: allow iteration over the strings without copying */
  gabble_capability_set_foreach (cap_set, ptr_array_strdup, features);

  str = caps_hash_compute (features, identities_copy);

  gabble_presence_free_xep0115_hash (features, identities_copy);

  return str;
}
