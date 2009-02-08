/*
 * search-channel.h - Header for GabbleSearchChannel
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __GABBLE_SEARCH_CHANNEL_H__
#define __GABBLE_SEARCH_CHANNEL_H__

#include <glib-object.h>

#include "base-channel.h"

G_BEGIN_DECLS

typedef struct _GabbleSearchChannel GabbleSearchChannel;
typedef struct _GabbleSearchChannelClass GabbleSearchChannelClass;
typedef struct _GabbleSearchChannelPrivate GabbleSearchChannelPrivate;

struct _GabbleSearchChannelClass {
    GabbleBaseChannelClass base_class;
};

struct _GabbleSearchChannel {
    GabbleBaseChannel base;
    GabbleSearchChannelPrivate *priv;
};

GType gabble_search_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_SEARCH_CHANNEL \
  (gabble_search_channel_get_type ())
#define GABBLE_SEARCH_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_SEARCH_CHANNEL, GabbleSearchChannel))
#define GABBLE_SEARCH_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_SEARCH_CHANNEL,\
                           GabbleSearchChannelClass))
#define GABBLE_IS_SEARCH_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_SEARCH_CHANNEL))
#define GABBLE_IS_SEARCH_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_SEARCH_CHANNEL))
#define GABBLE_SEARCH_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_SEARCH_CHANNEL, \
                              GabbleSearchChannelClass))

G_END_DECLS

#endif /* #ifndef __GABBLE_SEARCH_CHANNEL_H__*/
