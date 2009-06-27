/*
 * jingle-content.h - Header for GabbleJingleContent
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
#include <loudmouth/loudmouth.h>
#include "types.h"
#include "jingle-factory.h"
#include "jingle-transport-iface.h"

G_BEGIN_DECLS

typedef enum {
  JINGLE_MEDIA_TYPE_NONE = 0,
  JINGLE_MEDIA_TYPE_AUDIO,
  JINGLE_MEDIA_TYPE_VIDEO
} JingleMediaType;

typedef enum {
  JINGLE_CONTENT_STATE_EMPTY = 0,
  JINGLE_CONTENT_STATE_NEW,
  JINGLE_CONTENT_STATE_SENT,
  JINGLE_CONTENT_STATE_ACKNOWLEDGED,
  JINGLE_CONTENT_STATE_REMOVING
} JingleContentState;

struct _JingleCandidate {
  JingleTransportProtocol protocol;
  JingleCandidateType type;

  gchar *id;
  gchar *address;
  int port;
  int component;
  int generation;

  gdouble preference;
  gchar *username;
  gchar *password;
  int network;
};

typedef struct _GabbleJingleContentClass GabbleJingleContentClass;

GType gabble_jingle_content_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_CONTENT \
  (gabble_jingle_content_get_type ())
#define GABBLE_JINGLE_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_CONTENT, \
                              GabbleJingleContent))
#define GABBLE_JINGLE_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_CONTENT, \
                           GabbleJingleContentClass))
#define GABBLE_IS_JINGLE_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_CONTENT))
#define GABBLE_IS_JINGLE_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_CONTENT))
#define GABBLE_JINGLE_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_CONTENT, \
                              GabbleJingleContentClass))

struct _GabbleJingleContentClass {
    GObjectClass parent_class;

    void  (*parse_description) (GabbleJingleContent *, LmMessageNode *,
        GError **);
    void  (*produce_description) (GabbleJingleContent *, LmMessageNode *);
};

typedef struct _GabbleJingleContentPrivate GabbleJingleContentPrivate;

struct _GabbleJingleContent {
    GObject parent;
    GabbleJingleContentPrivate *priv;

    GabbleConnection *conn;
    GabbleJingleSession *session;
};

void gabble_jingle_content_parse_add (GabbleJingleContent *c,
    LmMessageNode *content_node, gboolean google_mode, GError **error);
void gabble_jingle_content_update_senders (GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error);
void gabble_jingle_content_produce_node (GabbleJingleContent *c,
    LmMessageNode *parent,
    gboolean include_description,
    gboolean include_transport,
    LmMessageNode **trans_node_out);
void gabble_jingle_content_parse_accept (GabbleJingleContent *c,
  LmMessageNode *content_node, gboolean google_mode, GError **error);

void gabble_jingle_content_parse_transport_info (GabbleJingleContent *self,
  LmMessageNode *trans_node, GError **error);
void gabble_jingle_content_parse_description_info (GabbleJingleContent *self,
  LmMessageNode *trans_node, GError **error);
void gabble_jingle_content_add_candidates (GabbleJingleContent *self, GList *li);
void _gabble_jingle_content_set_media_ready (GabbleJingleContent *self);
gboolean gabble_jingle_content_is_ready (GabbleJingleContent *self);
void gabble_jingle_content_set_transport_state (GabbleJingleContent *content,
    JingleTransportState state);
void gabble_jingle_content_remove (GabbleJingleContent *c, gboolean signal_peer);
GList *gabble_jingle_content_get_remote_candidates (GabbleJingleContent *c);
gboolean gabble_jingle_content_change_direction (GabbleJingleContent *c,
    JingleContentSenders senders);
void gabble_jingle_content_retransmit_candidates (GabbleJingleContent *self,
    gboolean all);
void gabble_jingle_content_inject_candidates (GabbleJingleContent *self,
    LmMessageNode *transport_node);
gboolean gabble_jingle_content_is_created_by_us (GabbleJingleContent *c);
gboolean gabble_jingle_content_creator_is_initiator (GabbleJingleContent *c);

const gchar *gabble_jingle_content_get_name (GabbleJingleContent *self);
const gchar *gabble_jingle_content_get_ns (GabbleJingleContent *self);
const gchar *gabble_jingle_content_get_disposition (GabbleJingleContent *self);
JingleTransportType gabble_jingle_content_get_transport_type (GabbleJingleContent *c);
const gchar *gabble_jingle_content_get_transport_ns (GabbleJingleContent *self);

void gabble_jingle_content_maybe_send_description (GabbleJingleContent *self);

#endif /* __JINGLE_CONTENT_H__ */

