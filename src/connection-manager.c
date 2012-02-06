/*
 * gabble-connection-manager.c - Source for GabbleConnectionManager
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

#include "config.h"
#include "connection-manager.h"

#include <string.h>

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <wocky/wocky.h>

#include "connection.h"
#include "debug.h"

#include "extensions/extensions.h"

#include "protocol.h"

G_DEFINE_TYPE(GabbleConnectionManager,
    gabble_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

/* type definition stuff */

static void
gabble_connection_manager_init (GabbleConnectionManager *self)
{
}

static void
gabble_connection_manager_constructed (GObject *object)
{
  GabbleConnectionManager *self = GABBLE_CONNECTION_MANAGER (object);
  TpBaseConnectionManager *base = (TpBaseConnectionManager *) self;
  void (*constructed) (GObject *) =
      ((GObjectClass *) gabble_connection_manager_parent_class)->constructed;
  TpBaseProtocol *protocol;

  if (constructed != NULL)
    constructed (object);

  protocol = gabble_jabber_protocol_new ();
  tp_base_connection_manager_add_protocol (base, protocol);
  g_object_unref (protocol);
}

static void
gabble_connection_manager_finalize (GObject *object)
{
  wocky_caps_cache_free_shared ();
  gabble_debug_free ();

  G_OBJECT_CLASS (gabble_connection_manager_parent_class)->finalize (object);
}

static void
gabble_connection_manager_class_init (GabbleConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseConnectionManagerClass *base_class =
    (TpBaseConnectionManagerClass *) klass;

  base_class->new_connection = NULL;
  base_class->cm_dbus_name = "gabble";
  base_class->protocol_params = NULL;
  object_class->constructed = gabble_connection_manager_constructed;
  object_class->finalize = gabble_connection_manager_finalize;
}

