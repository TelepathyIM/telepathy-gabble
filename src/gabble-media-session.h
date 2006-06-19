/*
 * gabble-media-session.h - Header for GabbleMediaSession
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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
#include "gabble-media-stream.h"

G_BEGIN_DECLS

typedef enum
{
  MODE_GOOGLE,
  MODE_JINGLE
} GabbleMediaSessionMode;

typedef enum {
    JS_STATE_PENDING_CREATED = 0,
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    JS_STATE_ENDED
} JingleSessionState;

typedef enum {
    DEBUG_MSG_INFO = 0,
    DEBUG_MSG_DUMP,
    DEBUG_MSG_WARNING,
    DEBUG_MSG_ERROR,
    DEBUG_MSG_EVENT
} DebugMessageType;

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

/* CONVENIENCE MACROS */
#define MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL(s, m) \
  G_STMT_START { \
  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT) \
    { \
      GMS_DEBUG_ERROR (s, m); \
      HANDLER_DEBUG (sent_msg->node, "message sent"); \
      HANDLER_DEBUG (reply_msg->node, "message reply"); \
      _gabble_media_session_terminate (s); \
      return LM_HANDLER_RESULT_REMOVE_MESSAGE; \
    } \
  } G_STMT_END

gboolean gabble_media_session_error (GabbleMediaSession *obj, guint errno, const gchar * message, GError **error);
gboolean gabble_media_session_ready (GabbleMediaSession *obj, GError **error);

void _gabble_media_session_handle_action (GabbleMediaSession *session,
                                          LmMessage *message,
                                          LmMessageNode *session_node,
                                          const gchar *action);

LmMessage *_gabble_media_session_message_new (GabbleMediaSession *session,
                                              const gchar *action,
                                              LmMessageNode **session_node);

void _gabble_media_session_accept (GabbleMediaSession *session);
void _gabble_media_session_terminate (GabbleMediaSession *session);

void _gabble_media_session_stream_state (GabbleMediaSession *sess, guint state);

#ifndef _GMS_DEBUG_LEVEL
#define _GMS_DEBUG_LEVEL 2
#endif

#if _GMS_DEBUG_LEVEL

#define GMS_DEBUG_INFO(s, ...)    _gabble_media_session_debug (s, DEBUG_MSG_INFO, __VA_ARGS__)
#if _GMS_DEBUG_LEVEL > 1
#define GMS_DEBUG_DUMP(s, ...)    _gabble_media_session_debug (s, DEBUG_MSG_DUMP, __VA_ARGS__)
#else
#define GMS_DEBUG_DUMP(s, ...)
#endif
#define GMS_DEBUG_WARNING(s, ...) _gabble_media_session_debug (s, DEBUG_MSG_WARNING, __VA_ARGS__)
#define GMS_DEBUG_ERROR(s, ...)   _gabble_media_session_debug (s, DEBUG_MSG_ERROR, __VA_ARGS__)
#define GMS_DEBUG_EVENT(s, ...)   _gabble_media_session_debug (s, DEBUG_MSG_EVENT, __VA_ARGS__)

void _gabble_media_session_debug (GabbleMediaSession *session,
                                  DebugMessageType type,
                                  const gchar *format, ...);

#else

#define GMS_DEBUG_INFO(s, ...)
#define GMS_DEBUG_DUMP(s, ...)
#define GMS_DEBUG_WARNING(s, ...)
#define GMS_DEBUG_ERROR(s, ...)
#define GMS_DEBUG_EVENT(s, ...)

#endif

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_SESSION_H__*/
