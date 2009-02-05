/*
 * jingle-transport-iceudp.h - Header for GabbleJingleTransportIceUdp
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

#ifndef __JINGLE_TRANSPORT_ICEUDP_H__
#define __JINGLE_TRANSPORT_ICEUDP_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include "types.h"

G_BEGIN_DECLS

typedef struct _GabbleJingleTransportIceUdpClass GabbleJingleTransportIceUdpClass;

GType gabble_jingle_transport_iceudp_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP \
  (gabble_jingle_transport_iceudp_get_type ())
#define GABBLE_JINGLE_TRANSPORT_ICEUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP, \
                              GabbleJingleTransportIceUdp))
#define GABBLE_JINGLE_TRANSPORT_ICEUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP, \
                           GabbleJingleTransportIceUdpClass))
#define GABBLE_IS_JINGLE_TRANSPORT_ICEUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP))
#define GABBLE_IS_JINGLE_TRANSPORT_ICEUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP))
#define GABBLE_JINGLE_TRANSPORT_ICEUDP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP, \
                              GabbleJingleTransportIceUdpClass))

struct _GabbleJingleTransportIceUdpClass {
    GObjectClass parent_class;
};

typedef struct _GabbleJingleTransportIceUdpPrivate GabbleJingleTransportIceUdpPrivate;

struct _GabbleJingleTransportIceUdp {
    GObject parent;
    GabbleJingleTransportIceUdpPrivate *priv;
};

void jingle_transport_iceudp_register (GabbleJingleFactory *factory);

#endif /* __JINGLE_TRANSPORT_ICEUDP_H__ */

