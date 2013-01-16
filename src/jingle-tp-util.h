/*
 * jingle-tp-util.h - Header for Telepathy-flavoured Jingle utility functions
 * Copyright © 2008–2012 Collabora Ltd.
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

#ifndef GABBLE_JINGLE_TP_UTIL_H
#define GABBLE_JINGLE_TP_UTIL_H

#include <telepathy-glib/telepathy-glib.h>
#include <wocky/wocky.h>

WockyJingleMediaType wocky_jingle_media_type_from_tp (TpMediaStreamType type);
TpMediaStreamType wocky_jingle_media_type_to_tp (WockyJingleMediaType type);

GPtrArray *gabble_build_tp_relay_info (GPtrArray *relays);

#endif /* GABBLE_JINGLE_TP_UTIL_H */
