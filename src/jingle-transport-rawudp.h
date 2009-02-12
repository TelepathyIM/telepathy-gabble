/*
 * jingle-transport-rawudp.h - Header for GabbleJingleTransportRawUdp
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
#include <loudmouth/loudmouth.h>

#include "types.h"

G_BEGIN_DECLS

typedef struct _GabbleJingleTransportRawUdpClass GabbleJingleTransportRawUdpClass;

GType gabble_jingle_transport_rawudp_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP \
  (gabble_jingle_transport_rawudp_get_type ())
#define GABBLE_JINGLE_TRANSPORT_RAWUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP, \
                              GabbleJingleTransportRawUdp))
#define GABBLE_JINGLE_TRANSPORT_RAWUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP, \
                           GabbleJingleTransportRawUdpClass))
#define GABBLE_IS_JINGLE_TRANSPORT_RAWUDP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP))
#define GABBLE_IS_JINGLE_TRANSPORT_RAWUDP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP))
#define GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP, \
                              GabbleJingleTransportRawUdpClass))

struct _GabbleJingleTransportRawUdpClass {
    GObjectClass parent_class;
};

typedef struct _GabbleJingleTransportRawUdpPrivate GabbleJingleTransportRawUdpPrivate;

struct _GabbleJingleTransportRawUdp {
    GObject parent;
    GabbleJingleTransportRawUdpPrivate *priv;
};

void jingle_transport_rawudp_register (GabbleJingleFactory *factory);

#endif /* __JINGLE_TRANSPORT_RAWUDP_H__ */

