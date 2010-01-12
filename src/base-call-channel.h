/*
 * gabble-call-channel.h - Header for GabbleBaseBaseCallChannel
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

#ifndef __GABBLE_BASE_CALL_CHANNEL_H__
#define __GABBLE_BASE_CALL_CHANNEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleBaseCallChannel GabbleBaseCallChannel;
typedef struct _GabbleBaseCallChannelPrivate GabbleBaseCallChannelPrivate;
typedef struct _GabbleBaseCallChannelClass GabbleBaseCallChannelClass;

struct _GabbleBaseCallChannelClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleBaseCallChannel {
    GObject parent;

    GabbleConnection *conn;
    TpHandle target;
    gboolean initial_audio;
    gboolean initial_video;

    GabbleBaseCallChannelPrivate *priv;
};

GType gabble_base_call_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BASE_CALL_CHANNEL \
  (gabble_base_call_channel_get_type ())
#define GABBLE_BASE_CALL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
   GABBLE_TYPE_BASE_CALL_CHANNEL, GabbleBaseCallChannel))
#define GABBLE_BASE_CALL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
   GABBLE_TYPE_BASE_CALL_CHANNEL, GabbleBaseCallChannelClass))
#define GABBLE_IS_BASE_CALL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BASE_CALL_CHANNEL))
#define GABBLE_IS_BASE_CALL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BASE_CALL_CHANNEL))
#define GABBLE_BASE_CALL_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
   GABBLE_TYPE_BASE_CALL_CHANNEL, GabbleBaseCallChannelClass))

void gabble_base_call_channel_close (GabbleBaseCallChannel *self);
void gabble_base_call_channel_register (GabbleBaseCallChannel *self);
gboolean gabble_base_call_channel_registered (GabbleBaseCallChannel *self);

GabbleJingleSession * gabble_base_call_channel_get_session (
    GabbleBaseCallChannel *self);
void gabble_base_call_channel_set_transport (GabbleBaseCallChannel *self,
    const gchar *transport);
void gabble_base_call_channel_set_session (GabbleBaseCallChannel *self,
    GabbleJingleSession *session);
const gchar* gabble_base_call_channel_create_content (
    GabbleBaseCallChannel *self,
    const gchar *name,
    JingleMediaType type,
    GabbleCallContentDisposition disposition,
    GError **error);

G_END_DECLS

#endif /* #ifndef __GABBLE_BASE_CALL_CHANNEL_H__*/
