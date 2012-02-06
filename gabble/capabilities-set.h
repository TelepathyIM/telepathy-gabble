/*
 * capabilities-set.h - capabilities set API available to telepathy-gabble plugins
 * Copyright (C) 2005-2010 Collabora Ltd.
 * Copyright (C) 2005-2010 Nokia Corporation
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

#ifndef GABBLE_PLUGINS_CAPABILITIES_SET_H
#define GABBLE_PLUGINS_CAPABILITIES_SET_H

#include <glib-object.h>

#include <wocky/wocky.h>

G_BEGIN_DECLS

/**
 * GabbleCapabilitySet:
 *
 * A set of capabilities.
 */
typedef struct _GabbleCapabilitySet GabbleCapabilitySet;

GabbleCapabilitySet *gabble_capability_set_new (void);
GabbleCapabilitySet *gabble_capability_set_new_from_stanza (
    WockyNode *query_result);
GabbleCapabilitySet *gabble_capability_set_copy (
    const GabbleCapabilitySet *caps);
void gabble_capability_set_update (GabbleCapabilitySet *target,
    const GabbleCapabilitySet *source);
void gabble_capability_set_add (GabbleCapabilitySet *caps,
    const gchar *cap);
gboolean gabble_capability_set_remove (GabbleCapabilitySet *caps,
    const gchar *cap);
void gabble_capability_set_exclude (GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *removed);
void gabble_capability_set_intersect (GabbleCapabilitySet *target,
    const GabbleCapabilitySet *source);
gint gabble_capability_set_size (const GabbleCapabilitySet *caps);
gboolean gabble_capability_set_has (const GabbleCapabilitySet *caps,
    const gchar *cap);
gboolean gabble_capability_set_has_one (const GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *alternatives);
gboolean gabble_capability_set_at_least (const GabbleCapabilitySet *caps,
    const GabbleCapabilitySet *query);
gboolean gabble_capability_set_equals (const GabbleCapabilitySet *a,
    const GabbleCapabilitySet *b);
void gabble_capability_set_clear (GabbleCapabilitySet *caps);
void gabble_capability_set_free (GabbleCapabilitySet *caps);
void gabble_capability_set_foreach (const GabbleCapabilitySet *caps,
    GFunc func, gpointer user_data);
gchar *gabble_capability_set_dump (const GabbleCapabilitySet *caps,
    const gchar *indent);
gchar *gabble_capability_set_dump_diff (const GabbleCapabilitySet *old_caps,
    const GabbleCapabilitySet *new_caps,
    const gchar *indent);

typedef gboolean (*GabbleCapabilitySetPredicate) (
    const GabbleCapabilitySet *set, gconstpointer user_data);
/* These functions are compatible with GabbleCapabilitySetPredicate;
 * pass in the desired capabilities as the user_data */
#define gabble_capability_set_predicate_equals \
  ((GabbleCapabilitySetPredicate) gabble_capability_set_equals)
#define gabble_capability_set_predicate_has \
  ((GabbleCapabilitySetPredicate) gabble_capability_set_has)
#define gabble_capability_set_predicate_has_one \
  ((GabbleCapabilitySetPredicate) gabble_capability_set_has_one)
#define gabble_capability_set_predicate_at_least \
  ((GabbleCapabilitySetPredicate) gabble_capability_set_at_least)

G_END_DECLS

#endif
