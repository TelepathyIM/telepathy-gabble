/*
 * jingle-share.h - Header for GabbleJingleShare
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __JINGLE_SHARE_H__
#define __JINGLE_SHARE_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>
#include "types.h"

#include "jingle-content.h"

G_BEGIN_DECLS

typedef struct _GabbleJingleShareClass GabbleJingleShareClass;

GType gabble_jingle_share_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_SHARE \
  (gabble_jingle_share_get_type ())
#define GABBLE_JINGLE_SHARE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_SHARE, \
                              GabbleJingleShare))
#define GABBLE_JINGLE_SHARE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_SHARE, \
                           GabbleJingleShareClass))
#define GABBLE_IS_JINGLE_SHARE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_SHARE))
#define GABBLE_IS_JINGLE_SHARE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_SHARE))
#define GABBLE_JINGLE_SHARE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_SHARE, \
                              GabbleJingleShareClass))

struct _GabbleJingleShareClass {
    GabbleJingleContentClass parent_class;
};

typedef struct _GabbleJingleSharePrivate GabbleJingleSharePrivate;

struct _GabbleJingleShare {
    GabbleJingleContent parent;
    GabbleJingleSharePrivate *priv;
};

typedef struct {
  gboolean folder;
  gboolean image;
  guint64 size;
  gchar *name;
  guint image_width;
  guint image_height;
} GabbleJingleShareManifestEntry;

typedef struct {
  gchar *source_url;
  gchar *preview_url;
  GList *entries;
} GabbleJingleShareManifest;

void jingle_share_register (GabbleJingleFactory *factory);

gchar *gabble_jingle_share_get_source_url (GabbleJingleShare *content);
gchar *gabble_jingle_share_get_preview_url (GabbleJingleShare *content);
GabbleJingleShareManifest *gabble_jingle_share_get_manifest (
    GabbleJingleShare *content);

#endif /* __JINGLE_SHARE_H__ */

