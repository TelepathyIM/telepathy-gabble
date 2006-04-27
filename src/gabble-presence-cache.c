
#include "gabble-presence.h"

#include "gabble-presence-cache.h"

G_DEFINE_TYPE (GabblePresenceCache, gabble_presence_cache, G_TYPE_OBJECT);

#define GABBLE_PRESENCE_CACHE_PRIV(account) ((GabblePresenceCachePrivate *)account->priv)

typedef struct _GabblePresenceCachePrivate GabblePresenceCachePrivate;

struct _GabblePresenceCachePrivate
{
  GHashTable *presence;
};

GabblePresenceCache *
gabble_presence_cache_new (void)
{
  return g_object_new (GABBLE_TYPE_PRESENCE_CACHE, NULL);
}

static void
gabble_presence_cache_class_init (GabblePresenceCacheClass *klass)
{
  g_type_class_add_private (klass, sizeof (GabblePresenceCachePrivate));
}

static void
gabble_presence_cache_init (GabblePresenceCache *cache)
{
  cache->priv = G_TYPE_INSTANCE_GET_PRIVATE (cache,
      GABBLE_TYPE_PRESENCE_CACHE, GabblePresenceCachePrivate);
  ((GabblePresenceCachePrivate *)cache->priv)->presence = g_hash_table_new_full (
    NULL, NULL, NULL, g_object_unref);
}

GabblePresence *
gabble_presence_cache_get (GabblePresenceCache *cache, GabbleHandle handle)
{
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  return g_hash_table_lookup (priv->presence, GINT_TO_POINTER (handle));
}

void
gabble_presence_cache_update (GabblePresenceCache *cache, GabbleHandle handle, const gchar *resource, GabblePresenceId presence_id, const gchar *status_message)
{
  GabblePresence *presence = gabble_presence_cache_get (cache, handle);
  GabblePresenceCachePrivate *priv = GABBLE_PRESENCE_CACHE_PRIV (cache);

  if (presence == NULL)
    {
      presence = gabble_presence_new ();
      g_hash_table_insert (priv->presence, GINT_TO_POINTER (handle), presence);
    }

  gabble_presence_update (presence, resource, presence_id, status_message);
}

