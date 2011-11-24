/*
 * conn-addressing.h - Header for Gabble connection code handling addressing.
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

#ifndef GABBLE_CONN_ADDRESSING_H
#define GABBLE_CONN_ADDRESSING_H

#include <glib.h>

#include "connection.h"

G_BEGIN_DECLS

void conn_addressing_iface_init (gpointer g_iface, gpointer iface_data);
void conn_addressing_init (GabbleConnection *self);

G_END_DECLS

#endif /* GABBLE_CONN_ADDRESSING_H */
