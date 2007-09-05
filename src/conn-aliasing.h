/*
 * conn-aliasing.h - Header for Gabble connection aliasing interface
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

#ifndef __CONN_ALIASING_H__
#define __CONN_ALIASING_H__

#include <glib.h>

#include "gabble-connection.h"

G_BEGIN_DECLS

void conn_aliasing_iface_init (gpointer g_iface, gpointer iface_data);

gboolean gabble_conn_aliasing_pep_nick_event_handler (GabbleConnection *conn,
    LmMessage *msg, TpHandle handle);

GQuark gabble_conn_aliasing_pep_alias_quark (void);

G_END_DECLS

#endif /* __CONN_ALIASING_H__ */

