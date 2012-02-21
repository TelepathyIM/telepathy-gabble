/*
 * jingle-media-rtp.h - Header for WockyJingleMediaRtp
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

#include "jingle-content.h"
#include "jingle-types.h"

G_BEGIN_DECLS

typedef struct _WockyJingleMediaRtpClass WockyJingleMediaRtpClass;

GType wocky_jingle_media_rtp_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_MEDIA_RTP \
  (wocky_jingle_media_rtp_get_type ())
#define WOCKY_JINGLE_MEDIA_RTP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_MEDIA_RTP, \
                              WockyJingleMediaRtp))
#define WOCKY_JINGLE_MEDIA_RTP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_MEDIA_RTP, \
                           WockyJingleMediaRtpClass))
#define WOCKY_IS_JINGLE_MEDIA_RTP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_MEDIA_RTP))
#define WOCKY_IS_JINGLE_MEDIA_RTP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_MEDIA_RTP))
#define WOCKY_JINGLE_MEDIA_RTP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_MEDIA_RTP, \
                              WockyJingleMediaRtpClass))

struct _WockyJingleMediaRtpClass {
    WockyJingleContentClass parent_class;
};

typedef struct _WockyJingleMediaRtpPrivate WockyJingleMediaRtpPrivate;

struct _WockyJingleMediaRtp {
    WockyJingleContent parent;
    WockyJingleMediaRtpPrivate *priv;
};

typedef struct {
  guint id;
  gchar *name;
  guint clockrate;
  guint channels;
  GHashTable *params;
  guint trr_int;
  GList *feedback_msgs;
} WockyJingleCodec;

typedef struct {
  gchar *type;
  gchar *subtype;
} WockyJingleFeedbackMessage;

typedef struct {
  guint id;
  WockyJingleContentSenders senders;
  gchar *uri;
} WockyJingleRtpHeaderExtension;

typedef struct {
  GList *codecs;
  GList *hdrexts;
  guint trr_int;
  GList *feedback_msgs;
} WockyJingleMediaDescription;

void jingle_media_rtp_register (WockyJingleFactory *factory);
gboolean jingle_media_rtp_set_local_media_description (
    WockyJingleMediaRtp *self, WockyJingleMediaDescription *md, gboolean ready,
    GError **error);
WockyJingleMediaDescription *wocky_jingle_media_rtp_get_remote_media_description (
    WockyJingleMediaRtp *self);

WockyJingleCodec * jingle_media_rtp_codec_new (guint id, const gchar *name,
    guint clockrate, guint channels, GHashTable *params);
void jingle_media_rtp_codec_free (WockyJingleCodec *p);
void jingle_media_rtp_free_codecs (GList *codecs);
GList * jingle_media_rtp_copy_codecs (GList *codecs);

gboolean jingle_media_rtp_compare_codecs (GList *old,
    GList *new,
    GList **changed,
    GError **e);

WockyJingleMediaDescription *wocky_jingle_media_description_new (void);
void wocky_jingle_media_description_free (WockyJingleMediaDescription *md);
WockyJingleMediaDescription *wocky_jingle_media_description_copy (
    WockyJingleMediaDescription *md);

WockyJingleRtpHeaderExtension *wocky_jingle_rtp_header_extension_new (guint id,
    WockyJingleContentSenders senders, const gchar *uri);
void wocky_jingle_rtp_header_extension_free (WockyJingleRtpHeaderExtension *hdrext);


WockyJingleFeedbackMessage *wocky_jingle_feedback_message_new (const gchar *type,
    const gchar *subtype);
void wocky_jingle_feedback_message_free (WockyJingleFeedbackMessage *fb);
void wocky_jingle_media_description_simplify (WockyJingleMediaDescription *md);

#endif /* __JINGLE_MEDIA_RTP_H__ */

