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

    gpointer priv;
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
gabble_connection_get_avatar_requirements (GabbleConnection *self,
                                           gchar ***ret,
                                           guint *ret1,
                                           guint *ret2,
                                           guint *ret3,
                                           guint *ret4,
                                           guint *ret5,
                                           GError **error);

void
gabble_connection_get_avatar_tokens (GabbleConnection *self,
                                     const GArray *contacts,
                                     DBusGMethodInvocation *context);

gboolean
gabble_connection_get_capabilities (GabbleConnection *self,
                                    const GArray *handles,
                                    GPtrArray **ret,
                                    GError **error);

gboolean
gabble_connection_get_interfaces (GabbleConnection *self,
                                  gchar ***ret,
                                  GError **error);

void
gabble_connection_get_presence (GabbleConnection *self,
                                const GArray *contacts,
                                DBusGMethodInvocation *context);

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

void
gabble_connection_request_aliases (GabbleConnection *self,
                                   const GArray *contacts,
                                   DBusGMethodInvocation *context);

void
gabble_connection_request_avatar (GabbleConnection *self,
                                  guint contact,
                                  DBusGMethodInvocation *context);

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

void
gabble_connection_set_avatar (GabbleConnection *self,
                              const GArray *avatar,
                              const gchar *mime_type,
                              DBusGMethodInvocation *context);

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
