/*
 * legacy-caps.c - Connection.Interface.Capabilities constants and utilities
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

#include "config.h"
#include "legacy-caps.h"

#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE
#include "debug.h"
#include "media-factory.h"

const CapabilityConversionData capabilities_conversions[] =
{
  { TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
    _gabble_media_factory_typeflags_to_caps,
    _gabble_media_factory_caps_to_typeflags },
  { NULL, NULL, NULL}
};
