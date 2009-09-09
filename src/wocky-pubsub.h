/*
 * wocky-pubsub.h - Header of Wocky Pubsub
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

#ifndef __WOCKY_PUBSUB_H__
#define __WOCKY_PUBSUB_H__

#include <glib-object.h>
#include <wocky/wocky-xmpp-stanza.h>
#include <wocky/wocky-session.h>

#include "types.h"

G_BEGIN_DECLS

typedef struct _WockyPubsubClass WockyPubsubClass;

struct _WockyPubsubClass {
  GObjectClass parent_class;
};

struct _WockyPubsub {
  GObject parent;
};

GType wocky_pubsub_get_type (void);

#define WOCKY_TYPE_PUBSUB \
  (wocky_pubsub_get_type ())
#define WOCKY_PUBSUB(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_PUBSUB, \
   WockyPubsub))
#define WOCKY_PUBSUB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_PUBSUB, \
   WockyPubsubClass))
#define WOCKY_IS_PUBSUB(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_PUBSUB))
#define WOCKY_IS_PUBSUB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_PUBSUB))
#define WOCKY_PUBSUB_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_PUBSUB, \
   WockyPubsubClass))

WockyPubsub * wocky_pubsub_new (void);

void wocky_pubsub_start (WockyPubsub *pubsub,
    WockySession *session);

typedef gboolean (* WockyPubsubEventHandlerFunction) (WockyPubsub *pubsub,
    WockyXmppStanza *msg,
    const gchar *from,
    gpointer user_data);

guint wocky_pubsub_register_event_handler (WockyPubsub *pubsub,
    const gchar *ns,
    WockyPubsubEventHandlerFunction func,
    gpointer user_data);

void wocky_pubsub_send_query_async (WockyPubsub *pubsub,
    const gchar *jid,
    const gchar *ns,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

WockyXmppStanza * wocky_pubsub_send_query_finish (WockyPubsub *pubsub,
    GAsyncResult *result,
    GError **error);

void wocky_pubsub_unregister_event_handler (WockyPubsub *pubsub,
    guint id);

/* not methods */
WockyXmppStanza * pubsub_make_publish_msg (const gchar *to,
    const gchar *node_name,
    const gchar *item_ns,
    const gchar *item_name,
    WockyXmppNode **node);

G_END_DECLS

#endif /* __WOCKY_PUBSUB_H__ */
