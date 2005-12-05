/* 
 * jabber-im-channel.h - Header for JabberIMChannel
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

#ifndef __JABBER_IM_CHANNEL_H__
#define __JABBER_IM_CHANNEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _JabberIMChannel JabberIMChannel;
typedef struct _JabberIMChannelClass JabberIMChannelClass;
typedef struct _JabberIMChannelPrivate JabberIMChannelPrivate;

struct _JabberIMChannelClass {
    GObjectClass parent_class;
};

struct _JabberIMChannel {
    GObject parent;
};

GType jabber_im_channel_get_type(void);

/* TYPE MACROS */
#define JABBER_TYPE_IM_CHANNEL \
  (jabber_im_channel_get_type())
#define JABBER_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), JABBER_TYPE_IM_CHANNEL, JabberIMChannel))
#define JABBER_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), JABBER_TYPE_IM_CHANNEL, JabberIMChannelClass))
#define JABBER_IS_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), JABBER_TYPE_IM_CHANNEL))
#define JABBER_IS_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), JABBER_TYPE_IM_CHANNEL))
#define JABBER_IM_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), JABBER_TYPE_IM_CHANNEL, JabberIMChannelClass))


gboolean jabber_im_channel_list_pending_messages (JabberIMChannel *obj, gpointer* ret, GError **error);
gboolean jabber_im_channel_acknowledge_pending_message (JabberIMChannel *obj, guint id, GError **error);
gboolean jabber_im_channel_send (JabberIMChannel *obj, guint type, const gchar * text, GError **error);
gboolean jabber_im_channel_get_handle (JabberIMChannel *obj, guint* ret, guint* ret1, GError **error);
gboolean jabber_im_channel_get_interfaces (JabberIMChannel *obj, gchar *** ret, GError **error);
gboolean jabber_im_channel_get_channel_type (JabberIMChannel *obj, gchar ** ret, GError **error);
gboolean jabber_im_channel_close (JabberIMChannel *obj, GError **error);


G_END_DECLS

#endif /* #ifndef __JABBER_IM_CHANNEL)_H__*/
