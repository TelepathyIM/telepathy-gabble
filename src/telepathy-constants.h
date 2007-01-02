/*
 * telepathy-constants.h - constants used in telepathy
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

#ifndef __TELEPATHY_CONSTANTS_H__
#define __TELEPATHY_CONSTANTS_H__

#include "telepathy-spec-enums.h"

/* this is internal to Gabble and behaves like a member of TpConnectionStatus */
#define TP_CONNECTION_STATUS_NEW ((TpConnectionStatus)(LAST_TP_CONNECTION_STATUS + 1))

#endif
