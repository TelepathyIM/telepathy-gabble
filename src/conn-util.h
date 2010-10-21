/*
 * conn-util.h - Header for Gabble connection kitchen-sink code.
 * Copyright (C) 2010 Collabora Ltd.
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

#ifndef GABBLE_CONN_UTIL_H
#define GABBLE_CONN_UTIL_H

#include <glib.h>

#include "connection.h"

G_BEGIN_DECLS

void conn_util_send_iq_async (GabbleConnection *self,
    WockyStanza *stanza,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean conn_util_send_iq_finish (GabbleConnection *self,
    GAsyncResult *result,
    WockyStanza **response,
    GError **error);

const gchar *conn_util_get_bare_self_jid (GabbleConnection *conn);

G_END_DECLS

#endif /* GABBLE_CONN_UTIL_H */
