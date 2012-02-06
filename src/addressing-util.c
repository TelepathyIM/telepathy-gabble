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

#include <wocky/wocky.h>

#include "connection.h"
#include "util.h"

static const gchar *addressable_vcard_fields[] = {"x-jabber", "x-facebook-id", NULL};
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
gabble_normalize_contact_uri (const gchar *uri,
    GError **error)
{
  gchar *scheme = NULL;
  gchar *normalized_jid = NULL;
  gchar *normalized_uri = NULL;

  g_return_val_if_fail (uri != NULL, NULL);

  normalized_jid = gabble_uri_to_jid (uri, error);
  if (normalized_jid == NULL)
    {
      goto OUT;
    }

  scheme = g_uri_parse_scheme (uri);

  normalized_uri = gabble_jid_to_uri (scheme, normalized_jid, error);

OUT:
  g_free (scheme);
  g_free (normalized_jid);

  return normalized_uri;
}

gchar *
gabble_uri_to_jid (const gchar *uri,
    GError **error)
{
  gchar *scheme;
  gchar *normalized_jid = NULL;

  g_return_val_if_fail (uri != NULL, NULL);

  scheme = g_uri_parse_scheme (uri);

  if (scheme == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid URI", uri);
      goto OUT;
    }
  else if (g_ascii_strcasecmp (scheme, "xmpp") == 0)
    {
      gchar *node = NULL;
      gchar *domain = NULL;
      gchar *resource = NULL;

      if (!gabble_parse_xmpp_uri (uri, &node, &domain, &resource, error))
        goto OUT;

      normalized_jid = gabble_encode_jid (node, domain, resource);

      g_free (node);
      g_free (domain);
      g_free (resource);
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

  return normalized_jid;
}

gchar *
gabble_jid_to_uri (const gchar *scheme,
    const gchar *jid,
    GError **error)
{
  gchar *normalized_uri = NULL;
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;
  gchar *escaped_node = NULL;
  gchar *escaped_domain = NULL;
  gchar *escaped_resource = NULL;
  gchar *escaped_jid = NULL;
  gchar *normalized_scheme = NULL;

  g_return_val_if_fail (scheme != NULL, NULL);

  if (!wocky_decode_jid (jid, &node, &domain, &resource))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid JID", jid);
      return NULL;
    }

  /* convert from "foo?" to "foo%3F" */
  if (node)
    escaped_node = g_uri_escape_string (node, NULL, TRUE);

  g_assert (domain != NULL);
  escaped_domain = g_uri_escape_string (domain, NULL, TRUE);

  if (resource)
    escaped_resource = g_uri_escape_string (resource, NULL, TRUE);

  escaped_jid = gabble_encode_jid (escaped_node, escaped_domain, escaped_resource);

  normalized_scheme = g_ascii_strdown (scheme, -1);

  normalized_uri = g_strdup_printf ("%s:%s", normalized_scheme, escaped_jid);

  g_free (node);
  g_free (domain);
  g_free (resource);
  g_free (escaped_node);
  g_free (escaped_domain);
  g_free (escaped_resource);
  g_free (escaped_jid);
  g_free (normalized_scheme);

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
gabble_normalize_vcard_address (const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error)
{
  gchar *normalized_jid = NULL;
  gchar *normalized_address = NULL;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (vcard_address != NULL, NULL);

  normalized_jid = gabble_vcard_address_to_jid (vcard_field, vcard_address, error);
  if (normalized_jid == NULL)
    {
      goto OUT;
    }

  normalized_address = gabble_jid_to_vcard_address (vcard_field, normalized_jid, error);

OUT:
  g_free (normalized_jid);

  return normalized_address;
}

gchar *
gabble_vcard_address_to_jid (const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error)
{
  gchar *normalized_jid = NULL;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (vcard_address != NULL, NULL);

  if (g_ascii_strcasecmp (vcard_field, "x-jabber") == 0)
    {
      GError *gabble_error = NULL;

      normalized_jid = gabble_normalize_contact (NULL,
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
  else if (g_ascii_strcasecmp (vcard_field, "x-facebook-id") == 0)
    {
      const gchar *s;

      s = vcard_address;
      while (*s && (g_ascii_isdigit (*s)))
        s++;
      if (G_UNLIKELY (*s != '\0'))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is an invalid facebook chat address", vcard_address);
          goto OUT;
        }

      normalized_jid = g_strdup_printf ("-%s@chat.facebook.com", vcard_address);
    }
  else
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "'%s' vCard field is not supported by this protocol", vcard_field);
    }

OUT:
  return normalized_jid;
}

gchar *
gabble_jid_to_vcard_address (const gchar *vcard_field,
    const gchar *jid,
    GError **error)
{
  gchar *normalized_address = NULL;

  g_return_val_if_fail (vcard_field != NULL, NULL);
  g_return_val_if_fail (jid != NULL, NULL);

  if (g_ascii_strcasecmp (vcard_field, "x-jabber") == 0)
    {
      GError *gabble_error = NULL;

      normalized_address = gabble_normalize_contact (NULL,
          jid, GUINT_TO_POINTER (GABBLE_JID_GLOBAL),
          &gabble_error);

      if (gabble_error != NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is an invalid address: %s", jid,
              gabble_error->message);
          g_error_free (gabble_error);
        }
    }
  else if (g_ascii_strcasecmp (vcard_field, "x-facebook-id") == 0)
    {
      gchar *address = g_ascii_strdown (jid, -1);

      if (address[0] == '-' &&
          g_str_has_suffix (address, "@chat.facebook.com"))
        {
          const gchar *at = strchr (address, '@');
          const gchar *start_of_number = address + 1;
          const gchar *s;

          g_assert (at != NULL);

          normalized_address = g_strndup (start_of_number, (int) (at - start_of_number));

          s = normalized_address;
          while (*s && (g_ascii_isdigit (*s)))
            s++;
          if (G_UNLIKELY (*s != '\0'))
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "'%s' is an invalid facebook chat address", jid);
            }
        }
      else
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is an invalid facebook chat address", jid);
        }

      g_free (address);
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
  gchar *normalized_jid;
  TpHandle handle;

  normalized_jid = gabble_vcard_address_to_jid (vcard_field, vcard_address, error);
  if (normalized_jid == NULL)
    return 0;

  handle = tp_handle_ensure (repo, normalized_jid, NULL, error);

  g_free (normalized_jid);

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
  const gchar *identifier = tp_handle_inspect (contact_repo, contact);
  return gabble_jid_to_vcard_address (vcard_field, identifier, NULL);
}

gchar *
gabble_uri_for_handle (TpHandleRepoIface *contact_repo,
    const gchar *scheme,
    TpHandle contact)
{
  const gchar *identifier = tp_handle_inspect (contact_repo, contact);
  return gabble_jid_to_uri (scheme, identifier, NULL);
}

gboolean
gabble_parse_xmpp_uri (const gchar *uri,
    gchar **node,
    gchar **domain,
    gchar **resource,
    GError **error)
{
  gboolean ret = FALSE;
  gchar *scheme;
  const gchar *jid;
  gchar *tmp_node = NULL;
  gchar *tmp_domain = NULL;
  gchar *tmp_resource = NULL;
  gchar *unescaped_node = NULL;
  gchar *unescaped_domain = NULL;
  gchar *unescaped_resource = NULL;
  gchar *unescaped_jid = NULL;
  gchar *normalized_jid = NULL;
  GError *gabble_error = NULL;

  g_return_val_if_fail (uri != NULL, FALSE);
  g_return_val_if_fail (domain != NULL, FALSE);

  scheme = g_uri_parse_scheme (uri);

  if (scheme == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid URI", uri);
      goto OUT;
    }

  jid = uri + strlen (scheme) + 1; /* Strip the scheme */

  if (!wocky_decode_jid (jid, &tmp_node, &tmp_domain, &tmp_resource))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid XMPP URI", uri);
      goto OUT;
    }

  /* convert from "foo%3F" to "foo?" */
  if (tmp_node)
    {
      unescaped_node = g_uri_unescape_string (tmp_node, NULL);
      if (unescaped_node == NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is not a valid XMPP URI", uri);
          goto OUT;
        }
    }

  g_assert (tmp_domain);
  unescaped_domain = g_uri_unescape_string (tmp_domain, NULL);
  if (unescaped_domain == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid XMPP URI", uri);
      goto OUT;
    }

  if (tmp_resource)
    {
      unescaped_resource = g_uri_unescape_string (tmp_resource, NULL);
      if (unescaped_resource == NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "'%s' is not a valid XMPP URI", uri);
          goto OUT;
        }
    }

  unescaped_jid = gabble_encode_jid (unescaped_node, unescaped_domain, unescaped_resource);

  normalized_jid = gabble_normalize_contact (NULL, unescaped_jid,
      GUINT_TO_POINTER (GABBLE_JID_GLOBAL), &gabble_error);

  if (gabble_error != NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid XMPP URI: %s", uri,
          gabble_error->message);
      g_error_free (gabble_error);
      goto OUT;
    }

  if (!wocky_decode_jid (normalized_jid, node, domain, resource))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' is not a valid XMPP URI", uri);
      goto OUT;
    }

  ret = TRUE;

OUT:
  g_free (scheme);
  g_free (tmp_node);
  g_free (tmp_domain);
  g_free (tmp_resource);
  g_free (unescaped_node);
  g_free (unescaped_domain);
  g_free (unescaped_resource);
  g_free (unescaped_jid);
  g_free (normalized_jid);
  return ret;
}
