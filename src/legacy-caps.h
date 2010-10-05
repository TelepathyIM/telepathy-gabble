/*
 * legacy-caps.h - Connection.Interface.Capabilities constants and utilities
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

#ifndef __GABBLE_LEGACY_CAPS__H__
#define __GABBLE_LEGACY_CAPS__H__

#include <glib-object.h>

#include "capabilities.h"

typedef void (*TypeFlagsToCapsFunc) (guint typeflags, GabbleCapabilitySet *caps);
typedef guint (*CapsToTypeFlagsFunc) (const GabbleCapabilitySet *caps);

typedef struct _CapabilityConversionData CapabilityConversionData;

struct _CapabilityConversionData
{
  const gchar *iface;
  TypeFlagsToCapsFunc tf2c_fn;
  CapsToTypeFlagsFunc c2tf_fn;
};

extern const CapabilityConversionData capabilities_conversions[];

#endif
