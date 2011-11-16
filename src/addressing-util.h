/*
 * addressing-util.c - Headers for Gabble addressing utility functions
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

#ifndef __GABBLE_UTIL_ADDRESSING_H__
#define __GABBLE_UTIL_ADDRESSING_H__

#include <telepathy-glib/handle-repo-dynamic.h>

const gchar * const * gabble_get_addressable_uri_schemes (void);

const gchar * const * gabble_get_addressable_vcard_fields (void);

gchar * gabble_normalize_uri (const gchar *uri,
    GError **error);
gchar * gabble_uri_to_jid (const gchar *uri,
    GError **error);
gchar * gabble_jid_to_uri (const gchar *scheme,
    const gchar *jid,
    GError **error);

TpHandle gabble_ensure_handle_from_uri (TpHandleRepoIface *repo,
    const gchar *uri,
    GError **error);

gchar * gabble_parse_vcard_address (const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error);

TpHandle gabble_ensure_handle_from_vcard_address (TpHandleRepoIface *repo,
    const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error);

gchar **gabble_uris_for_handle (TpHandleRepoIface *contact_repo,
    TpHandle contact);

GHashTable *gabble_vcard_addresses_for_handle (TpHandleRepoIface *contact_repo,
    TpHandle contact);

gchar *gabble_uri_for_handle (TpHandleRepoIface *contact_repo,
    const gchar *uri_scheme,
    TpHandle contact);

gchar *gabble_vcard_address_for_handle (TpHandleRepoIface *contact_repo,
    const gchar *vcard_field,
    TpHandle contact);

#endif /* __GABBLE_UTIL_ADDRESSING_H__ */
