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
gabble_normalize_uri (const gchar *uri,
    GError **error)
{
  gchar *normalized_jid;
  gchar *normalized_uri = NULL;

  g_return_val_if_fail (uri != NULL, NULL);

  /* get the normalized jid from the uri */
  normalized_jid = gabble_uri_to_jid (uri, error);

  if (normalized_jid != NULL)
    {
      gchar *node = NULL;
      gchar *domain = NULL;
      gchar *resource = NULL;
      gchar *escaped_node = NULL;
      gchar *escaped_domain = NULL;
      gchar *escaped_resource = NULL;
      gchar *escaped_jid = NULL;
      gchar *scheme;
      gchar *normalized_scheme;

      if (!gabble_decode_jid (normalized_jid, &node, &domain, &resource))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is not a valid URI", uri);
          goto OUT;
        }

      /* convert from "foo?" to "foo%3F" */
      if (node)
        escaped_node = g_uri_escape_string (node, NULL, TRUE);
      escaped_domain = g_uri_escape_string (domain, NULL, TRUE);
      if (resource)
        escaped_resource = g_uri_escape_string (resource, NULL, TRUE);

      escaped_jid = gabble_encode_jid (escaped_node, escaped_domain, escaped_resource);

      g_free (node);
      g_free (domain);
      g_free (resource);
      g_free (escaped_node);
      g_free (escaped_domain);
      g_free (escaped_resource);

      scheme = g_uri_parse_scheme (uri);
      normalized_scheme = g_ascii_strdown (scheme, -1);

      normalized_uri = g_strdup_printf ("%s:%s", normalized_scheme, escaped_jid);

      g_free (escaped_jid);
      g_free (scheme);
      g_free (normalized_scheme);
    }

OUT:
  g_free (normalized_jid);

  return normalized_uri;
}

gchar *
gabble_uri_to_jid (const gchar *uri,
    GError **error)
{
  gchar *scheme;
  gchar *unescaped_uri;
  gchar *normalized_jid = NULL;

  g_return_val_if_fail (uri != NULL, NULL);

  scheme = g_uri_parse_scheme (uri);

  /* convert from "xmpp:foo%3F@example.com" to "xmpp:foo?@example.com" */
  unescaped_uri = g_uri_unescape_string (uri, NULL);

  if (scheme == NULL || unescaped_uri == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid URI", uri);
      goto OUT;
    }
  else if (g_ascii_strcasecmp (scheme, "xmpp") == 0)
    {
      const gchar *jid = unescaped_uri + strlen (scheme) + 1; /* Strip the scheme */
      GError *gabble_error = NULL;

      normalized_jid = gabble_normalize_contact (NULL, jid,
          GUINT_TO_POINTER (GABBLE_JID_GLOBAL), &gabble_error);

      if (gabble_error != NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is an invalid address: %s", jid,
              gabble_error->message);
          g_error_free (gabble_error);
          goto OUT;
        }
    }
  else
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "'%s' URI scheme is not supported by this protocol",
          scheme);
      goto OUT;
    }

OUT:
  g_free (scheme);
  g_free (unescaped_uri);

  return normalized_jid;
}

gchar *
gabble_jid_to_uri (const gchar *scheme,
    const gchar *jid,
    GError **error)
{
  gchar *normalized_uri;
  gchar *uri;

  g_return_val_if_fail (scheme != NULL, NULL);

  uri = g_strdup_printf ("%s:%s", scheme, jid);

  normalized_uri = gabble_normalize_uri (uri, error);

  g_free (uri);

  return normalized_uri;
}

TpHandle
gabble_ensure_handle_from_uri (TpHandleRepoIface *repo,
    const gchar *uri,
    GError **error)
{
  TpHandle handle;

  gchar *jid = gabble_uri_to_jid (uri, error);

  if (jid == NULL)
    return 0;

  handle = tp_handle_ensure (repo, jid, NULL, error);

  g_free (jid);

  return handle;
}

gchar *
gabble_parse_vcard_address (const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error)
{
  gchar *normalized_address = NULL;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (vcard_address != NULL, NULL);

  if (g_ascii_strcasecmp (vcard_field, "x-jabber") == 0)
    {
      GError *gabble_error = NULL;

      normalized_address = gabble_normalize_contact (NULL,
          vcard_address, GUINT_TO_POINTER (GABBLE_JID_GLOBAL),
          &gabble_error);

      if (gabble_error != NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is an invalid address: %s", vcard_address,
              gabble_error->message);
          g_error_free (gabble_error);
        }
    }
  else
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "'%s' vCard field is not supported by this protocol", vcard_field);
    }

  return normalized_address;
}

TpHandle
gabble_ensure_handle_from_vcard_address (TpHandleRepoIface *repo,
    const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error)
{
  TpHandle handle;
  gchar *jid = gabble_parse_vcard_address (vcard_field, vcard_address, error);

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
  GPtrArray *uris = g_ptr_array_new ();

  for (const gchar * const *scheme = addressable_uri_schemes; *scheme != NULL; scheme++)
    {
      gchar *uri = gabble_uri_for_handle (contact_repo, *scheme, contact);

      if (uri != NULL)
        {
          g_ptr_array_add (uris, uri);
        }
    }

  g_ptr_array_add (uris, NULL);
  return (gchar **) g_ptr_array_free (uris, FALSE);
}

GHashTable *
gabble_vcard_addresses_for_handle (TpHandleRepoIface *contact_repo,
    TpHandle contact)
{
  GHashTable *addresses = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) g_free);

  for (const gchar * const *field = addressable_vcard_fields; *field != NULL; field++)
    {
      gchar *vcard_address = gabble_vcard_address_for_handle (contact_repo, *field, contact);

      if (vcard_address != NULL)
        {
          g_hash_table_insert (addresses, (gpointer) *field, vcard_address);
        }
    }

  return addresses;
}

gchar *
gabble_vcard_address_for_handle (TpHandleRepoIface *contact_repo,
    const gchar *vcard_field,
    TpHandle contact)
{
  if (g_ascii_strcasecmp (vcard_field, "x-jabber") == 0)
    {
      return g_strdup (tp_handle_inspect (contact_repo, contact));
    }
  else
    {
      return NULL;
    }
}

gchar *
gabble_uri_for_handle (TpHandleRepoIface *contact_repo,
    const gchar *scheme,
    TpHandle contact)
{
  if (g_ascii_strcasecmp (scheme, "xmpp") == 0)
    {
      const gchar *identifier = tp_handle_inspect (contact_repo, contact);
      return gabble_jid_to_uri (scheme, identifier, NULL);
    }
  else
    {
      return NULL;
    }
}
