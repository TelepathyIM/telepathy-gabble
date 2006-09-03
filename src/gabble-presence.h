/*
 * gabble-presence.h - Headers for Gabble's per-contact presence structure
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


#ifndef __GABBLE_PRESENCE_H__
#define __GABBLE_PRESENCE_H__

#include <glib-object.h>

#include "gabble-connection.h"

G_BEGIN_DECLS

#define GABBLE_TYPE_PRESENCE gabble_presence_get_type()

#define GABBLE_PRESENCE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                   GABBLE_TYPE_PRESENCE, GabblePresence))

#define GABBLE_PRESENCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), \
                                GABBLE_TYPE_PRESENCE, GabblePresenceClass))

#define GABBLE_IS_PRESENCE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                   GABBLE_TYPE_PRESENCE))

#define GABBLE_IS_PRESENCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                                GABBLE_TYPE_PRESENCE))

#define GABBLE_PRESENCE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                                  GABBLE_TYPE_PRESENCE, GabblePresenceClass))

typedef enum {
    PRESENCE_CAP_NONE = 0,
    PRESENCE_CAP_GOOGLE_TRANSPORT_P2P = 1 << 0,
    PRESENCE_CAP_GOOGLE_VOICE = 1 << 1,
    PRESENCE_CAP_JINGLE = 1 << 2,
    PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO = 1 << 3,
    PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO = 1 << 4,
} GabblePresenceCapabilities;

typedef struct _GabblePresence GabblePresence;

struct _GabblePresence {
    GObject parent;
    GabblePresenceCapabilities caps;
    GabblePresenceId status;
    gchar *status_message;
    gchar *nickname;
    gboolean keep_unavailable;
    gpointer priv;
};

typedef struct _GabblePresenceClass GabblePresenceClass;

struct _GabblePresenceClass {
    GObjectClass parent_class;
};

GType gabble_presence_get_type (void);

GabblePresence* gabble_presence_new (void);

gboolean gabble_presence_update (GabblePresence *presence, const gchar *resource, GabblePresenceId status, const gchar *status_message, gint8 priority);

void gabble_presence_set_capabilities (GabblePresence *presence, const gchar *resource, GabblePresenceCapabilities caps);

const gchar *gabble_presence_pick_resource_by_caps (GabblePresence *presence, GabblePresenceCapabilities caps);

gboolean
gabble_presence_resource_has_caps (GabblePresence *presence,
                                   const gchar *resource,
                                   GabblePresenceCapabilities caps);

LmMessage *gabble_presence_as_message (GabblePresence *presence, const gchar *resource);

G_END_DECLS

#endif /* __GABBLE_PRESENCE_H__ */

