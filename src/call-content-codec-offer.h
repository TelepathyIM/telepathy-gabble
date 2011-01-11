/*
 * gabble-call-content-codec-offer.h - Header for GabbleCallContentCodecOffer
 * Copyright (C) 2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#ifndef __GABBLE_CALL_CONTENT_CODEC_OFFER_H__
#define __GABBLE_CALL_CONTENT_CODEC_OFFER_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _GabbleCallContentCodecOffer GabbleCallContentCodecOffer;
typedef struct _GabbleCallContentCodecOfferPrivate
  GabbleCallContentCodecOfferPrivate;
typedef struct _GabbleCallContentCodecOfferClass
  GabbleCallContentCodecOfferClass;

struct _GabbleCallContentCodecOfferClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleCallContentCodecOffer {
    GObject parent;

    GabbleCallContentCodecOfferPrivate *priv;
};

GType gabble_call_content_codec_offer_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_CONTENT_CODEC_OFFER \
  (gabble_call_content_codec_offer_get_type ())
#define GABBLE_CALL_CONTENT_CODEC_OFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  GABBLE_TYPE_CALL_CONTENT_CODEC_OFFER, GabbleCallContentCodecOffer))
#define GABBLE_CALL_CONTENT_CODEC_OFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  GABBLE_TYPE_CALL_CONTENT_CODEC_OFFER, GabbleCallContentCodecOfferClass))
#define GABBLE_IS_CALL_CONTENT_CODEC_OFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_CONTENT_CODEC_OFFER))
#define GABBLE_IS_CALL_CONTENT_CODEC_OFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_CONTENT_CODEC_OFFER))
#define GABBLE_CALL_CONTENT_CODEC_OFFER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CALL_CONTENT_CODEC_OFFER, \
  GabbleCallContentCodecOfferClass))

GabbleCallContentCodecOffer *gabble_call_content_codec_offer_new (
    TpDBusDaemon *dbus_daemon,
    const gchar *object_path,
    GHashTable *codecs);

void gabble_call_content_codec_offer_offer (GabbleCallContentCodecOffer *offer,
  GCancellable *cancellable,
  GAsyncReadyCallback callback,
  gpointer user_data);

GPtrArray *gabble_call_content_codec_offer_offer_finish (
  GabbleCallContentCodecOffer *offer,
  GAsyncResult *result,
  GError **error);


G_END_DECLS

#endif /* #ifndef __GABBLE_CALL_CONTENT_CODEC_OFFER_H__*/
