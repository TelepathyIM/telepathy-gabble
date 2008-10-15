/*
 * jingle-factory.h - Header for GabbleJingleFactory
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __JINGLE_FACTORY_H__
#define __JINGLE_FACTORY_H__

#include <glib-object.h>

#include "types.h"
#include "jingle-transport-iface.h"
#include "jingle-description-iface.h"

G_BEGIN_DECLS

typedef enum {
  /* not a jingle message */
  JINGLE_DIALECT_ERROR,
  /* old libjingle3 gtalk variant */
  JINGLE_DIALECT_GTALK3,
  /* new gtalk variant */
  JINGLE_DIALECT_GTALK4,
  /* jingle in the old 0.15 version days */
  JINGLE_DIALECT_V015,
  /* current jingle standard */
  JINGLE_DIALECT_V026
} JingleDialect;

typedef enum {
  JINGLE_STATE_PENDING_CREATED,
  JINGLE_STATE_PENDING_INITIATE_SENT,
  JINGLE_STATE_PENDING_INITIATED,
  JINGLE_STATE_PENDING_ACCEPT_SENT,
  JINGLE_STATE_PENDING_ACTIVE,
  JINGLE_STATE_ENDED
} JingleState;

typedef enum {
  JINGLE_ACTION_UNKNOWN = -1,
  JINGLE_ACTION_CONTENT_ACCEPT = 0,
  JINGLE_ACTION_CONTENT_ADD,
  JINGLE_ACTION_CONTENT_MODIFY,
  JINGLE_ACTION_CONTENT_REMOVE,
  JINGLE_ACTION_CONTENT_REPLACE,
  JINGLE_ACTION_SESSION_ACCEPT,
  JINGLE_ACTION_SESSION_INFO,
  JINGLE_ACTION_SESSION_INITIATE,
  JINGLE_ACTION_SESSION_TERMINATE,
  JINGLE_ACTION_TRANSPORT_INFO,
  JINGLE_ACTION_TRANSPORT_ACCEPT,
} JingleAction;

typedef enum {
  JINGLE_CONTENT_SENDERS_NONE = -1,
  JINGLE_CONTENT_SENDERS_INITIATOR = 0,
  JINGLE_CONTENT_SENDERS_RESPONDER,
  JINGLE_CONTENT_SENDERS_BOTH
} JingleContentSenders;

typedef enum {
  JINGLE_TRANSPORT_UNKNOWN = -1,
  JINGLE_TRANSPORT_GOOGLE_P2P = 0,
  JINGLE_TRANSPORT_RAW_UDP,
  JINGLE_TRANSPORT_ICE
} JingleTransportType;

typedef enum {
  JINGLE_TRANSPORT_PROTOCOL_UDP,
  JINGLE_TRANSPORT_PROTOCOL_TCP
} JingleTransportProtocol;

typedef enum {
  JINGLE_CANDIDATE_TYPE_LOCAL,
  JINGLE_CANDIDATE_TYPE_STUN,
  JINGLE_CANDIDATE_TYPE_RELAY
} JingleCandidateType;

typedef struct _GabbleJingleFactoryClass GabbleJingleFactoryClass;

GType gabble_jingle_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_FACTORY \
  (gabble_jingle_factory_get_type ())
#define GABBLE_JINGLE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_FACTORY, \
                              GabbleJingleFactory))
#define GABBLE_JINGLE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_FACTORY, \
                           GabbleJingleFactoryClass))
#define GABBLE_IS_JINGLE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_FACTORY))
#define GABBLE_IS_JINGLE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_FACTORY))
#define GABBLE_JINGLE_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_FACTORY, \
                              GabbleJingleFactoryClass))

struct _GabbleJingleFactoryClass {
    GObjectClass parent_class;
};

struct _GabbleJingleFactory {
    GObject parent;

    GHashTable *content_types;
    GHashTable *transports;

    gboolean get_stun_from_jingle;
    gchar *stun_server;
    guint16 stun_port;
    gchar *relay_token;

    gpointer priv;
};

void gabble_jingle_factory_register_content_type (GabbleJingleFactory *factory,
    gchar *namespace, GType content_type);
void gabble_jingle_factory_register_transport (GabbleJingleFactory *factory,
    gchar *namespace, GType transport_type);
void _jingle_factory_unregister_session (GabbleJingleFactory *factory,
    const gchar *sid);

GabbleJingleSession *gabble_jingle_factory_create_session (GabbleJingleFactory
    *fac, TpHandle peer, const gchar *peer_resource);

G_END_DECLS;

#endif /* __JINGLE_FACTORY_H__ */

