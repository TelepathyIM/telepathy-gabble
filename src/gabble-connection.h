/*
 * gabble-connection.h - Header for GabbleConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __GABBLE_CONNECTION_H__
#define __GABBLE_CONNECTION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleConnection GabbleConnection;
typedef struct _GabbleConnectionClass GabbleConnectionClass;

struct _GabbleConnectionClass {
    GObjectClass parent_class;
};

struct _GabbleConnection {
    GObject parent;
};

GType gabble_connection_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_CONNECTION \
  (gabble_connection_get_type())
#define GABBLE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CONNECTION, GabbleConnection))
#define GABBLE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CONNECTION, GabbleConnectionClass))
#define GABBLE_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CONNECTION))
#define GABBLE_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CONNECTION))
#define GABBLE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CONNECTION, GabbleConnectionClass))

void _gabble_connection_set_properties_from_account (GabbleConnection *conn, const char *account);
gboolean _gabble_connection_connect (GabbleConnection *conn, GError **error);

gboolean gabble_connection_disconnect (GabbleConnection *obj, GError **error);
gboolean gabble_connection_inspect_handle (GabbleConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error);
gboolean gabble_connection_release_handle (GabbleConnection *obj, guint handle_type, guint handle, GError **error);
gboolean gabble_connection_request_channel (GabbleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean supress_handler, gpointer* ret, GError **error);
gboolean gabble_connection_request_handle (GabbleConnection *obj, guint handle_type, const gchar * name, guint* ret, GError **error);
gboolean gabble_connection_get_protocol (GabbleConnection *obj, gchar ** ret, GError **error);
gboolean gabble_connection_get_status (GabbleConnection *obj, guint* ret, GError **error);
gboolean gabble_connection_list_channels (GabbleConnection *obj, gpointer* ret, GError **error);
gboolean gabble_connection_hold_handle (GabbleConnection *obj, guint handle_type, guint handle, GError **error);
gboolean gabble_connection_get_self_handle (GabbleConnection *obj, guint* ret, GError **error);
gboolean gabble_connection_get_interfaces (GabbleConnection *obj, gchar *** ret, GError **error);


G_END_DECLS

#endif /* #ifndef __GABBLE_CONNECTION)_H__*/
