/*
 * jingle_transport-iface.h - Header for GabbleJingleTransport interface
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#ifndef __GABBLE_JINGLE_TRANSPORT_IFACE_H__
#define __GABBLE_JINGLE_TRANSPORT_IFACE_H__

#include <glib-object.h>

#include "types.h"
#include <loudmouth/loudmouth.h>

G_BEGIN_DECLS

typedef enum
{
  JINGLE_TRANSPORT_STATE_DISCONNECTED,
  JINGLE_TRANSPORT_STATE_CONNECTING,
  JINGLE_TRANSPORT_STATE_CONNECTED
} JingleTransportState;

typedef struct _GabbleJingleTransportIface GabbleJingleTransportIface;
typedef struct _GabbleJingleTransportIfaceClass GabbleJingleTransportIfaceClass;

struct _GabbleJingleTransportIfaceClass {
  GTypeInterface parent;

  void (*parse_candidates) (GabbleJingleTransportIface *,
    LmMessageNode *, GError **);
  void (*produce) (GabbleJingleTransportIface *,
    LmMessageNode *);

  void (*add_candidates) (GabbleJingleTransportIface *,
    GList *);
};

GType gabble_jingle_transport_iface_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_TRANSPORT_IFACE \
  (gabble_jingle_transport_iface_get_type ())
#define GABBLE_JINGLE_TRANSPORT_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_TRANSPORT_IFACE, GabbleJingleTransportIface))
#define GABBLE_IS_JINGLE_TRANSPORT_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_TRANSPORT_IFACE))
#define GABBLE_JINGLE_TRANSPORT_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GABBLE_TYPE_JINGLE_TRANSPORT_IFACE,\
                              GabbleJingleTransportIfaceClass))

void gabble_jingle_transport_iface_parse_candidates (GabbleJingleTransportIface *,
    LmMessageNode *, GError **);
void gabble_jingle_transport_iface_produce (GabbleJingleTransportIface *, LmMessageNode *);
void gabble_jingle_transport_iface_add_candidates (GabbleJingleTransportIface *, GList *);

G_END_DECLS

#endif /* #ifndef __GABBLE_JINGLE_TRANSPORT_IFACE_H__ */
