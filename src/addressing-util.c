/*
 * addressing-util.c - Source for Gabble addressing utility functions
 * Copyright (C) 2010 Collabora Ltd.
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
#include "addressing-util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connection.h"
#include "util.h"

static const gchar *addressable_vcard_fields[] = {"x-jabber", NULL};
static const gchar *addressable_uri_schemes[] = {"xmpp", NULL};


const gchar * const *
gabble_get_addressable_uri_schemes ()
{
  return addressable_uri_schemes;
}

const gchar * const *
gabble_get_addressable_vcard_fields ()
{
  return addressable_vcard_fields;
}

gchar *
gabble_parse_uri (const gchar *uri,
    gchar **normalized_uri,
    GError **error)
{
  /* excuse the poor man's URI parsing, couldn't find a GLib helper */
  gchar **tokenized_uri = g_strsplit (uri, ":", 2);
  gchar *jid = NULL;
  gchar *normalized_scheme = NULL;

  g_return_val_if_fail (uri != NULL, NULL);

  if (g_strv_length (tokenized_uri) != 2)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid URI", uri);
      goto OUT;
    }

  normalized_scheme = g_ascii_strdown (tokenized_uri[0], -1);

  if (!tp_strv_contains (addressable_uri_schemes, normalized_scheme))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "'%s' URI scheme is not supported by this protocol",
          normalized_scheme);
      goto OUT;
    }

  jid = gabble_normalize_contact (NULL, tokenized_uri[1],
      GUINT_TO_POINTER (GABBLE_JID_GLOBAL), error);

  if (jid && normalized_uri != NULL)
    *normalized_uri = g_strdup_printf ("%s:%s", normalized_scheme, jid);

 OUT:
  g_free (normalized_scheme);
  g_strfreev (tokenized_uri);
  return jid;
}

TpHandle
gabble_ensure_handle_from_uri (TpHandleRepoIface *repo,
    const gchar *uri,
    GError **error)
{
  TpHandle handle;
  gchar *jid = gabble_parse_uri (uri, NULL, error);

  if (jid == NULL)
    return 0;

  handle = tp_handle_ensure (repo, jid, NULL, error);

  g_free (jid);

  return handle;
}

gchar *
gabble_parse_vcard_address (const gchar *vcard_field,
    const gchar *vcard_address,
    gchar **normalized_field,
    GError **error)
{
  gchar *jid = NULL;
  gchar *_normalized_field;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (vcard_address != NULL, NULL);

  _normalized_field = g_ascii_strdown (vcard_field, -1);

  if (!tp_strv_contains (addressable_vcard_fields, _normalized_field))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "'%s' vCard field is not supported by this protocol", vcard_field);
      goto OUT;
    }

  jid = gabble_normalize_contact (NULL, vcard_address,
      GUINT_TO_POINTER (GABBLE_JID_GLOBAL), error);

  if (normalized_field != NULL)
    *normalized_field = g_strdup (_normalized_field);

 OUT:
  g_free (_normalized_field);
  return jid;
}

TpHandle
gabble_ensure_handle_from_vcard_address (TpHandleRepoIface *repo,
    const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error)
{
  TpHandle handle;
  gchar *jid = gabble_parse_vcard_address (vcard_field, vcard_address, NULL, error);

  if (jid == NULL)
    return 0;

  handle = tp_handle_ensure (repo, jid, NULL, error);

  g_free (jid);

  return handle;
}

gchar **
gabble_uris_for_handle (TpHandleRepoIface *contact_repo,
    TpHandle contact)
{
  guint len = g_strv_length ((gchar **) addressable_uri_schemes);
  guint i;
  gchar **uris = g_new0 (gchar *, len + 1);

  for (i = 0; i < len; i++)
    uris[i] = gabble_uri_for_handle (contact_repo, addressable_uri_schemes[i], contact);

  return uris;
}

GHashTable *
gabble_vcard_addresses_for_handle (TpHandleRepoIface *contact_repo,
    TpHandle contact)
{
  const gchar **field;
  GHashTable *addresses = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) g_free);

  for (field = addressable_vcard_fields; *field != NULL; field++)
    g_hash_table_insert (addresses, (gpointer) *field,
        gabble_vcard_address_for_handle (contact_repo, *field, contact));

  return addresses;
}

gchar *
gabble_vcard_address_for_handle (TpHandleRepoIface *contact_repo,
    const gchar *vcard_field,
    TpHandle contact)
{
  return g_strdup (tp_handle_inspect (contact_repo, contact));
}

gchar *
gabble_uri_for_handle (TpHandleRepoIface *contact_repo,
    const gchar *uri_scheme,
    TpHandle contact)
{
  const gchar *identifier = tp_handle_inspect (contact_repo, contact);
  return g_strdup_printf ("%s:%s", uri_scheme, identifier);
}
