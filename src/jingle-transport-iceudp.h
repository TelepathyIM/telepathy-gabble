/*
 * jingle-transport-iceudp.h - Header for WockyJingleTransportIceUdp
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

#include "jingle-types.h"

G_BEGIN_DECLS

typedef struct _WockyJingleTransportIceUdpClass WockyJingleTransportIceUdpClass;

GType wocky_jingle_transport_iceudp_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_TRANSPORT_ICEUDP \
  (wocky_jingle_transport_iceudp_get_type ())
#define WOCKY_JINGLE_TRANSPORT_ICEUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_TRANSPORT_ICEUDP, \
                              WockyJingleTransportIceUdp))
#define WOCKY_JINGLE_TRANSPORT_ICEUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_TRANSPORT_ICEUDP, \
                           WockyJingleTransportIceUdpClass))
#define WOCKY_IS_JINGLE_TRANSPORT_ICEUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_TRANSPORT_ICEUDP))
#define WOCKY_IS_JINGLE_TRANSPORT_ICEUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_TRANSPORT_ICEUDP))
#define WOCKY_JINGLE_TRANSPORT_ICEUDP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_TRANSPORT_ICEUDP, \
                              WockyJingleTransportIceUdpClass))

struct _WockyJingleTransportIceUdpClass {
    GObjectClass parent_class;
};

typedef struct _WockyJingleTransportIceUdpPrivate WockyJingleTransportIceUdpPrivate;

struct _WockyJingleTransportIceUdp {
    GObject parent;
    WockyJingleTransportIceUdpPrivate *priv;
};

void jingle_transport_iceudp_register (WockyJingleFactory *factory);

#endif /* __JINGLE_TRANSPORT_ICEUDP_H__ */

