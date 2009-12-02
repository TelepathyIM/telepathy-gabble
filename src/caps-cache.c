
#include "caps-cache.h"

G_DEFINE_TYPE (GabbleCapsCache, gabble_caps_cache, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CAPS_CACHE, GabbleCapsCachePrivate))

static GabbleCapsCache *shared_cache = NULL;

struct _GabbleCapsCachePrivate
{
  GHashTable *cache;
};

static void
gabble_caps_cache_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gabble_caps_cache_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gabble_caps_cache_dispose (GObject *object)
{
  G_OBJECT_CLASS (gabble_caps_cache_parent_class)->dispose (object);
}

static void
gabble_caps_cache_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_caps_cache_parent_class)->finalize (object);
}

static void
gabble_caps_cache_class_init (GabbleCapsCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GabbleCapsCachePrivate));

  object_class->get_property = gabble_caps_cache_get_property;
  object_class->set_property = gabble_caps_cache_set_property;
  object_class->dispose = gabble_caps_cache_dispose;
  object_class->finalize = gabble_caps_cache_finalize;
}

static void
gabble_caps_cache_init (GabbleCapsCache *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
      self, GABBLE_TYPE_CAPS_CACHE, GabbleCapsCachePrivate);

  self->priv->cache = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_strfreev);
}

GabbleCapsCache *
gabble_caps_cache_new (void)
{
  return g_object_new (GABBLE_TYPE_CAPS_CACHE, NULL);
}

GabbleCapsCache *
gabble_caps_cache_dup_shared (void)
{
  if (shared_cache == NULL)
    {
      shared_cache = gabble_caps_cache_new ();
    }

  g_object_ref (shared_cache);
  return shared_cache;
}

gchar **
gabble_caps_cache_lookup (GabbleCapsCache *self, const gchar *node)
{
  return g_strdupv (g_hash_table_lookup (self->priv->cache, node));
}

void
gabble_caps_cache_insert (
    GabbleCapsCache *self,
    const gchar *node,
    gchar **caps)
{
  GSList *old_caps;

  old_caps = g_hash_table_lookup (self->priv->cache, node);

  if (old_caps != NULL)
    {
      /* XXX: issue warning here? */
      return;
    }

  g_hash_table_insert (
      self->priv->cache, g_strdup (node), g_strdupv ((gchar **) caps));
}

