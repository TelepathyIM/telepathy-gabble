/*
 * dbus.h - Header for D-Bus helper functions for telepathy implementation
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

#ifndef __TELEPATHY_DBUS_H__
#define __TELEPATHY_DBUS_H__

#ifndef DBUS_API_SUBJECT_TO_CHANGE
#define DBUS_API_SUBJECT_TO_CHANGE 1
#endif

#include <glib.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

void tp_dbus_g_method_return_not_implemented (DBusGMethodInvocation *context);
DBusGConnection * tp_get_bus (void);
DBusGProxy * tp_get_bus_proxy (void);

G_END_DECLS

#endif /* __TELEPATHY_DBUS_H__ */

