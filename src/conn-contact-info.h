/*
 * conn-contact-info.h - Header for Gabble connection ContactInfo interface
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __CONN_CONTACT_INFO_H__
#define __CONN_CONTACT_INFO_H__

#include "connection.h"

G_BEGIN_DECLS

void conn_contact_info_class_init (GabbleConnectionClass *klass);
void conn_contact_info_init (GabbleConnection *conn);
void conn_contact_info_iface_init (gpointer g_iface, gpointer iface_data);

extern TpDBusPropertiesMixinPropImpl *conn_contact_info_properties;
void conn_contact_info_properties_getter (GObject *object, GQuark interface,
    GQuark name, GValue *value, gpointer getter_data);

G_END_DECLS

#endif /* __CONN_CONTACT_INFO_H__ */

