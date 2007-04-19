/*
 * pubsub.h - Header of Gabble Pubsub functions
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __PUBSUB_H__
#define __PUBSUB_H__

#include "gabble-connection.h"

G_BEGIN_DECLS

typedef gboolean (* GabblePubsubEventHandlerFunction) (GabbleConnection *conn,
                                                       LmMessage *msg,
                                                       TpHandle handle);

typedef struct _GabblePubsubEventHandler GabblePubsubEventHandler;

struct _GabblePubsubEventHandler
{
    const gchar *ns;
    GabblePubsubEventHandlerFunction handle_function;
};

gboolean
gabble_pubsub_event_handler (
    GabbleConnection *conn,
    LmMessage *msg,
    TpHandle handle);

gboolean
pubsub_query (
    GabbleConnection *conn,
    const gchar *jid,
    const gchar *ns,
    GabbleConnectionMsgReplyFunc reply_func,
    gpointer user_data);

LmMessage *
pubsub_make_publish_msg (
    const gchar *to,
    const gchar *node_name,
    const gchar *item_ns,
    const gchar *item_name,
    LmMessageNode **node);

G_END_DECLS

#endif /* __PUBSUB_H__ */

