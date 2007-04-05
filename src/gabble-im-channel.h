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
#include <time.h>

#include <telepathy-glib/enums.h>
#include "text-mixin.h"

G_BEGIN_DECLS

typedef struct _GabbleIMChannel GabbleIMChannel;
typedef struct _GabbleIMChannelClass GabbleIMChannelClass;

struct _GabbleIMChannelClass {
    GObjectClass parent_class;

    GabbleTextMixinClass text_class;
};

struct _GabbleIMChannel {
    GObject parent;

    GabbleTextMixin text;

    gpointer priv;
};

GType gabble_im_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_IM_CHANNEL \
  (gabble_im_channel_get_type ())
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

void _gabble_im_channel_receive (GabbleIMChannel *chan, TpChannelTextMessageType type, TpHandle sender, const char *from, time_t timestamp, const char *text);
void _gabble_im_channel_state_receive (GabbleIMChannel *chan, guint state);

G_END_DECLS

#endif /* #ifndef __GABBLE_IM_CHANNEL_H__*/
