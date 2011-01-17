/*
 * gabble-call-content.h - Header for TpyBaseMediaCallContent
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

#ifndef __TPY_BASE_MEDIA_CALL_CONTENT_H__
#define __TPY_BASE_MEDIA_CALL_CONTENT_H__

#include <glib-object.h>

#include <telepathy-yell/call-content-codec-offer.h>
#include <telepathy-yell/base-call-content.h>


G_BEGIN_DECLS

typedef struct _TpyBaseMediaCallContent TpyBaseMediaCallContent;
typedef struct _TpyBaseMediaCallContentPrivate TpyBaseMediaCallContentPrivate;
typedef struct _TpyBaseMediaCallContentClass TpyBaseMediaCallContentClass;

struct _TpyBaseMediaCallContentClass {
    TpyBaseCallContentClass parent_class;
};

struct _TpyBaseMediaCallContent {
    TpyBaseCallContent parent;

    TpyBaseMediaCallContentPrivate *priv;
};

GType tpy_base_media_call_content_get_type (void);

/* TYPE MACROS */
#define TPY_TYPE_BASE_MEDIA_CALL_CONTENT \
  (tpy_base_media_call_content_get_type ())
#define TPY_BASE_MEDIA_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      TPY_TYPE_BASE_MEDIA_CALL_CONTENT, TpyBaseMediaCallContent))
#define TPY_BASE_MEDIA_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
    TPY_TYPE_BASE_MEDIA_CALL_CONTENT, TpyBaseMediaCallContentClass))
#define TPY_IS_BASE_MEDIA_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPY_TYPE_BASE_MEDIA_CALL_CONTENT))
#define TPY_IS_BASE_MEDIA_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPY_TYPE_BASE_MEDIA_CALL_CONTENT))
#define TPY_BASE_MEDIA_CALL_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
    TPY_TYPE_BASE_MEDIA_CALL_CONTENT, TpyBaseMediaCallContentClass))

GPtrArray *tpy_base_media_call_content_get_local_codecs (
  TpyBaseMediaCallContent *self);

void tpy_base_media_call_content_add_offer (TpyBaseMediaCallContent *self,
    TpyCallContentCodecOffer *offer);

G_END_DECLS

#endif /* #ifndef __TPY_BASE_MEDIA_CALL_CONTENT_H__*/
