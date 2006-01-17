/*
 * telepathy-interfaces.h - Header for Telepathy interface names
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

#ifndef __TELEPATHY_INTERFACES_H__
#define __TELEPATHY_INTERFACES_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define TP_IFACE_CHANNEL_INTERFACE \
        "org.freedesktop.Telepathy.Channel"
#define TP_IFACE_CHANNEL_INTERFACE_DTMF \
        "org.freedesktop.Telepathy.Channel.Interface.DTMF"
#define TP_IFACE_CHANNEL_INTERFACE_GROUP \
        "org.freedesktop.Telepathy.Channel.Interface.Group"
#define TP_IFACE_CHANNEL_INTERFACE_HOLD \
        "org.freedesktop.Telepathy.Channel.Interface.Hold"
#define TP_IFACE_CHANNEL_INTERFACE_PASSWORD \
        "org.freedesktop.Telepathy.Channel.Interface.Password"
#define TP_IFACE_CHANNEL_INTERFACE_SUBJECT \
        "org.freedesktop.Telepathy.Channel.Interface.Subject"
#define TP_IFACE_CHANNEL_INTERFACE_TRANSFER \
        "org.freedesktop.Telepathy.Channel.Interface.Transfer"
#define TP_IFACE_CHANNEL_TYPE_CONTACT_LIST \
        "org.freedesktop.Telepathy.Channel.Type.ContactList"
#define TP_IFACE_CHANNEL_TYPE_CONTACT_SEARCH \
        "org.freedesktop.Telepathy.Channel.Type.ContactSearch"
#define TP_IFACE_CHANNEL_TYPE_ROOM_LIST \
        "org.freedesktop.Telepathy.Channel.Type.RoomList"
#define TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA \
        "org.freedesktop.Telepathy.Channel.Type.StreamedMedia"
#define TP_IFACE_CHANNEL_TYPE_TEXT \
        "org.freedesktop.Telepathy.Channel.Type.Text"
#define TP_IFACE_CONN_INTERFACE \
        "org.freedesktop.Telepathy.Connection"
#define TP_IFACE_CONN_INTERFACE_ALIASING \
        "org.freedesktop.Telepathy.Connection.Interface.Aliasing"
#define TP_IFACE_CONN_INTERFACE_CAPABILITIES \
        "org.freedesktop.Telepathy.Connection.Interface.Capabilities"
#define TP_IFACE_CONN_INTERFACE_CONTACT_INFO \
        "org.freedesktop.Telepathy.Connection.Interface.ContactInfo"
#define TP_IFACE_CONN_INTERFACE_FORWARDING \
        "org.freedesktop.Telepathy.Connection.Interface.Forwarding"
#define TP_IFACE_CONN_INTERFACE_PRESENCE \
        "org.freedesktop.Telepathy.Connection.Interface.Presence"
#define TP_IFACE_CONN_INTERFACE_PRIVACY \
        "org.freedesktop.Telepathy.Connection.Interface.Privacy"
#define TP_IFACE_CONN_INTERFACE_RENAMING \
        "org.freedesktop.Telepathy.Connection.Interface.Renaming"
#define TP_IFACE_CONN_MGR_INTERFACE \
        "org.freedesktop.Telepathy.ConnectionManager"
#define TP_IFACE_MEDIA_SESSION_HANDLER \
        "org.freedesktop.Telepathy.Media.SessionHandler"
#define TP_IFACE_MEDIA_STREAM_HANDLER \
        "org.freedesktop.Telepathy.Media.StreamHandler"

G_END_DECLS

#endif /* #ifndef __TELEPATHY_INTERFACES_H__*/
