/*
 * caps-hash.h - caps computing string hash (XEP-0115 v1.5) API available to telepathy-gabble plugins
 * Copyright (C) 2008-2010 Collabora Ltd.
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

#ifndef GABBLE_PLUGINS_CAPS_HASH_H
#define GABBLE_PLUGINS_CAPS_HASH_H

#include <glib.h>

#include <gabble/capabilities-set.h>

G_BEGIN_DECLS

gchar *gabble_caps_hash_compute_full (const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities, GPtrArray *data_forms)
    G_GNUC_WARN_UNUSED_RESULT;

gchar *gabble_caps_hash_compute (const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif
