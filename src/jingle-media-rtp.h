/*
 * jingle-media-rtp.h - Header for GabbleJingleMediaRtp
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

#ifndef __JINGLE_MEDIA_RTP_H__
#define __JINGLE_MEDIA_RTP_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>
#include "types.h"

#include "jingle-content.h"

G_BEGIN_DECLS

typedef struct _GabbleJingleMediaRtpClass GabbleJingleMediaRtpClass;

GType gabble_jingle_media_rtp_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_MEDIA_RTP \
  (gabble_jingle_media_rtp_get_type ())
#define GABBLE_JINGLE_MEDIA_RTP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_MEDIA_RTP, \
                              GabbleJingleMediaRtp))
#define GABBLE_JINGLE_MEDIA_RTP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_MEDIA_RTP, \
                           GabbleJingleMediaRtpClass))
#define GABBLE_IS_JINGLE_MEDIA_RTP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_MEDIA_RTP))
#define GABBLE_IS_JINGLE_MEDIA_RTP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_MEDIA_RTP))
#define GABBLE_JINGLE_MEDIA_RTP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_MEDIA_RTP, \
                              GabbleJingleMediaRtpClass))

struct _GabbleJingleMediaRtpClass {
    GabbleJingleContentClass parent_class;
};

typedef struct _GabbleJingleMediaRtpPrivate GabbleJingleMediaRtpPrivate;

struct _GabbleJingleMediaRtp {
    GabbleJingleContent parent;
    GabbleJingleMediaRtpPrivate *priv;
};

typedef struct {
  guint id;
  gchar *name;
  guint clockrate;
  guint channels;
  GHashTable *params;
  guint trr_int;
  GList *feedback_msgs;
} JingleCodec;

typedef struct {
  gchar *type;
  gchar *subtype;
} JingleFeedbackMessage;

typedef struct {
  guint id;
  JingleContentSenders senders;
  gchar *uri;
} JingleRtpHeaderExtension;

typedef struct {
  GList *codecs;
  GList *hdrexts;
  guint trr_int;
  GList *feedback_msgs;
} JingleMediaDescription;

void jingle_media_rtp_register (GabbleJingleFactory *factory);
gboolean jingle_media_rtp_set_local_media_description (
    GabbleJingleMediaRtp *self, JingleMediaDescription *md, gboolean ready,
    GError **error);
JingleMediaDescription *gabble_jingle_media_rtp_get_remote_media_description (
    GabbleJingleMediaRtp *self);

JingleCodec * jingle_media_rtp_codec_new (guint id, const gchar *name,
    guint clockrate, guint channels, GHashTable *params);
void jingle_media_rtp_codec_free (JingleCodec *p);
void jingle_media_rtp_free_codecs (GList *codecs);
GList * jingle_media_rtp_copy_codecs (GList *codecs);

gboolean jingle_media_rtp_compare_codecs (GList *old,
    GList *new,
    GList **changed,
    GError **e);

JingleMediaDescription *jingle_media_description_new (void);
void jingle_media_description_free (JingleMediaDescription *md);
JingleMediaDescription *jingle_media_description_copy (
    JingleMediaDescription *md);

JingleRtpHeaderExtension *jingle_rtp_header_extension_new (guint id,
    JingleContentSenders senders, const gchar *uri);
void jingle_rtp_header_extension_free (JingleRtpHeaderExtension *hdrext);


JingleFeedbackMessage *jingle_feedback_message_new (const gchar *type,
    const gchar *subtype);
void jingle_feedback_message_free (JingleFeedbackMessage *fb);
void jingle_media_description_simplify (JingleMediaDescription *md);

#endif /* __JINGLE_MEDIA_RTP_H__ */

