/*
 * conn-sidecars.h - Header for Gabble connection implementation of sidecars
 * Copyright © 2009 Collabora Ltd.
 * Copyright © 2009 Nokia Corporation
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

#ifndef __CONN_SIDECARS_H__
#define __CONN_SIDECARS_H__

#include "connection.h"

G_BEGIN_DECLS

void conn_sidecars_init (GabbleConnection *conn);
void conn_sidecars_dispose (GabbleConnection *conn);
void conn_future_iface_init (gpointer g_iface, gpointer iface_data);

G_END_DECLS

#endif /* __CONN_SIDECARS_H__ */

