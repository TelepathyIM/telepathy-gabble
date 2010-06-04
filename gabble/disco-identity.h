/*
 * disco-identity.h —  utility API representing a Disco Identity for telepathy-gabble plugins
 * Copyright © 2009 Collabora Ltd.
 * Copyright © 2009 Nokia Corporation
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

#ifndef GABBLE_PLUGINS_DISCO_IDENTITY_H
#define GABBLE_PLUGINS_DISCO_IDENTITY_H

#include <glib.h>

#include <gabble/types.h>

G_BEGIN_DECLS

GabbleDiscoIdentity *gabble_disco_identity_new (const gchar *category,
    const gchar *type, const gchar *lang, const gchar *name);

GabbleDiscoIdentity *gabble_disco_identity_copy (
    const GabbleDiscoIdentity *source);

const gchar *gabble_disco_identity_get_category (GabbleDiscoIdentity *identity);
const gchar *gabble_disco_identity_get_type (GabbleDiscoIdentity *identity);
const gchar *gabble_disco_identity_get_lang (GabbleDiscoIdentity *identity);
const gchar *gabble_disco_identity_get_name (GabbleDiscoIdentity *identity);

void gabble_disco_identity_free (GabbleDiscoIdentity *identity);

/* array of GabbleDiscoIdentity helper methods */
GPtrArray *gabble_disco_identity_array_copy (const GPtrArray *source);
void gabble_disco_identity_array_free (GPtrArray *arr);

G_END_DECLS

#endif
