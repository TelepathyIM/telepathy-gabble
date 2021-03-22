/* * roster-cache.c - Source for RosterCache
 * Copyright (C) 2017 Ruslan N. Marchenko
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "roster-cache.h"
#include "namespaces.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sqlite3.h>

#define DEBUG_FLAG GABBLE_DEBUG_ROSTER
#include "debug.h"

#define DB_USER_VERSION 2

G_DEFINE_TYPE (RosterCache, roster_cache, G_TYPE_OBJECT)

static RosterCache *shared_cache = NULL;

struct _RosterCachePrivate
{
  gchar *path;
  sqlite3 *db;
};

enum
{
  PROP_PATH = 1,
};

static void roster_cache_constructed (GObject *object);
static gboolean roster_cache_prepare (RosterCache *self,
    const gchar *sql, sqlite3_stmt **stmt);

static void
roster_cache_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  RosterCache *self = (RosterCache *) object;

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
roster_cache_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  RosterCache *self = (RosterCache *) object;

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
roster_cache_dispose (GObject *object)
{
  G_OBJECT_CLASS (roster_cache_parent_class)->dispose (object);
}

static void
roster_cache_finalize (GObject *object)
{
  RosterCache *self = ROSTER_CACHE (object);

  g_free (self->priv->path);
  self->priv->path = NULL;

  if (self->priv->db != NULL)
    {
      sqlite3_close (self->priv->db);
      self->priv->db = NULL;
    }

  G_OBJECT_CLASS (roster_cache_parent_class)->finalize (object);
}

static void
roster_cache_class_init (RosterCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (RosterCachePrivate));

  object_class->constructed = roster_cache_constructed;
  object_class->get_property = roster_cache_get_property;
  object_class->set_property = roster_cache_set_property;
  object_class->dispose = roster_cache_dispose;
  object_class->finalize = roster_cache_finalize;

  /**
   * RosterCache:path:
   *
   * The path on disk to the SQLite database where this
   * #RosterCache stores its information.
   */
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
  path = g_getenv ("ROSTER_CACHE");

  if (path != NULL)
    {
      dir = free_dir = g_path_get_dirname (path);
      ret = g_strdup (path);
    }
  else
    {
      dir = g_getenv ("WOCKY_CACHE_DIR");

      if (dir != NULL)
        {
          ret = g_build_path (G_DIR_SEPARATOR_S, dir, "roster-cache.db", NULL);
        }
      else
        {
          ret = g_build_path (G_DIR_SEPARATOR_S,
              g_get_user_cache_dir (), "telepathy", "gabble", "roster-cache.db",
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

static gboolean
roster_cache_check_db_version (RosterCache *self)
{
  gchar *error;
  sqlite3_stmt *stmt;
  guint version;
  gint ret;

  if (!roster_cache_prepare (self, "PRAGMA user_version;", &stmt))
    return FALSE;

  ret = sqlite3_step (stmt);

  if (ret == SQLITE_DONE)
    {
      DEBUG ("no roster db version, assuming new");
      version = 0;
    }
  else
  if (ret != SQLITE_ROW)
    {
      DEBUG ("pragma execution failed: %s", sqlite3_errmsg (self->priv->db));
      sqlite3_finalize (stmt);
      return FALSE;
    }
  else
    {
      version = sqlite3_column_int (stmt, 0);
      DEBUG ("roster db version %i opened", version);
    }
  sqlite3_finalize (stmt);

  switch (version)
    {
      case 0:
        DEBUG ("Creating roster cache database at %s", self->priv->path);
        ret = sqlite3_exec (self->priv->db,
          "CREATE TABLE IF NOT EXISTS users (\n"
          "  user TEXT NOT NULL PRIMARY KEY ON CONFLICT REPLACE,\n"
          "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,\n"
          "  version TEXT\n"
          ")", NULL, NULL, &error);

        if (ret != SQLITE_OK)
          {
            DEBUG ("failed to ensure table users exists: %s", error);
            sqlite3_free (error);
            return FALSE;
          }
        ret = sqlite3_exec (self->priv->db,
          "CREATE TABLE IF NOT EXISTS roster (\n"
          "  user TEXT NOT NULL REFERENCES users(user)\n"
          "       ON DELETE CASCADE ON UPDATE CASCADE,\n"
          "  jid text NOT NULL,\n"
          "  subscription int,\n"
          "  name text,\n"
          "  ver text NOT NULL,\n"
          " PRIMARY KEY (user, jid) ON CONFLICT REPLACE\n"
          ")", NULL, NULL, &error);

        if (ret != SQLITE_OK)
          {
            DEBUG ("failed to ensure table roster exists: %s", error);
            sqlite3_free (error);
            return FALSE;
          }
        ret = sqlite3_exec (self->priv->db,
          "CREATE TABLE IF NOT EXISTS groups (\n"
          "  user TEXT NOT NULL,\n"
          "  jid  TEXT NOT NULL,\n"
          "  grp  TEXT NOT NULL,\n"
          " PRIMARY KEY (user, jid, grp) ON CONFLICT REPLACE,\n"
          " FOREIGN KEY (user, jid) REFERENCES roster(user, jid)\n"
          "   ON DELETE CASCADE ON UPDATE CASCADE\n"
          ")", NULL, NULL, &error);

        if (ret != SQLITE_OK)
          {
            DEBUG ("failed to ensure table groups exists: %s", error);
            sqlite3_free (error);
            return FALSE;
          }
        return TRUE;

      case DB_USER_VERSION:
        DEBUG ("opened %s, user_version %u", self->priv->path, version);
        return TRUE;

      default:
        DEBUG ("%s is version %u, not our version %u; let's nuke it",
            self->priv->path, version, DB_USER_VERSION);
        return FALSE;
    }
}

static gboolean
roster_cache_open (RosterCache *self)
{
  gint ret;
  gchar *error;

  g_return_val_if_fail (self->priv->db == NULL, FALSE);

  ret = sqlite3_open (self->priv->path, &self->priv->db);

  if (ret != SQLITE_OK)
    {
      DEBUG ("opening database %s failed: %s", self->priv->path,
          sqlite3_errmsg (self->priv->db));
      goto err;
    }

  if (!roster_cache_check_db_version (self))
    goto err;

  ret = sqlite3_exec (self->priv->db,
      "PRAGMA user_version = " G_STRINGIFY (DB_USER_VERSION) ";"
      "PRAGMA journal_mode = MEMORY;"
      "PRAGMA synchronous = OFF",
      NULL, NULL, &error);

  if (ret != SQLITE_OK)
    {
      DEBUG ("failed to set user_version, turn off fsync() and "
          "turn off on-disk journalling: %s", error);
      sqlite3_free (error);
      goto err;
    }


  return TRUE;

 err:
  sqlite3_close (self->priv->db);
  self->priv->db = NULL;
  return FALSE;
}

static void
drop_and_open (RosterCache *self)
{
  int ret;

  g_return_if_fail (self->priv->path != NULL);
  if (self->priv->db != NULL)
    {
      sqlite3_close (self->priv->db);
      self->priv->db = NULL;
    }

  ret = unlink (self->priv->path);

  if (ret != 0)
    DEBUG ("removing roster cache failed: %s", g_strerror (errno));
  else
    roster_cache_open (self);
}

static void
roster_cache_constructed (GObject *object)
{
  RosterCache *self = ROSTER_CACHE (object);

  if (!roster_cache_open (self))
    {
      // Retry from scratch
      drop_and_open (self);
    }

  if (self->priv->db == NULL)
    {
      DEBUG ("couldn't open cache db; giving up");
      return;
    }
}

static void
roster_cache_init (RosterCache *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
      self, GABBLE_TYPE_ROSTER_CACHE, RosterCachePrivate);
}

/**
 * roster_cache_new:
 * @path: full path to where the cache SQLite database should be stored
 *
 * Convenience function to create a new #RosterCache.
 *
 * Returns: a new #RosterCache.
 */
static RosterCache *
roster_cache_new (const gchar *path)
{
  return g_object_new (GABBLE_TYPE_ROSTER_CACHE,
      "path", path,
      NULL);
}

/**
 * roster_cache_dup_shared:
 *
 * Returns a new or existing #RosterCache object.
 *
 * #RosterCache object is used as a singleton, one instance
 * is served for all gabble accounts. This function returns
 * a reference to singleton instance.
 *
 * Returns: a new, or cached, #RosterCache.
 */
RosterCache *
roster_cache_dup_shared (void)
{
  if (shared_cache == NULL)
    {
      gchar *path;

      path = get_path ();
      shared_cache = roster_cache_new (path);
      DEBUG ("roster-cache db at %s", path);
      g_free (path);
    }

  g_object_ref (shared_cache);
  return shared_cache;
}

/**
 * roster_cache_free_shared:
 *
 * Free the shared #RosterCache instance which was created by
 * calling roster_cache_dup_shared(), or do nothing if said
 * function was not called.
 */
void
roster_cache_free_shared (void)
{
  if (shared_cache != NULL)
    {
      g_object_unref (shared_cache);
      shared_cache = NULL;
    }
}

static gboolean
roster_cache_prepare (RosterCache *self,
    const gchar *sql,
    sqlite3_stmt **stmt)
{
  gint ret;

  g_return_val_if_fail (self->priv->db != NULL, FALSE);

  ret = sqlite3_prepare_v2 (self->priv->db, sql, -1, stmt, NULL);

  if (ret != SQLITE_OK)
    {
      g_warning ("preparing statement '%s' failed: %s", sql,
          sqlite3_errmsg (self->priv->db));
      return FALSE;
    }

  g_assert (stmt != NULL);
  return TRUE;
}

static gboolean
roster_cache_bind_long (RosterCache *self,
    sqlite3_stmt *stmt,
    gint param,
    glong val)
{
  gint ret = sqlite3_bind_int64 (stmt, param, val);

  if (ret != SQLITE_OK)
    {
      g_warning ("parameter binding failed: %s",
          sqlite3_errmsg (self->priv->db));
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
roster_cache_bind_text (RosterCache *self,
    sqlite3_stmt *stmt,
    gint param,
    gint len,
    const gchar *value)
{
  gint ret = sqlite3_bind_text (stmt, param, value, len, SQLITE_STATIC);

  if (ret != SQLITE_OK)
    {
      g_warning ("parameter binding failed: %s",
          sqlite3_errmsg (self->priv->db));
      sqlite3_finalize (stmt);
      return FALSE;
    }

  return TRUE;
}

static gboolean
roster_cache_select (RosterCache *self,
    sqlite3_stmt **stmt, const gchar *sql,
    const gchar *user, const gchar *jid)
{
  gint ret;

  if (!roster_cache_prepare (self, sql, stmt))
    return FALSE;

  if (!roster_cache_bind_text (self, *stmt, 1, -1, user))
    return FALSE;

  if (jid != NULL)
    if (!roster_cache_bind_text (self, *stmt, 2, -1, jid))
      return FALSE;

  ret = sqlite3_step (*stmt);

  if (ret == SQLITE_DONE)
    {
      DEBUG ("no roster data for user %s", user);
      sqlite3_finalize (*stmt);
      return FALSE;
    }

  if (ret != SQLITE_ROW)
    {
      DEBUG ("statement execution failed: %s",
          sqlite3_errmsg (self->priv->db));
      sqlite3_finalize (*stmt);
      return FALSE;
    }
  return TRUE;
}

static gboolean
roster_cache_insert (RosterCache *self,
    const gchar *sql,
    const gchar *user, const gchar *jid,
    const gchar *text, const int *pint,
    const gchar *last)
{
  sqlite3_stmt *stmt;
  gint ret = SQLITE_OK;

  if (!roster_cache_prepare (self, sql, &stmt))
    return FALSE;

  if (!roster_cache_bind_text (self, stmt, 1, -1, user))
    return FALSE;

  if (jid && !roster_cache_bind_text (self, stmt, 2, -1, jid))
    return FALSE;

  if (text && !roster_cache_bind_text (self, stmt, 3, -1, text))
    return FALSE;

  if (pint && !roster_cache_bind_long (self, stmt, 4, (glong)(*pint)))
    return FALSE;

  if (last && !roster_cache_bind_text (self, stmt, 5, -1, last))
    return FALSE;

  ret = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (ret == SQLITE_DONE)
    return TRUE;

  DEBUG ("statement execution failed: %s",
          sqlite3_errmsg (self->priv->db));

  if (ret == SQLITE_CORRUPT)
    drop_and_open (self);

  return FALSE;
}

/**
 * roster_cache_get_roster:
 * @self: a #RosterCache
 * @user: the account's bare jid whose roster to retrieve
 *
 * Returns: a #WockyNode representing roster if @user was
 * found in the cache, or %NULL if a no roster cached for
 * the @user.
 */
WockyNode *
roster_cache_get_roster (RosterCache *self,
    const gchar *user)
{
  sqlite3_stmt *stmt, *gst;
  const gchar *val;
  WockyNode *roster;

  if (!self->priv->db)
    return NULL;

  if (!roster_cache_select (self, &stmt,
        "SELECT version FROM users WHERE user=?", user, NULL))
    return NULL;

  val = (const gchar*)sqlite3_column_text (stmt, 0);
  DEBUG ("cached roster[%s] version: %s", user, val);

  roster = wocky_node_new ("query", NS_ROSTER);
  wocky_node_set_attribute (roster, "ver", val);

  sqlite3_finalize (stmt);

  if (!roster_cache_select (self, &stmt,
        "SELECT jid,name,subscription FROM roster WHERE user=?", user, NULL))
    return NULL;

  do
    {
      WockyNode *item;
      const guchar *name = sqlite3_column_text (stmt, 1);
      int sub = sqlite3_column_int (stmt, 2);
      val = (const gchar*)sqlite3_column_text (stmt, 0);
      DEBUG("Adding item %s[%s] sub=%d", val, name, sub);
      wocky_node_add_build (roster,
        '(', "item",
        '@', "jid", val,
        '@', "name", ((name != NULL)? name : (guchar *)""),
        '@', "subscription",
          ((sub & 3)?((sub & 1)?(sub & 2 ? "both":"to"):"from"):"none"),
        '*', &item,
        ')', NULL);
      if (sub & 8)
        wocky_node_set_attribute (item, "ask", "subscribe");
      if (sub & 0x10)
        wocky_node_set_attribute (item, "approved", "true");

      if (!roster_cache_select (self, &gst,
          "SELECT grp FROM groups WHERE user=? AND jid=?", user, val))
        continue;
      do
        {
          val = (const gchar*)sqlite3_column_text (gst, 0);
          wocky_node_add_child_with_content (item, "group", val);
        }
      while (sqlite3_step (gst) == SQLITE_ROW);
      sqlite3_finalize (gst);
    }
  while (sqlite3_step (stmt) == SQLITE_ROW);
  sqlite3_finalize (stmt);

  return roster;
}

gboolean
roster_cache_update_roster (RosterCache *self,
    const gchar *user, WockyNode *query)
{
  const gchar *ver, *val;
  WockyNodeIter iter, jter;
  WockyNode *item, *group;
  gint sub;

  if (self->priv->db == NULL)
      return FALSE;

  ver = wocky_node_get_attribute (query, "ver");

  if (ver == NULL || strlen (ver) == 0)
      return FALSE;

  DEBUG ("Updating cache for %s with version %s", user, ver);
  if (sqlite3_exec (self->priv->db, "BEGIN TRANSACTION",
                        NULL, NULL, (char **)&val) != SQLITE_OK)
    {
      DEBUG("cannot start roster cache version %s for %s: %s",
            ver, user, val);
      return FALSE;
    }

  if (!roster_cache_insert (self,
        "INSERT INTO users (user, version) VALUES (?, ?)",
        user, ver, NULL, NULL, NULL))
    return FALSE;

  wocky_node_iter_init (&iter, query, "item", NS_ROSTER);
  while (wocky_node_iter_next (&iter, &item))
    {
      const gchar *jid = wocky_node_get_attribute (item, "jid");

      val = wocky_node_get_attribute (item, "subscription");
      if (!wocky_strdiff (val, "remove"))
        {
          if (!roster_cache_insert (self,
              "DELETE FROM groups WHERE user=? AND jid=?",
              user, jid, NULL, NULL, NULL))
            goto err;

          if (!roster_cache_insert (self,
              "DELETE FROM roster WHERE user=? AND jid=?",
              user, jid, NULL, NULL, NULL))
            goto err;
        }
      else
        {
          sub = (!wocky_strdiff (val, "both")) ? 3 :
                  (!wocky_strdiff (val, "to")) ? 2 :
                    (!wocky_strdiff (val, "from")) ? 1 : 0;
          if (!wocky_strdiff ("subscribe",
                wocky_node_get_attribute (item, "ask")))
            sub |= 0x08;
          if (!wocky_strdiff ("true",
                wocky_node_get_attribute (item, "approve")))
            sub |= 0x10;

          if (!roster_cache_insert (self,
                "INSERT INTO roster(user, jid, name, subscription, ver)"
                            "VALUES(?, ?, ?, ?, ?)", user, jid,
                            wocky_node_get_attribute (item, "name"),
                            &sub, ver))
            {
              DEBUG ("cannot insert roster cache item %s/%s/%d/%s",
                      user, jid, sub, ver);
              continue;
            }
          DEBUG ("updated roster cache item %s/%s/%d/%s",
                  user, jid, sub, ver);
          if (!roster_cache_insert (self,
              "DELETE FROM groups WHERE user=? AND jid=?",
              user, jid, NULL, NULL, NULL))
            goto err;

          wocky_node_iter_init (&jter, item, "group", NS_ROSTER);
          while (wocky_node_iter_next (&jter, &group))
            {
              if (!roster_cache_insert (self,
                  "INSERT INTO groups(user, jid, grp) VALUES(?, ?, ?)",
                  user, jid, group->content, NULL, NULL))
                goto err;
            }
        }
    }

  if (sqlite3_exec (self->priv->db, "COMMIT", NULL, NULL,
                          (char **)&val) != SQLITE_OK)
    {
      DEBUG ("cannot commit cache roster version %s for %s: %s",
            user, ver, val);
      goto err;
    }
  return TRUE;

err:
  sqlite3_exec (self->priv->db, "ROLLBACK TRANSACTION", NULL, NULL, NULL);
  return FALSE;
}
/* vim: set sts=2 et: */
