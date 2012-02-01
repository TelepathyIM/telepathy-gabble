/*
 * error.h — error API available to telepathy-gabble plugins (and internals)
 * Copyright © 2010 Collabora Ltd.
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

#ifndef GABBLE_PLUGINS_ERROR_H
#define GABBLE_PLUGINS_ERROR_H

#include <glib.h>

#include <wocky/wocky.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

void gabble_set_tp_error_from_wocky (const GError *wocky_error,
    GError **error);

void gabble_set_tp_conn_error_from_wocky (const GError *wocky_error,
    TpConnectionStatus previous_status,
    TpConnectionStatusReason *conn_reason,
    GError **error);

G_END_DECLS

#endif
