/*
 * gabble-media-session-handler.h - Header for GabbleMediaSessionHandler
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

#ifndef __GABBLE_MEDIA_SESSION_HANDLER_H__
#define __GABBLE_MEDIA_SESSION_HANDLER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleMediaSessionHandler GabbleMediaSessionHandler;
typedef struct _GabbleMediaSessionHandlerClass GabbleMediaSessionHandlerClass;

struct _GabbleMediaSessionHandlerClass {
    GObjectClass parent_class;
};

struct _GabbleMediaSessionHandler {
    GObject parent;
};

GType gabble_media_session_handler_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_SESSION_HANDLER \
  (gabble_media_session_handler_get_type())
#define GABBLE_MEDIA_SESSION_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_SESSION_HANDLER, GabbleMediaSessionHandler))
#define GABBLE_MEDIA_SESSION_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_SESSION_HANDLER, GabbleMediaSessionHandlerClass))
#define GABBLE_IS_MEDIA_SESSION_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_SESSION_HANDLER))
#define GABBLE_IS_MEDIA_SESSION_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_SESSION_HANDLER))
#define GABBLE_MEDIA_SESSION_HANDLER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_SESSION_HANDLER, GabbleMediaSessionHandlerClass))


gboolean gabble_media_session_handler_error (GabbleMediaSessionHandler *obj, guint errno, const gchar * message, GError **error);
gboolean gabble_media_session_handler_ready (GabbleMediaSessionHandler *obj, GError **error);


G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_SESSION_HANDLER_H__*/
