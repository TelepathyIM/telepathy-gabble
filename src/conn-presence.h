/*
 * conn-presence.h - Header for Gabble connection presence interface
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
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

#ifndef __CONN_PRESENCE_H__
#define __CONN_PRESENCE_H__

#include <glib.h>

#include "connection.h"

G_BEGIN_DECLS

typedef enum {
    CONN_PRESENCE_ERROR_SET_INVISIBLE = 0,
    CONN_PRESENCE_ERROR_CREATE_PRIVACY_LIST,
    CONN_PRESENCE_ERROR_RETRIEVE_PRIVACY_LIST,
    CONN_PRESENCE_ERROR_INVALID_PRIVACY_LIST,
    CONN_PRESENCE_ERROR_SET_PRIVACY_LIST,
    CONN_PRESENCE_ERROR_SET_INVISIBLE_PRESENCE
} GabbleConnPresenceErrorType;

GQuark conn_presence_error_quark (void);
#define CONN_PRESENCE_ERROR (conn_presence_error_quark ())

void conn_presence_class_init (GabbleConnectionClass *klass);
void conn_presence_init (GabbleConnection *conn);
void conn_presence_finalize (GabbleConnection *conn);
void conn_presence_iface_init (gpointer g_iface, gpointer iface_data);
void conn_presence_emit_presence_update (
    GabbleConnection *, const GArray *contact_handles);
gboolean conn_presence_signal_own_presence (GabbleConnection *self,
    const gchar *to, GError **error);
gboolean conn_presence_visible_to (GabbleConnection *self,
    TpHandle recipient);
void conn_presence_set_initial_presence_async (GabbleConnection *self,
    GAsyncReadyCallback callback, gpointer user_data);
gboolean conn_presence_set_initial_presence_finish (GabbleConnection *self,
    GAsyncResult *result, GError **error);

void conn_decloak_iface_init (gpointer g_iface, gpointer iface_data);
void conn_decloak_emit_requested (GabbleConnection *conn,
    TpHandle contact, const gchar *reason, gboolean decloaked);

GabblePresenceId conn_presence_get_type (GabblePresence *presence);

const TpPresenceStatusSpec *conn_presence_statuses (void);

G_END_DECLS

#endif /* __CONN_PRESENCE_H__ */

