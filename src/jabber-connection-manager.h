/* 
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * jabber-connection-manager.h- Header for JabberConnectionManager
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

#ifndef __JABBER_CONNECTION_MANAGER_H__
#define __JABBER_CONNECTION_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _JabberConnectionManager JabberConnectionManager;
typedef struct _JabberConnectionManagerClass JabberConnectionManagerClass;
typedef struct _JabberConnectionManagerPrivate JabberConnectionManagerPrivate;

struct _JabberConnectionManagerClass {
    GObjectClass parent_class;
};

struct _JabberConnectionManager {
    GObject parent;
};

GType jabber_connection_manager_get_type(void);

/* TYPE MACROS */
#define JABBER_TYPE_CONNECTION_MANAGER \
  (jabber_connection_manager_get_type())
#define JABBER_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), JABBER_TYPE_CONNECTION_MANAGER, JabberConnectionManager))
#define JABBER_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), JABBER_TYPE_CONNECTION_MANAGER, JabberConnectionManagerClass))
#define JABBER_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), JABBER_TYPE_CONNECTION_MANAGER))
#define JABBER_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), JABBER_TYPE_CONNECTION_MANAGER))
#define JABBER_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), JABBER_TYPE_CONNECTION_MANAGER, JabberConnectionManagerClass))


gboolean jabber_connection_manager_connect (JabberConnectionManager *obj, const gchar * proto, const GHashTable * parameters, gchar ** ret, gpointer* ret1, GError **error);
gboolean jabber_connection_manager_get_parameter_defaults (JabberConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error);
gboolean jabber_connection_manager_get_optional_parameters (JabberConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error);
gboolean jabber_connection_manager_get_mandatory_parameters (JabberConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error);
gboolean jabber_connection_manager_list_protocols (JabberConnectionManager *obj, gchar *** ret, GError **error);


G_END_DECLS

#endif /* #ifndef __JABBER_CONNECTION_MANAGER)_H__*/
