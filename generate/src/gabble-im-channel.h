/*
 * gabble-im-channel.h - Header for GabbleIMChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __GABBLE_IM_CHANNEL_H__
#define __GABBLE_IM_CHANNEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleIMChannel GabbleIMChannel;
typedef struct _GabbleIMChannelClass GabbleIMChannelClass;

struct _GabbleIMChannelClass {
    GObjectClass parent_class;
};

struct _GabbleIMChannel {
    GObject parent;
};

GType gabble_im_channel_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_IM_CHANNEL \
  (gabble_im_channel_get_type())
#define GABBLE_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_IM_CHANNEL, GabbleIMChannel))
#define GABBLE_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_IM_CHANNEL, GabbleIMChannelClass))
#define GABBLE_IS_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_IM_CHANNEL))
#define GABBLE_IS_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_IM_CHANNEL))
#define GABBLE_IM_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_IM_CHANNEL, GabbleIMChannelClass))


gboolean gabble_im_channel_acknowledge_pending_message (GabbleIMChannel *obj, guint id, GError **error);
gboolean gabble_im_channel_close (GabbleIMChannel *obj, GError **error);
gboolean gabble_im_channel_get_channel_type (GabbleIMChannel *obj, gchar ** ret, GError **error);
gboolean gabble_im_channel_get_handle (GabbleIMChannel *obj, guint* ret, guint* ret1, GError **error);
gboolean gabble_im_channel_get_interfaces (GabbleIMChannel *obj, gchar *** ret, GError **error);
gboolean gabble_im_channel_get_message_types (GabbleIMChannel *obj, GArray ** ret, GError **error);
gboolean gabble_im_channel_list_pending_messages (GabbleIMChannel *obj, GPtrArray ** ret, GError **error);
gboolean gabble_im_channel_send (GabbleIMChannel *obj, guint type, const gchar * text, GError **error);


G_END_DECLS

#endif /* #ifndef __GABBLE_IM_CHANNEL_H__*/
