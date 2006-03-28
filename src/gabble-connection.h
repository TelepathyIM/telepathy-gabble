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
#include <loudmouth/loudmouth.h>

#include "handles.h"

G_BEGIN_DECLS

typedef enum
{
  GABBLE_PRESENCE_AVAILABLE,
  GABBLE_PRESENCE_AWAY,
  GABBLE_PRESENCE_CHAT,
  GABBLE_PRESENCE_DND,
  GABBLE_PRESENCE_XA,
  GABBLE_PRESENCE_OFFLINE,
  LAST_GABBLE_PRESENCE
} GabblePresenceId;

typedef struct _GabbleConnection GabbleConnection;
typedef struct _GabbleConnectionClass GabbleConnectionClass;
typedef struct _ContactPresence ContactPresence;

typedef LmHandlerResult (*GabbleConnectionMsgReplyFunc) (GabbleConnection *conn,
                                                         LmMessage *sent_msg,
                                                         LmMessage *reply_msg,
                                                         GObject *object,
                                                         gpointer user_data);

struct _GabbleConnectionClass {
    GObjectClass parent_class;
};

struct _GabbleConnection {
    GObject parent;
};

struct _ContactPresence
{
  GabblePresenceId presence_id;
  gchar *status_message;
  gchar *voice_resource;
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

/* Convenience macros */
#define HANDLER_DEBUG(n, s) \
G_STMT_START { \
  gchar *handler_debug_tmp = lm_message_node_to_string (n); \
  g_debug ("%s: " s ":\n%s", G_STRFUNC, handler_debug_tmp); \
  g_free (handler_debug_tmp); \
} G_STMT_END


void _gabble_connection_set_properties_from_account (GabbleConnection *conn, const char *account);
gboolean _gabble_connection_register (GabbleConnection *conn, char **bus_name, char **object_path, GError **error);
gboolean _gabble_connection_connect (GabbleConnection *conn, GError **error);
GabbleHandleRepo *_gabble_connection_get_handles (GabbleConnection *conn);
gboolean _gabble_connection_send (GabbleConnection *conn, LmMessage *msg, GError **error);
gboolean _gabble_connection_send_with_reply (GabbleConnection *conn, LmMessage *msg, GabbleConnectionMsgReplyFunc reply_func, GObject *object, gpointer user_data, GError **error);
void _gabble_connection_send_iq_ack (GabbleConnection *conn, LmMessageNode *iq_node, LmMessageSubType type);
GQuark _get_contact_presence_quark();

void _gabble_connection_client_hold_handle (GabbleConnection *conn, gchar* client_name, GabbleHandle handle, TpHandleType type);
gboolean _gabble_connection_client_release_handle (GabbleConnection *conn, gchar* client_name, GabbleHandle handle, TpHandleType type);

guint32 _gabble_connection_jingle_session_allocate (GabbleConnection *conn);
void _gabble_connection_jingle_session_register (GabbleConnection *conn, guint32 sid, gpointer channel);
void _gabble_connection_jingle_session_unregister (GabbleConnection *conn, guint32 sid);

gboolean _gabble_connection_contact_supports_voice (GabbleConnection *conn, GabbleHandle handle);

gboolean gabble_connection_add_status (GabbleConnection *obj, const gchar * status, GHashTable * parms, GError **error);
gboolean gabble_connection_advertise_capabilities (GabbleConnection *obj, const gchar ** add, const gchar ** remove, GError **error);
gboolean gabble_connection_clear_status (GabbleConnection *obj, GError **error);
gboolean gabble_connection_disconnect (GabbleConnection *obj, GError **error);
gboolean gabble_connection_get_capabilities (GabbleConnection *obj, guint handle, GPtrArray ** ret, GError **error);
gboolean gabble_connection_get_interfaces (GabbleConnection *obj, gchar *** ret, GError **error);
gboolean gabble_connection_get_protocol (GabbleConnection *obj, gchar ** ret, GError **error);
gboolean gabble_connection_get_self_handle (GabbleConnection *obj, guint* ret, GError **error);
gboolean gabble_connection_get_status (GabbleConnection *obj, guint* ret, GError **error);
gboolean gabble_connection_get_statuses (GabbleConnection *obj, GHashTable ** ret, GError **error);
gboolean gabble_connection_hold_handle (GabbleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context);
gboolean gabble_connection_inspect_handle (GabbleConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error);
gboolean gabble_connection_list_channels (GabbleConnection *obj, GPtrArray ** ret, GError **error);
gboolean gabble_connection_release_handle (GabbleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context);
gboolean gabble_connection_remove_status (GabbleConnection *obj, const gchar * status, GError **error);
gboolean gabble_connection_request_channel (GabbleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean supress_handler, gchar ** ret, GError **error);
gboolean gabble_connection_request_contact_info (GabbleConnection *obj, guint contact, GError **error);
gboolean gabble_connection_request_handle (GabbleConnection *obj, guint handle_type, const gchar * name, DBusGMethodInvocation *context);
gboolean gabble_connection_request_presence (GabbleConnection *obj, const GArray * contacts, GError **error);
gboolean gabble_connection_set_last_activity_time (GabbleConnection *obj, guint time, GError **error);
gboolean gabble_connection_set_status (GabbleConnection *obj, GHashTable * statuses, GError **error);



G_END_DECLS

#endif /* #ifndef __GABBLE_CONNECTION_H__*/
