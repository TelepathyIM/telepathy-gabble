
#include "config.h"
#include "caps-cache.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE
#include "debug.h"

G_DEFINE_TYPE (GabbleCapsCache, gabble_caps_cache, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CAPS_CACHE, GabbleCapsCachePrivate))

static GabbleCapsCache *shared_cache = NULL;

struct _GabbleCapsCachePrivate
{
  gchar *path;
  sqlite3 *db;
  guint inserts;
};

enum
{
    PROP_PATH = 1,
};

static GObject *
gabble_caps_cache_constructor (
    GType type, guint n_props, GObjectConstructParam *props);

static void
gabble_caps_cache_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  GabbleCapsCache *self = (GabbleCapsCache *) object;

  switch (property_id)
    {
    case PROP_PATH:
      g_value_set_string (value, self->priv->path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gabble_caps_cache_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  GabbleCapsCache *self = (GabbleCapsCache *) object;

  switch (property_id)
    {
    case PROP_PATH:
      g_free (self->priv->path);
      self->priv->path = g_value_dup_string (value);
      break;
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
  GabbleCapsCache *self = GABBLE_CAPS_CACHE (object);

  g_free (self->priv->path);
  self->priv->path = NULL;

  if (self->priv->db != NULL)
    {
      sqlite3_close (self->priv->db);
      self->priv->db = NULL;
    }

  G_OBJECT_CLASS (gabble_caps_cache_parent_class)->finalize (object);
}

static void
gabble_caps_cache_class_init (GabbleCapsCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GabbleCapsCachePrivate));

  object_class->constructor = gabble_caps_cache_constructor;
  object_class->get_property = gabble_caps_cache_get_property;
  object_class->set_property = gabble_caps_cache_set_property;
  object_class->dispose = gabble_caps_cache_dispose;
  object_class->finalize = gabble_caps_cache_finalize;

  g_object_class_install_property (object_class, PROP_PATH,
      g_param_spec_string ("path", "Path", "The path to the cache", NULL,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));
}

static gchar *
get_path (void)
{
  gchar *free_dir = NULL, *ret;
  const gchar *dir, *path;

  /* This should still work if it's the magic value ":memory:". */
  path = g_getenv ("GABBLE_CAPS_CACHE");

  if (path != NULL)
    {
      dir = free_dir = g_path_get_dirname (path);
      ret = g_strdup (path);
    }
  else
    {
      dir = g_getenv ("GABBLE_CACHE_DIR");

      if (dir != NULL)
        {
          ret = g_build_path (G_DIR_SEPARATOR_S, dir, "caps-cache.db", NULL);
        }
      else
        {
          ret = g_build_path (G_DIR_SEPARATOR_S,
              g_get_user_cache_dir (), "telepathy", "gabble", "caps-cache.db",
              NULL);
          dir = free_dir = g_path_get_dirname (ret);
        }
    }

  /* Any errors are ignored here, on the basis that we'll find out the path is
   * duff when we try to open the database anyway.
   */
  g_mkdir_with_parents (dir, 0755);
  g_free (free_dir);
  return ret;
}

static GObject *
gabble_caps_cache_constructor (
    GType type, guint n_props, GObjectConstructParam *props)
{
  int ret;
  GabbleCapsCache *self;
  gchar *error;

  self = (GabbleCapsCache *) G_OBJECT_CLASS (gabble_caps_cache_parent_class)
      ->constructor (type, n_props, props);

  ret = sqlite3_open (self->priv->path, &self->priv->db);

  if (ret == SQLITE_OK)
    {
      DEBUG ("opened database at %s", self->priv->path);
    }
  else
    {
      DEBUG ("opening database failed: %s", sqlite3_errmsg (self->priv->db));

      /* Can't open it. Nuke it and try again. */
      sqlite3_close (self->priv->db);
      ret = unlink (self->priv->path);

      if (!ret)
        {
          DEBUG ("removing database failed: %s", strerror (ret));

          /* Can't open it or remove it. Just pretend it isn't there. */
          self->priv->db = NULL;
        }
      else
        {
          ret = sqlite3_open (self->priv->path, &self->priv->db);

          if (ret == SQLITE_OK)
            {
              DEBUG ("opened database at %s", self->priv->path);
            }
          else
            {
              DEBUG ("database open after remove failed: %s",
                  sqlite3_errmsg (self->priv->db));
              /* Can't open it after removing it. Just pretend it isn't there.
               */

              sqlite3_close (self->priv->db);
              self->priv->db = NULL;
            }
        }
    }

  ret = sqlite3_exec (self->priv->db,
      "CREATE TABLE IF NOT EXISTS capabilities (\n"
      "  node text PRIMARY KEY,\n"
      "  namespaces text,\n"
      "  timestamp int)", NULL, NULL, &error);

  if (ret != SQLITE_OK)
    {
      DEBUG ("failed to ensure table exists: %s", error);
      sqlite3_free (error);
      sqlite3_close (self->priv->db);
      self->priv->db = NULL;
    }

  return (GObject *) self;
}

static void
gabble_caps_cache_init (GabbleCapsCache *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
      self, GABBLE_TYPE_CAPS_CACHE, GabbleCapsCachePrivate);
}

GabbleCapsCache *
gabble_caps_cache_new (const gchar *path)
{
  return g_object_new (GABBLE_TYPE_CAPS_CACHE, "path", path, NULL);
}

GabbleCapsCache *
gabble_caps_cache_dup_shared (void)
{
  if (shared_cache == NULL)
    {
      gchar *path;

      path = get_path ();
      shared_cache = gabble_caps_cache_new (path);
      g_free (path);
    }

  g_object_ref (shared_cache);
  return shared_cache;
}

void
gabble_caps_cache_free_shared (void)
{
  if (shared_cache != NULL)
    {
      g_object_unref (shared_cache);
      shared_cache = NULL;
    }
}

static gboolean
caps_cache_prepare (
    GabbleCapsCache *self,
    const gchar *sql,
    sqlite3_stmt **stmt)
{
  gint ret = sqlite3_prepare_v2 (self->priv->db, sql, -1, stmt, NULL);

  if (ret != SQLITE_OK)
    {
      DEBUG ("preparing statement failed: %s",
          sqlite3_errmsg (self->priv->db));
      return FALSE;
    }

  g_assert (stmt != NULL);
  return TRUE;
}

/* Finalizes @stmt if an error happens. */
static gboolean
caps_cache_bind_int (
    GabbleCapsCache *self,
    sqlite3_stmt *stmt,
    gint param,
    gint value)
{
  gint ret = sqlite3_bind_int (stmt, param, value);

  if (ret != SQLITE_OK)
    {
      DEBUG ("parameter binding failed: %s", sqlite3_errmsg (self->priv->db));
      sqlite3_finalize (stmt);
      return FALSE;
    }

  return TRUE;
}

/* Finalizes @stmt if an error happens.
 *
 * Note: the parameter is bound statically, so it mustn't be freed before the
 * statment is finalized.
 */
static gboolean
caps_cache_bind_text (
    GabbleCapsCache *self,
    sqlite3_stmt *stmt,
    gint param,
    gint len,
    const gchar *value)
{
  gint ret = sqlite3_bind_text (stmt, param, value, len, SQLITE_STATIC);

  if (ret != SQLITE_OK)
    {
      DEBUG ("parameter binding failed: %s", sqlite3_errmsg (self->priv->db));
      sqlite3_finalize (stmt);
      return FALSE;
    }

  return TRUE;
}

/* Update cache entry timestmp. */
static void
caps_cache_touch (GabbleCapsCache *self, const gchar *node)
{
  gint ret;
  sqlite3_stmt *stmt;

  if (!caps_cache_prepare (self,
        "UPDATE capabilities SET timestamp=? WHERE node=?", &stmt))
    return;

  if (!caps_cache_bind_int (self, stmt, 1, time (NULL)))
    return;

  if (!caps_cache_bind_text (self, stmt, 2, -1, node))
    return;

  ret = sqlite3_step (stmt);

  if (ret != SQLITE_DONE)
    {
      DEBUG ("statement execution failed: %s",
          sqlite3_errmsg (self->priv->db));
    }

  sqlite3_finalize (stmt);
}

/* Caller is responsible for freeing the returned set.
 */
GabbleCapabilitySet *
gabble_caps_cache_lookup (GabbleCapsCache *self, const gchar *node)
{
  gint ret;
  const gchar *value = NULL;
  sqlite3_stmt *stmt;
  gchar **i, **uris;
  GabbleCapabilitySet *caps;

  if (!self->priv->db)
    /* DB open failed. */
    return NULL;

  if (!caps_cache_prepare (self,
        "SELECT namespaces FROM capabilities WHERE node=?", &stmt))
    return NULL;

  if (!caps_cache_bind_text (self, stmt, 1, -1, node))
    return NULL;

  ret = sqlite3_step (stmt);

  if (ret == SQLITE_DONE)
    {
      /* No result. */
      DEBUG ("caps cache miss: %s", node);
      sqlite3_finalize (stmt);
      return NULL;
    }

  if (ret != SQLITE_ROW)
    {
      DEBUG ("statement execution failed: %s",
          sqlite3_errmsg (self->priv->db));
      sqlite3_finalize (stmt);
      return NULL;
    }

  DEBUG ("caps cache hit: %s", node);
  sqlite3_column_bytes (stmt, 0);
  value = (gchar *) sqlite3_column_text (stmt, 0);
  uris = g_strsplit (value, "\n", 0);
  caps = gabble_capability_set_new ();

  for (i = uris; *i != NULL; i++)
      gabble_capability_set_add (caps, *i);

  g_strfreev (uris);
  sqlite3_finalize (stmt);
  caps_cache_touch (self, node);
  return caps;
}

static void
append_uri (gchar *uri, GPtrArray *array)
{
  g_ptr_array_add (array, uri);
}

static void
caps_cache_insert (
    GabbleCapsCache *self,
    const gchar *node,
    GabbleCapabilitySet *caps)
{
  gchar *val;
  gint ret;
  sqlite3_stmt *stmt;
  GPtrArray *uris;

  if (!caps_cache_prepare (self,
        "INSERT INTO capabilities (node, namespaces, timestamp) "
        "VALUES (?, ?, ?)", &stmt))
    return;

  if (!caps_cache_bind_text (self, stmt, 1, -1, node))
    return;

  /* The plus one is for the terminating NULL. */
  uris = g_ptr_array_sized_new (gabble_capability_set_size (caps) + 1);
  gabble_capability_set_foreach (caps, (GFunc) append_uri, uris);
  g_ptr_array_add (uris, NULL);
  val = g_strjoinv ("\n", (gchar **) uris->pdata);

  if (!caps_cache_bind_text (self, stmt, 2, -1, val))
    goto OUT;

  if (!caps_cache_bind_int (self, stmt, 3, time (NULL)))
    goto OUT;

  ret = sqlite3_step (stmt);

  if (ret == SQLITE_CONSTRAINT)
    /* Presumably the error is because the key already exists. Ignore it. */
    goto OUT;

  if (ret != SQLITE_DONE)
    {
      DEBUG ("statement execution failed: %s",
          sqlite3_errmsg (self->priv->db));
    }

OUT:
  sqlite3_finalize (stmt);
  g_ptr_array_free (uris, TRUE);
  g_free (val);
}

static gboolean
caps_cache_count_entries (GabbleCapsCache *self, guint *count)
{
  gint ret;
  sqlite3_stmt *stmt;

  if (!self->priv->db)
    return FALSE;

  if (!caps_cache_prepare (self, "SELECT COUNT(*) FROM capabilities", &stmt))
    return FALSE;

  ret = sqlite3_step (stmt);

  if (ret != SQLITE_ROW)
    {
      DEBUG ("statement execution failed: %s",
          sqlite3_errmsg (self->priv->db));
      sqlite3_finalize (stmt);
      return FALSE;
    }

  *count = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);
  return TRUE;
}

/* If the number of entries is above @high_threshold, remove entries older
 * than @max_age while the cache is bigger than @low_threshold.
 */
static void
caps_cache_gc (
    GabbleCapsCache *self,
    guint high_threshold,
    guint low_threshold)
{
  gint ret;
  guint count;
  sqlite3_stmt *stmt;

  if (!caps_cache_count_entries (self, &count))
    return;

  if (count <= high_threshold)
    return;

  /* This emulates DELETE ... ORDER ... LIMIT because some Sqlites (e.g.
   * Debian) ship without SQLITE_ENABLE_UPDATE_DELETE_LIMIT unabled.
   */

  if (!caps_cache_prepare (self,
        "DELETE FROM capabilities WHERE oid IN ("
        "  SELECT oid FROM capabilities"
        "    ORDER BY timestamp ASC, oid ASC"
        "    LIMIT ?)", &stmt))
    return;

  if (!caps_cache_bind_int (self, stmt, 1, count - low_threshold))
    return;

  ret = sqlite3_step (stmt);

  if (ret != SQLITE_DONE)
    {
      DEBUG ("statement execution failed: %s",
          sqlite3_errmsg (self->priv->db));
    }

  sqlite3_finalize (stmt);
  DEBUG ("cache reduced from %d to %d items",
      count, count - sqlite3_changes (self->priv->db));
}

static guint
get_size (void)
{
  static gboolean ready = FALSE;
  static guint size = 1000;

  if (G_UNLIKELY (!ready))
    {
      const gchar *str = g_getenv ("GABBLE_CAPS_CACHE_SIZE");

      if (str != NULL)
        {
          /* Ignoring return code; size will retain default value on failure.
           */
          sscanf (str, "%u", &size);
        }

      ready = TRUE;
      /* DEBUG ("caps cache size = %d", size); */
    }

  return size;
}

void
gabble_caps_cache_insert (
    GabbleCapsCache *self,
    const gchar *node,
    GabbleCapabilitySet *caps)
{
  guint size = get_size ();

  if (!self->priv->db)
    /* DB open failed. */
    return;

  DEBUG ("caps cache insert: %s", node);
  caps_cache_insert (self, node, caps);

  /* Remove old entries after every 50th insert. */

  if (self->priv->inserts % 50 == 0)
    caps_cache_gc (self, size, MAX (1, 0.95 * size));

  self->priv->inserts++;
}

