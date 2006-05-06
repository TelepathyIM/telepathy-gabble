/*
 * tp-channel-factory-iface.c - Stubs for Telepathy Channel Factory interface
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#include <glib-object.h>

#include "tp-channel-factory-iface.h"
#include "tp-channel-iface.h"

static void
tp_channel_factory_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    initialized = TRUE;

    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, G_TYPE_OBJECT);
  }
}

GType
tp_channel_factory_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (TpChannelFactoryIfaceClass),
      tp_channel_factory_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "TpChannelFactoryIface", &info, 0);
  }

  return type;
}

void
tp_channel_factory_iface_close_all (TpChannelFactoryIface *self)
{
  TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->close_all (self);
}

void
tp_channel_factory_iface_connected (TpChannelFactoryIface *self)
{
  TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->connected (self);
}

void
tp_channel_factory_iface_disconnected (TpChannelFactoryIface *self)
{
  TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->disconnected (self);
}

void
tp_channel_factory_iface_foreach (TpChannelFactoryIface *self,
                                  TpChannelFunc func,
                                  gpointer data)
{
  TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->foreach (self, func, data);
}

TpChannelFactoryRequestStatus
tp_channel_factory_iface_request (TpChannelFactoryIface *self,
                                  const gchar *chan_type,
                                  TpHandleType handle_type,
                                  guint handle,
                                  TpChannelIface **ret)
{
  return (TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->request (self, chan_type,
        handle_type, handle_type, ret));
}
