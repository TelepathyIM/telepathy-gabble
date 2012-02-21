/*
 * jingle-transport-google.h - Header for WockyJingleTransportGoogle
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

#include "jingle-types.h"

G_BEGIN_DECLS

typedef struct _WockyJingleTransportGoogleClass WockyJingleTransportGoogleClass;

GType wocky_jingle_transport_google_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_TRANSPORT_GOOGLE \
  (wocky_jingle_transport_google_get_type ())
#define WOCKY_JINGLE_TRANSPORT_GOOGLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_TRANSPORT_GOOGLE, \
                              WockyJingleTransportGoogle))
#define WOCKY_JINGLE_TRANSPORT_GOOGLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_TRANSPORT_GOOGLE, \
                           WockyJingleTransportGoogleClass))
#define WOCKY_IS_JINGLE_TRANSPORT_GOOGLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_TRANSPORT_GOOGLE))
#define WOCKY_IS_JINGLE_TRANSPORT_GOOGLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_TRANSPORT_GOOGLE))
#define WOCKY_JINGLE_TRANSPORT_GOOGLE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_TRANSPORT_GOOGLE, \
                              WockyJingleTransportGoogleClass))

struct _WockyJingleTransportGoogleClass {
    GObjectClass parent_class;
};

typedef struct _WockyJingleTransportGooglePrivate WockyJingleTransportGooglePrivate;

struct _WockyJingleTransportGoogle {
    GObject parent;
    WockyJingleTransportGooglePrivate *priv;
};

void jingle_transport_google_register (WockyJingleFactory *factory);

gboolean jingle_transport_google_set_component_name (
    WockyJingleTransportGoogle *transport,
    const gchar *name, guint component_id);

#endif /* __JINGLE_TRANSPORT_GOOGLE_H__ */

