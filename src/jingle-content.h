/*
 * jingle-content.h - Header for WockyJingleContent
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

#ifndef __JINGLE_CONTENT_H__
#define __JINGLE_CONTENT_H__

#include <glib-object.h>

#include "jingle-factory.h"
#include "jingle-transport-iface.h"
#include "jingle-types.h"

G_BEGIN_DECLS

typedef enum {
  WOCKY_JINGLE_MEDIA_TYPE_NONE = 0,
  WOCKY_JINGLE_MEDIA_TYPE_AUDIO,
  WOCKY_JINGLE_MEDIA_TYPE_VIDEO,
} WockyJingleMediaType;

typedef enum {
  WOCKY_JINGLE_CONTENT_STATE_EMPTY = 0,
  WOCKY_JINGLE_CONTENT_STATE_NEW,
  WOCKY_JINGLE_CONTENT_STATE_SENT,
  WOCKY_JINGLE_CONTENT_STATE_ACKNOWLEDGED,
  WOCKY_JINGLE_CONTENT_STATE_REMOVING
} WockyJingleContentState;

struct _WockyJingleCandidate {
  WockyJingleTransportProtocol protocol;
  WockyJingleCandidateType type;

  gchar *id;
  gchar *address;
  int port;
  int component;
  int generation;

  int preference;
  gchar *username;
  gchar *password;
  int network;
};

typedef struct _WockyJingleContentClass WockyJingleContentClass;

GType wocky_jingle_content_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_CONTENT \
  (wocky_jingle_content_get_type ())
#define WOCKY_JINGLE_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_CONTENT, \
                              WockyJingleContent))
#define WOCKY_JINGLE_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_CONTENT, \
                           WockyJingleContentClass))
#define WOCKY_IS_JINGLE_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_CONTENT))
#define WOCKY_IS_JINGLE_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_CONTENT))
#define WOCKY_JINGLE_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_CONTENT, \
                              WockyJingleContentClass))

struct _WockyJingleContentClass {
    GObjectClass parent_class;

    void  (*parse_description) (WockyJingleContent *, WockyNode *,
        GError **);
    void  (*produce_description) (WockyJingleContent *, WockyNode *);
    void  (*transport_created) (WockyJingleContent *,
        WockyJingleTransportIface *);
    WockyJingleContentSenders (*get_default_senders) (WockyJingleContent *);
};

typedef struct _WockyJingleContentPrivate WockyJingleContentPrivate;

struct _WockyJingleContent {
    GObject parent;
    WockyJingleContentPrivate *priv;

    WockyJingleSession *session;
};

void wocky_jingle_content_parse_add (WockyJingleContent *c,
    WockyNode *content_node, gboolean google_mode, GError **error);
void wocky_jingle_content_update_senders (WockyJingleContent *c,
    WockyNode *content_node, GError **error);
void wocky_jingle_content_produce_node (WockyJingleContent *c,
    WockyNode *parent,
    gboolean include_description,
    gboolean include_transport,
    WockyNode **trans_node_out);
void wocky_jingle_content_parse_accept (WockyJingleContent *c,
  WockyNode *content_node, gboolean google_mode, GError **error);

void wocky_jingle_content_parse_info (WockyJingleContent *c,
    WockyNode *content_node, GError **error);
void wocky_jingle_content_parse_transport_info (WockyJingleContent *self,
  WockyNode *trans_node, GError **error);
void wocky_jingle_content_parse_description_info (WockyJingleContent *self,
  WockyNode *trans_node, GError **error);
guint wocky_jingle_content_create_share_channel (WockyJingleContent *self,
    const gchar *name);
void wocky_jingle_content_add_candidates (WockyJingleContent *self, GList *li);
void _wocky_jingle_content_set_media_ready (WockyJingleContent *self);
gboolean wocky_jingle_content_is_ready (WockyJingleContent *self);
void wocky_jingle_content_set_transport_state (WockyJingleContent *content,
    WockyJingleTransportState state);
void wocky_jingle_content_remove (WockyJingleContent *c, gboolean signal_peer);
void wocky_jingle_content_reject (WockyJingleContent *c,
    WockyJingleReason reason);

GList *wocky_jingle_content_get_remote_candidates (WockyJingleContent *c);
GList *wocky_jingle_content_get_local_candidates (WockyJingleContent *c);
gboolean wocky_jingle_content_get_credentials (WockyJingleContent *c,
  gchar **ufrag, gchar **pwd);
gboolean wocky_jingle_content_change_direction (WockyJingleContent *c,
    WockyJingleContentSenders senders);
void wocky_jingle_content_retransmit_candidates (WockyJingleContent *self,
    gboolean all);
void wocky_jingle_content_inject_candidates (WockyJingleContent *self,
    WockyNode *transport_node);
gboolean wocky_jingle_content_is_created_by_us (WockyJingleContent *c);
gboolean wocky_jingle_content_creator_is_initiator (WockyJingleContent *c);

const gchar *wocky_jingle_content_get_name (WockyJingleContent *self);
const gchar *wocky_jingle_content_get_ns (WockyJingleContent *self);
const gchar *wocky_jingle_content_get_disposition (WockyJingleContent *self);
WockyJingleTransportType wocky_jingle_content_get_transport_type (WockyJingleContent *c);
const gchar *wocky_jingle_content_get_transport_ns (WockyJingleContent *self);

void wocky_jingle_content_maybe_send_description (WockyJingleContent *self);

gboolean wocky_jingle_content_sending (WockyJingleContent *self);
gboolean wocky_jingle_content_receiving (WockyJingleContent *self);

void wocky_jingle_content_set_sending (WockyJingleContent *self,
    gboolean send);
void wocky_jingle_content_request_receiving (WockyJingleContent *self,
    gboolean receive);

void wocky_jingle_content_send_complete (WockyJingleContent *self);

#endif /* __JINGLE_CONTENT_H__ */

