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
  guint8 id;
  gchar *name;
  guint clockrate;
  guint channels;
  GHashTable *params;
} JingleCodec;

const gchar *gabble_jingle_media_rtp_parse (GabbleJingleMediaRtp *sess,
    LmMessage *message, GError **error);
void jingle_media_rtp_register (GabbleJingleFactory *factory);
gboolean jingle_media_rtp_set_local_codecs (GabbleJingleMediaRtp *self,
    GList *codecs, gboolean ready, GError **error);
GList *gabble_jingle_media_rtp_get_remote_codecs (GabbleJingleMediaRtp *self);

JingleCodec * jingle_media_rtp_codec_new (guint id, const gchar *name,
    guint clockrate, guint channels, GHashTable *params);

#endif /* __JINGLE_MEDIA_RTP_H__ */

