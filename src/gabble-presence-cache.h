
#ifndef __GABBLE_PRESENCE_CACHE_H__
#define __GABBLE_PRESENCE_CACHE_H__

#include <glib-object.h>

#include "gabble-presence.h"
#include "handles.h"

G_BEGIN_DECLS

#define GABBLE_TYPE_PRESENCE_CACHE gabble_presence_cache_get_type()

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

struct _GabblePresenceCache {
    GObject parent;
    gpointer priv;
};

typedef struct _GabblePresenceCacheClass GabblePresenceCacheClass;

struct _GabblePresenceCacheClass {
    GObjectClass parent_class;
};

GType gabble_presence_cache_get_type (void);

GabblePresenceCache *gabble_presence_cache_new (GabbleConnection *conn);
GabblePresence *gabble_presence_cache_get (GabblePresenceCache *cache,
    GabbleHandle handle);
void gabble_presence_cache_update (GabblePresenceCache *cache,
    GabbleHandle handle, const gchar *resource, GabblePresenceId presence_id,
    const gchar *status_message, gint8 priority);
void gabble_presence_cache_maybe_remove (GabblePresenceCache *cache,
    GabbleHandle handle);

G_END_DECLS

#endif /* __GABBLE_PRESENCE_CACHE_H__ */

