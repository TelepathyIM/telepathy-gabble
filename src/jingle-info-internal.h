/*
 * jingle-info-internal.h - internal types for GabbleJingleInfo
 * Copyright © 2012 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef GABBLE_JINGLE_INFO_INTERNAL_H
#define GABBLE_JINGLE_INFO_INTERNAL_H

typedef enum {
    GABBLE_STUN_SERVER_USER_SPECIFIED,
    GABBLE_STUN_SERVER_DISCOVERED,
    GABBLE_STUN_SERVER_FALLBACK
} GabbleStunServerSource;

#endif /* GABBLE_JINGLE_INFO_INTERNAL_H */
