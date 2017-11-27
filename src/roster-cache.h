/*
 * roster-cache.h - Header for RosterCache
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
#ifndef __ROSTER_CACHE_H__
#define __ROSTER_CACHE_H__

#include <glib-object.h>

#include "wocky.h"

G_BEGIN_DECLS

/**
 * RosterCache: typedefs
 */
typedef struct _RosterCache RosterCache;
typedef struct _RosterCacheClass RosterCacheClass;
typedef struct _RosterCachePrivate RosterCachePrivate;

/* TYPE_DEFS */
#define GABBLE_TYPE_ROSTER_CACHE roster_cache_get_type()
#define ROSTER_CACHE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_ROSTER_CACHE, \
        RosterCache))
#define ROSTER_CACHE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_ROSTER_CACHE, \
        RosterCacheClass))
#define GABBLE_IS_ROSTER_CACHE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_ROSTER_CACHE))
#define GABBLE_IS_ROSTER_CACHE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_ROSTER_CACHE))
#define ROSTER_CACHE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_ROSTER_CACHE, \
        RosterCacheClass))

struct _RosterCache
{
  GObject parent;
  RosterCachePrivate *priv;
};

struct _RosterCacheClass
{
  GObjectClass parent_class;
};

GType
roster_cache_get_type (void);

WockyNode *roster_cache_get_roster (RosterCache *self,
    const gchar *user);

gboolean roster_cache_update_roster (RosterCache *,
    const gchar *, WockyNode *);

RosterCache *
roster_cache_dup_shared (void);

void
roster_cache_free_shared (void);

G_END_DECLS

#endif /* ifndef __ROSTER_CACHE_H__ */
