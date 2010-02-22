/*
 * gabble-call-content-codecoffer.h - Header for GabbleCallContentCodecoffer
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

#ifndef __GABBLE_CALL_CONTENT_CODECOFFER_H__
#define __GABBLE_CALL_CONTENT_CODECOFFER_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _GabbleCallContentCodecoffer GabbleCallContentCodecoffer;
typedef struct _GabbleCallContentCodecofferPrivate
  GabbleCallContentCodecofferPrivate;
typedef struct _GabbleCallContentCodecofferClass
  GabbleCallContentCodecofferClass;

struct _GabbleCallContentCodecofferClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleCallContentCodecoffer {
    GObject parent;

    GabbleCallContentCodecofferPrivate *priv;
};

GType gabble_call_content_codecoffer_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_CONTENT_CODECOFFER \
  (gabble_call_content_codecoffer_get_type ())
#define GABBLE_CALL_CONTENT_CODECOFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  GABBLE_TYPE_CALL_CONTENT_CODECOFFER, GabbleCallContentCodecoffer))
#define GABBLE_CALL_CONTENT_CODECOFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  GABBLE_TYPE_CALL_CONTENT_CODECOFFER, GabbleCallContentCodecofferClass))
#define GABBLE_IS_CALL_CONTENT_CODECOFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_CONTENT_CODECOFFER))
#define GABBLE_IS_CALL_CONTENT_CODECOFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_CONTENT_CODECOFFER))
#define GABBLE_CALL_CONTENT_CODECOFFER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CALL_CONTENT_CODECOFFER, \
  GabbleCallContentCodecofferClass))

GabbleCallContentCodecoffer *gabble_call_content_codecoffer_new (
  const gchar *object_path,
  GHashTable *codecs);

void gabble_call_content_codecoffer_offer (GabbleCallContentCodecoffer *offer,
  GCancellable *cancellable,
  GAsyncReadyCallback callback,
  gpointer user_data);

GPtrArray *gabble_call_content_codecoffer_offer_finish (
  GabbleCallContentCodecoffer *offer,
  GAsyncResult *result,
  GError **error);


G_END_DECLS

#endif /* #ifndef __GABBLE_CALL_CONTENT_CODECOFFER_H__*/
