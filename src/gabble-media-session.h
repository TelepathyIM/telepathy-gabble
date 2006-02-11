/*
 * gabble-media-session.h - Header for GabbleMediaSession
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

#ifndef __GABBLE_MEDIA_SESSION_H__
#define __GABBLE_MEDIA_SESSION_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

G_BEGIN_DECLS

typedef struct _GabbleMediaSession GabbleMediaSession;
typedef struct _GabbleMediaSessionClass GabbleMediaSessionClass;

struct _GabbleMediaSessionClass {
    GObjectClass parent_class;
};

struct _GabbleMediaSession {
    GObject parent;
};

GType gabble_media_session_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_SESSION \
  (gabble_media_session_get_type())
#define GABBLE_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_SESSION, GabbleMediaSession))
#define GABBLE_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_SESSION, GabbleMediaSessionClass))
#define GABBLE_IS_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_SESSION))
#define GABBLE_IS_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_SESSION))
#define GABBLE_MEDIA_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_SESSION, GabbleMediaSessionClass))


gboolean gabble_media_session_error (GabbleMediaSession *obj, guint errno, const gchar * message, GError **error);
gboolean gabble_media_session_ready (GabbleMediaSession *obj, GError **error);

gboolean gabble_media_session_dispatch_action (GabbleMediaSession *session,
                                               const gchar *action,
                                               LmMessageNode *session_node);

LmMessage *gabble_media_session_message_new (GabbleMediaSession *session,
                                             const gchar *action,
                                             LmMessageNode **session_node);
void gabble_media_session_message_send (GabbleMediaSession *session,
                                        LmMessage *msg);

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_SESSION_H__*/
