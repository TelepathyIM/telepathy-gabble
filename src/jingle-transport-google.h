/*
 * jingle-transport-google.h - Header for GabbleJingleTransportGoogle
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

#ifndef __JINGLE_TRANSPORT_GOOGLE_H__
#define __JINGLE_TRANSPORT_GOOGLE_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include "types.h"

G_BEGIN_DECLS

typedef struct _GabbleJingleTransportGoogleClass GabbleJingleTransportGoogleClass;

GType gabble_jingle_transport_google_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE \
  (gabble_jingle_transport_google_get_type ())
#define GABBLE_JINGLE_TRANSPORT_GOOGLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE, \
                              GabbleJingleTransportGoogle))
#define GABBLE_JINGLE_TRANSPORT_GOOGLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE, \
                           GabbleJingleTransportGoogleClass))
#define GABBLE_IS_JINGLE_TRANSPORT_GOOGLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE))
#define GABBLE_IS_JINGLE_TRANSPORT_GOOGLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE))
#define GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE, \
                              GabbleJingleTransportGoogleClass))

struct _GabbleJingleTransportGoogleClass {
    GObjectClass parent_class;
};

typedef struct _GabbleJingleTransportGooglePrivate GabbleJingleTransportGooglePrivate;

struct _GabbleJingleTransportGoogle {
    GObject parent;
    GabbleJingleTransportGooglePrivate *priv;
};

void jingle_transport_google_register (GabbleJingleFactory *factory);

#endif /* __JINGLE_TRANSPORT_GOOGLE_H__ */

