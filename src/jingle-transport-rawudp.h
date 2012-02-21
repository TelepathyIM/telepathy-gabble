/*
 * jingle-transport-rawudp.h - Header for WockyJingleTransportRawUdp
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

#ifndef __JINGLE_TRANSPORT_RAWUDP_H__
#define __JINGLE_TRANSPORT_RAWUDP_H__

#include <glib-object.h>

#include "jingle-types.h"

G_BEGIN_DECLS

typedef struct _WockyJingleTransportRawUdpClass WockyJingleTransportRawUdpClass;

GType wocky_jingle_transport_rawudp_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP \
  (wocky_jingle_transport_rawudp_get_type ())
#define WOCKY_JINGLE_TRANSPORT_RAWUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP, \
                              WockyJingleTransportRawUdp))
#define WOCKY_JINGLE_TRANSPORT_RAWUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP, \
                           WockyJingleTransportRawUdpClass))
#define WOCKY_IS_JINGLE_TRANSPORT_RAWUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP))
#define WOCKY_IS_JINGLE_TRANSPORT_RAWUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP))
#define WOCKY_JINGLE_TRANSPORT_RAWUDP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP, \
                              WockyJingleTransportRawUdpClass))

struct _WockyJingleTransportRawUdpClass {
    GObjectClass parent_class;
};

typedef struct _WockyJingleTransportRawUdpPrivate WockyJingleTransportRawUdpPrivate;

struct _WockyJingleTransportRawUdp {
    GObject parent;
    WockyJingleTransportRawUdpPrivate *priv;
};

void jingle_transport_rawudp_register (WockyJingleFactory *factory);

#endif /* __JINGLE_TRANSPORT_RAWUDP_H__ */

