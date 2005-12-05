/* 
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * jabber-connection.h - Header for JabberConnection
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __JABBER_CONNECTION_H__
#define __JABBER_CONNECTION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _JabberConnection JabberConnection;
typedef struct _JabberConnectionClass JabberConnectionClass;
typedef struct _JabberConnectionPrivate JabberConnectionPrivate;

struct _JabberConnectionClass {
    GObjectClass parent_class;
};

struct _JabberConnection {
    GObject parent;
};

GType jabber_connection_get_type(void);

/* TYPE MACROS */
#define JABBER_TYPE_CONNECTION \
  (jabber_connection_get_type())
#define JABBER_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), JABBER_TYPE_CONNECTION, JabberConnection))
#define JABBER_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), JABBER_TYPE_CONNECTION, JabberConnectionClass))
#define JABBER_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), JABBER_TYPE_CONNECTION))
#define JABBER_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), JABBER_TYPE_CONNECTION))
#define JABBER_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), JABBER_TYPE_CONNECTION, JabberConnectionClass))


gboolean jabber_connection_disconnect (JabberConnection *obj, GError **error);
gboolean jabber_connection_inspect_handle (JabberConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error);
gboolean jabber_connection_release_handle (JabberConnection *obj, guint handle_type, guint handle, GError **error);
gboolean jabber_connection_request_channel (JabberConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean supress_handler, gpointer* ret, GError **error);
gboolean jabber_connection_request_handle (JabberConnection *obj, guint handle_type, const gchar * name, guint* ret, GError **error);
gboolean jabber_connection_get_protocol (JabberConnection *obj, gchar ** ret, GError **error);
gboolean jabber_connection_get_status (JabberConnection *obj, guint* ret, GError **error);
gboolean jabber_connection_list_channels (JabberConnection *obj, gpointer* ret, GError **error);
gboolean jabber_connection_hold_handle (JabberConnection *obj, guint handle_type, guint handle, GError **error);
gboolean jabber_connection_get_self_handle (JabberConnection *obj, guint* ret, GError **error);
gboolean jabber_connection_get_interfaces (JabberConnection *obj, gchar *** ret, GError **error);


G_END_DECLS

#endif /* #ifndef __JABBER_CONNECTION)_H__*/
