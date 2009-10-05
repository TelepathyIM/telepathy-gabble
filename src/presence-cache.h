/*
 * gabble-presence-cache.h - Headers for Gabble's contact presence cache
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

#ifndef __GABBLE_PRESENCE_CACHE_H__
#define __GABBLE_PRESENCE_CACHE_H__

#include <glib-object.h>

#include "presence.h"

G_BEGIN_DECLS

#define GABBLE_TYPE_PRESENCE_CACHE gabble_presence_cache_get_type ()

#define GABBLE_PRESENCE_CACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  GABBLE_TYPE_PRESENCE_CACHE, GabblePresenceCache))

#define GABBLE_PRESENCE_CACHE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  GABBLE_TYPE_PRESENCE_CACHE, GabblePresenceCacheClass))

#define GABBLE_IS_PRESENCE_CACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  GABBLE_TYPE_PRESENCE_CACHE))

#define GABBLE_IS_PRESENCE_CACHE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  GABBLE_TYPE_PRESENCE_CACHE))

#define GABBLE_PRESENCE_CACHE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  GABBLE_TYPE_PRESENCE_CACHE, GabblePresenceCacheClass))

#define JABBER_PRESENCE_SHOW_AWAY "away"
#define JABBER_PRESENCE_SHOW_CHAT "chat"
#define JABBER_PRESENCE_SHOW_DND "dnd"
#define JABBER_PRESENCE_SHOW_XA "xa"

typedef struct _GabblePresenceCachePrivate GabblePresenceCachePrivate;

struct _GabblePresenceCache {
    GObject parent;
    GabblePresenceCachePrivate *priv;
};

typedef struct _GabblePresenceCacheClass GabblePresenceCacheClass;

struct _GabblePresenceCacheClass {
    GObjectClass parent_class;
};

GType gabble_presence_cache_get_type (void);

GabblePresenceCache *gabble_presence_cache_new (GabbleConnection *conn);
GabblePresence *gabble_presence_cache_get (GabblePresenceCache *cache,
    TpHandle handle);
void gabble_presence_cache_update (GabblePresenceCache *cache,
    TpHandle handle, const gchar *resource, GabblePresenceId presence_id,
    const gchar *status_message, gint8 priority);
void gabble_presence_cache_update_many (GabblePresenceCache *cache,
    const GArray *contact_handles, const gchar *resource,
    GabblePresenceId presence_id, const gchar *status_message, gint8 priority);
void gabble_presence_cache_maybe_remove (GabblePresenceCache *cache,
    TpHandle handle);
void gabble_presence_cache_add_bundle_caps (GabblePresenceCache *cache,
    const gchar *node, GabblePresenceCapabilities caps);
void gabble_presence_cache_add_own_caps (
    GabblePresenceCache *cache,
    const gchar *ver,
    GabblePresenceCapabilities caps,
    GHashTable *contact_caps);
gboolean gabble_presence_cache_peek_own_caps (
    GabblePresenceCache *cache,
    const gchar *ver,
    GabblePresenceCapabilities *caps,
    GHashTable **contact_caps);
void gabble_presence_cache_really_remove (GabblePresenceCache *cache,
    TpHandle handle);

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * gabble_caps_channel_manager_free_capabilities */
void gabble_presence_cache_free_cache_entry (
    GHashTable *per_channel_manager_caps);

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * gabble_caps_channel_manager_copy_capabilities */
void gabble_presence_cache_copy_cache_entry (GHashTable **out,
    GHashTable *in);

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * gabble_caps_channel_manager_update_capabilities */
void gabble_presence_cache_update_cache_entry (GHashTable *out,
    GHashTable *in);

void gabble_presence_cache_contacts_added_to_olpc_view (
    GabblePresenceCache *cache, TpHandleSet *handles);

void gabble_presence_cache_contacts_removed_from_olpc_view (
    GabblePresenceCache *cache, TpHandleSet *handles);

gboolean gabble_presence_cache_caps_pending (GabblePresenceCache *cache,
    TpHandle handle);

gboolean gabble_presence_cache_is_unsure (GabblePresenceCache *cache);

void gabble_presence_cache_update_location (GabblePresenceCache *cache,
    TpHandle handle, GHashTable *location);
GHashTable* gabble_presence_cache_get_location (GabblePresenceCache *cache,
    TpHandle handle);

G_END_DECLS

#endif /* __GABBLE_PRESENCE_CACHE_H__ */

