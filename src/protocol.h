/*
 * protocol.h - header for GabbleJabberProtocol
 * Copyright (C) 2007-2010 Collabora Ltd.
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

#ifndef JABBER_PROTOCOL_H
#define JABBER_PROTOCOL_H

#include <glib-object.h>
#include <telepathy-glib/base-protocol.h>

G_BEGIN_DECLS

typedef struct _GabbleJabberProtocol
    GabbleJabberProtocol;
typedef struct _GabbleJabberProtocolPrivate
    GabbleJabberProtocolPrivate;
typedef struct _GabbleJabberProtocolClass
    GabbleJabberProtocolClass;
typedef struct _GabbleJabberProtocolClassPrivate
    GabbleJabberProtocolClassPrivate;

struct _GabbleJabberProtocolClass {
    TpBaseProtocolClass parent_class;

    GabbleJabberProtocolClassPrivate *priv;
};

struct _GabbleJabberProtocol {
    TpBaseProtocol parent;

    GabbleJabberProtocolPrivate *priv;
};

GType gabble_jabber_protocol_get_type (void);

#define GABBLE_TYPE_JABBER_PROTOCOL \
    (gabble_jabber_protocol_get_type ())
#define GABBLE_JABBER_PROTOCOL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
        GABBLE_TYPE_JABBER_PROTOCOL, \
        GabbleJabberProtocol))
#define GABBLE_JABBER_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), \
        GABBLE_TYPE_JABBER_PROTOCOL, \
        GabbleJabberProtocolClass))
#define GABBLE_IS_JABBER_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
        GABBLE_TYPE_JABBER_PROTOCOL))
#define GABBLE_JABBER_PROTOCOL_GET_CLASS(klass) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
        GABBLE_TYPE_JABBER_PROTOCOL, \
        GabbleJabberProtocolClass))

gchar *gabble_jabber_protocol_normalize_contact (const gchar *id,
    GError **error);

G_END_DECLS

#endif
