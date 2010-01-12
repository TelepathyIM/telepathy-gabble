
#ifndef __GABBLE_CAPS_CACHE_H
#define __GABBLE_CAPS_CACHE_H

#include <glib-object.h>

#include <capabilities.h>

G_BEGIN_DECLS

#define GABBLE_TYPE_CAPS_CACHE gabble_caps_cache_get_type()

#define GABBLE_CAPS_CACHE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_CAPS_CACHE, \
        GabbleCapsCache))

#define GABBLE_CAPS_CACHE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_CAPS_CACHE, \
        GabbleCapsCacheClass))

#define GABBLE_IS_CAPS_CACHE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_CAPS_CACHE))

#define GABBLE_IS_CAPS_CACHE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_CAPS_CACHE))

#define GABBLE_CAPS_CACHE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CAPS_CACHE, \
        GabbleCapsCacheClass))

typedef struct _GabbleCapsCachePrivate GabbleCapsCachePrivate;

typedef struct
{
  GObject parent;
  GabbleCapsCachePrivate *priv;
} GabbleCapsCache;

typedef struct
{
  GObjectClass parent_class;
} GabbleCapsCacheClass;

GType
gabble_caps_cache_get_type (void);

GabbleCapsCache *
gabble_caps_cache_get_singleton (void);

GabbleCapabilitySet *
gabble_caps_cache_lookup (GabbleCapsCache *self, const gchar *node);

void
gabble_caps_cache_insert (
    GabbleCapsCache *cache,
    const gchar *node,
    GabbleCapabilitySet *set);

GabbleCapsCache *
gabble_caps_cache_new (void);

GabbleCapsCache *
gabble_caps_cache_dup_shared (void);

void
gabble_caps_cache_free_shared (void);

G_END_DECLS

#endif /* defined __GABBLE_CAPS_CACHE_H */
