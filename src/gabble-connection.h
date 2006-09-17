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

#include <dbus/dbus-glib.h>
#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include "gabble-types.h"
#include "gabble-error.h"
#include "properties-mixin.h"
#include "telepathy-constants.h"

G_BEGIN_DECLS

/* Default parameters for optional parameters */
#define GABBLE_PARAMS_DEFAULT_RESOURCE                   "Telepathy"
#define GABBLE_PARAMS_DEFAULT_PORT                       5222
#define GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT           443
#define GABBLE_PARAMS_DEFAULT_STUN_PORT                  3478

/* order must match array of statuses in gabble-connection.c */
/* in increasing order of presence */
typedef enum
{
  GABBLE_PRESENCE_OFFLINE = 0,
  GABBLE_PRESENCE_HIDDEN,
  GABBLE_PRESENCE_XA,
  GABBLE_PRESENCE_AWAY,
  GABBLE_PRESENCE_DND,
  GABBLE_PRESENCE_AVAILABLE,
  GABBLE_PRESENCE_CHAT,
  LAST_GABBLE_PRESENCE
} GabblePresenceId;

typedef enum
{
  GABBLE_CONNECTION_FEATURES_NONE = 0,
  GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO = 1 << 0,
  GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER = 1 << 1,
  GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE = 1 << 2,
  GABBLE_CONNECTION_FEATURES_PRIVACY = 1 << 3
} GabbleConnectionFeatures;

typedef struct _GabbleConnectionClass GabbleConnectionClass;

typedef LmHandlerResult (*GabbleConnectionMsgReplyFunc) (GabbleConnection *conn,
                                                         LmMessage *sent_msg,
                                                         LmMessage *reply_msg,
                                                         GObject *object,
                                                         gpointer user_data);

struct _GabbleConnectionClass {
    GObjectClass parent_class;

    GabblePropertiesMixinClass properties_class;
};

struct _GabbleConnection {
    GObject parent;

    GabblePropertiesMixin properties;

    /* dbus object location */
    gchar *bus_name;
    gchar *object_path;

    /* loudmouth connection */
    LmConnection *lmconn;

    /* connection status */
    TpConnectionStatus status;

    /* handles */
    GabbleHandleRepo *handles;
    GabbleHandle self_handle;

    /* roster */
    GabbleRoster *roster;

    /* DISCO! */
    GabbleDisco *disco;

    /* connection feature flags */
    GabbleConnectionFeatures features;

    /* presence */
    GabblePresenceCache *presence_cache;

    gpointer priv;
};

typedef enum {
    GABBLE_CONNECTION_ALIAS_NONE = 0,
    GABBLE_CONNECTION_ALIAS_FROM_JID,
    GABBLE_CONNECTION_ALIAS_FROM_CONNMGR,
    GABBLE_CONNECTION_ALIAS_FROM_VCARD,
    GABBLE_CONNECTION_ALIAS_FROM_PRESENCE,
    GABBLE_CONNECTION_ALIAS_FROM_ROSTER
} GabbleConnectionAliasSource;

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

gboolean _gabble_connection_set_properties_from_account (GabbleConnection *conn, const gchar *account, GError **error);
gboolean _gabble_connection_register (GabbleConnection *conn, char **bus_name, char **object_path, GError **error);
gboolean _gabble_connection_connect (GabbleConnection *conn, GError **error);
gboolean _gabble_connection_send (GabbleConnection *conn, LmMessage *msg, GError **error);
gboolean _gabble_connection_send_with_reply (GabbleConnection *conn, LmMessage *msg, GabbleConnectionMsgReplyFunc reply_func, GObject *object, gpointer user_data, GError **error);
void _gabble_connection_acknowledge_set_iq (GabbleConnection *conn, LmMessage *iq);
void _gabble_connection_send_iq_error (GabbleConnection *conn, LmMessage *message, GabbleXmppError error);

GabbleConnectionAliasSource _gabble_connection_get_cached_alias (GabbleConnection *, GabbleHandle, gchar **);

const char *_gabble_connection_find_conference_server (GabbleConnection *);

gboolean
gabble_connection_add_status (GabbleConnection *self,
                              const gchar *status,
                              GHashTable *parms,
                              GError **error);

gboolean
gabble_connection_advertise_capabilities (GabbleConnection *self,
                                          const GPtrArray *add,
                                          const gchar **remove,
                                          GPtrArray **ret,
                                          GError **error);

gboolean
gabble_connection_clear_status (GabbleConnection *self,
                                GError **error);

gboolean
gabble_connection_connect (GabbleConnection *self,
                           GError **error);

gboolean
gabble_connection_disconnect (GabbleConnection *self,
                              GError **error);

gboolean
gabble_connection_get_alias_flags (GabbleConnection *self,
                                   guint *ret,
                                   GError **error);

gboolean
gabble_connection_get_capabilities (GabbleConnection *self,
                                    const GArray *handles,
                                    GPtrArray **ret,
                                    GError **error);

gboolean
gabble_connection_get_interfaces (GabbleConnection *self,
                                  gchar ***ret,
                                  GError **error);

gboolean
gabble_connection_get_properties (GabbleConnection *self,
                                  const GArray *properties,
                                  GPtrArray **ret,
                                  GError **error);

gboolean
gabble_connection_get_protocol (GabbleConnection *self,
                                gchar **ret,
                                GError **error);

gboolean
gabble_connection_get_self_handle (GabbleConnection *self,
                                   guint *ret,
                                   GError **error);

gboolean
gabble_connection_get_status (GabbleConnection *self,
                              guint *ret,
                              GError **error);

gboolean
gabble_connection_get_statuses (GabbleConnection *self,
                                GHashTable **ret,
                                GError **error);

void
gabble_connection_hold_handles (GabbleConnection *self,
                                guint handle_type,
                                const GArray *handles,
                                DBusGMethodInvocation *context);

void
gabble_connection_inspect_handles (GabbleConnection *self,
                                   guint handle_type,
                                   const GArray *handles,
                                   DBusGMethodInvocation *context);

gboolean
gabble_connection_list_channels (GabbleConnection *self,
                                 GPtrArray **ret,
                                 GError **error);

gboolean
gabble_connection_list_properties (GabbleConnection *self,
                                   GPtrArray **ret,
                                   GError **error);

void
gabble_connection_release_handles (GabbleConnection *self,
                                   guint handle_type,
                                   const GArray *handles,
                                   DBusGMethodInvocation *context);

gboolean
gabble_connection_remove_status (GabbleConnection *self,
                                 const gchar *status,
                                 GError **error);

gboolean
gabble_connection_request_aliases (GabbleConnection *self,
                                   const GArray *contacts,
                                   gchar ***ret,
                                   GError **error);

void
gabble_connection_request_channel (GabbleConnection *self,
                                   const gchar *type,
                                   guint handle_type,
                                   guint handle,
                                   gboolean suppress_handler,
                                   DBusGMethodInvocation *context);

void
gabble_connection_request_handles (GabbleConnection *self,
                                   guint handle_type,
                                   const gchar **names,
                                   DBusGMethodInvocation *context);

gboolean
gabble_connection_request_presence (GabbleConnection *self,
                                    const GArray *contacts,
                                    GError **error);

gboolean
gabble_connection_set_aliases (GabbleConnection *self,
                               GHashTable *aliases,
                               GError **error);

gboolean
gabble_connection_set_last_activity_time (GabbleConnection *self,
                                          guint time,
                                          GError **error);

void
gabble_connection_set_properties (GabbleConnection *self,
                                  const GPtrArray *properties,
                                  DBusGMethodInvocation *context);

gboolean
gabble_connection_set_status (GabbleConnection *self,
                              GHashTable *statuses,
                              GError **error);

G_END_DECLS

#endif /* #ifndef __GABBLE_CONNECTION_H__*/
