/*
 * jingle-session.h - Header for WockyJingleSession
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

#ifndef __JINGLE_SESSION_H__
#define __JINGLE_SESSION_H__

#include <glib-object.h>
#include <wocky/wocky.h>

#include "jingle-content.h"
#include "jingle-factory.h"
#include "jingle-types.h"

G_BEGIN_DECLS

typedef enum
{
  MODE_GOOGLE,
  MODE_JINGLE
} GabbleMediaSessionMode;

typedef struct _WockyJingleSessionClass WockyJingleSessionClass;

GType wocky_jingle_session_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_SESSION \
  (wocky_jingle_session_get_type ())
#define WOCKY_JINGLE_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_SESSION, \
                              WockyJingleSession))
#define WOCKY_JINGLE_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_SESSION, \
                           WockyJingleSessionClass))
#define WOCKY_IS_JINGLE_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_SESSION))
#define WOCKY_IS_JINGLE_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_SESSION))
#define WOCKY_JINGLE_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_SESSION, \
                              WockyJingleSessionClass))

struct _WockyJingleSessionClass {
    GObjectClass parent_class;
};

typedef struct _WockyJingleSessionPrivate WockyJingleSessionPrivate;

struct _WockyJingleSession {
    GObject parent;
    WockyJingleSessionPrivate *priv;
};

WockyJingleSession *wocky_jingle_session_new (
    WockyJingleFactory *factory,
    WockyPorter *porter,
    const gchar *session_id,
    gboolean local_initiator,
    WockyContact *peer,
    WockyJingleDialect dialect,
    gboolean local_hold);

const gchar * wocky_jingle_session_detect (WockyStanza *stanza,
    WockyJingleAction *action, WockyJingleDialect *dialect);
gboolean wocky_jingle_session_parse (WockyJingleSession *sess,
    WockyJingleAction action, WockyStanza *stanza, GError **error);
WockyStanza *wocky_jingle_session_new_message (WockyJingleSession *sess,
    WockyJingleAction action, WockyNode **sess_node);

void wocky_jingle_session_accept (WockyJingleSession *sess);
gboolean wocky_jingle_session_terminate (WockyJingleSession *sess,
    WockyJingleReason reason,
    const gchar *text,
    GError **error);
void wocky_jingle_session_remove_content (WockyJingleSession *sess,
    WockyJingleContent *c);

WockyJingleContent *
wocky_jingle_session_add_content (WockyJingleSession *sess,
    WockyJingleMediaType mtype,
    WockyJingleContentSenders senders,
    const char *name,
    const gchar *content_ns,
    const gchar *transport_ns);

GType wocky_jingle_session_get_content_type (WockyJingleSession *);
GList *wocky_jingle_session_get_contents (WockyJingleSession *sess);
const gchar *wocky_jingle_session_get_peer_resource (
    WockyJingleSession *sess);
const gchar *wocky_jingle_session_get_initiator (
    WockyJingleSession *sess);
const gchar *wocky_jingle_session_get_sid (WockyJingleSession *sess);
WockyJingleDialect wocky_jingle_session_get_dialect (WockyJingleSession *sess);

gboolean wocky_jingle_session_can_modify_contents (WockyJingleSession *sess);
gboolean wocky_jingle_session_peer_has_cap (
    WockyJingleSession *self,
    const gchar *cap_or_quirk);

void wocky_jingle_session_send (
    WockyJingleSession *sess,
    WockyStanza *stanza);

void wocky_jingle_session_set_local_hold (WockyJingleSession *sess,
    gboolean held);

gboolean wocky_jingle_session_get_remote_hold (WockyJingleSession *sess);

gboolean wocky_jingle_session_get_remote_ringing (WockyJingleSession *sess);

gboolean wocky_jingle_session_defines_action (WockyJingleSession *sess,
    WockyJingleAction action);

WockyContact *wocky_jingle_session_get_peer_contact (WockyJingleSession *self);
const gchar *wocky_jingle_session_get_peer_jid (WockyJingleSession *sess);

const gchar *wocky_jingle_session_get_reason_name (WockyJingleReason reason);

WockyJingleFactory *wocky_jingle_session_get_factory (WockyJingleSession *self);
WockyPorter *wocky_jingle_session_get_porter (WockyJingleSession *self);

#endif /* __JINGLE_SESSION_H__ */

