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

#include <telepathy-glib/channel-factory-iface.h>

#include <glib-object.h>

#include <telepathy-glib/channel-iface.h>

#include "_gen/signals-marshal.h"

enum {
    NEW_CHANNEL,
    CHANNEL_ERROR,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

static void
tp_channel_factory_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    initialized = TRUE;

    signals[NEW_CHANNEL] = g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _tp_marshal_VOID__OBJECT_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_POINTER);

    signals[CHANNEL_ERROR] = g_signal_new ("channel-error",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _tp_marshal_VOID__OBJECT_POINTER_POINTER,
                  G_TYPE_NONE,
                  3, G_TYPE_OBJECT, G_TYPE_POINTER, G_TYPE_POINTER);
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
  void (*virtual_method)(TpChannelFactoryIface *) = 
    TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->close_all;
  g_assert (virtual_method != NULL);
  virtual_method (self);
}

void
tp_channel_factory_iface_connecting (TpChannelFactoryIface *self)
{
  void (*virtual_method)(TpChannelFactoryIface *) = 
    TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->connecting;
  if (virtual_method)
    virtual_method (self);
}

void
tp_channel_factory_iface_connected (TpChannelFactoryIface *self)
{
  void (*virtual_method)(TpChannelFactoryIface *) = 
    TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->connected;
  if (virtual_method)
    virtual_method (self);
}

void
tp_channel_factory_iface_disconnected (TpChannelFactoryIface *self)
{
  void (*virtual_method)(TpChannelFactoryIface *) = 
    TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->disconnected;
  if (virtual_method)
    virtual_method (self);
}

void
tp_channel_factory_iface_foreach (TpChannelFactoryIface *self,
                                  TpChannelFunc func,
                                  gpointer data)
{
  void (*virtual_method)(TpChannelFactoryIface *, TpChannelFunc, gpointer) = 
    TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->foreach;
  g_assert (virtual_method != NULL);
  virtual_method (self, func, data);
}

TpChannelFactoryRequestStatus
tp_channel_factory_iface_request (TpChannelFactoryIface *self,
                                  const gchar *chan_type,
                                  TpHandleType handle_type,
                                  guint handle,
                                  gpointer request,
                                  TpChannelIface **ret,
                                  GError **error)
{
  TpChannelFactoryRequestStatus (*virtual_method) (TpChannelFactoryIface *,
      const gchar *, TpHandleType, guint, gpointer, TpChannelIface **,
      GError **) = TP_CHANNEL_FACTORY_IFACE_GET_CLASS (self)->request;
  g_assert (virtual_method != NULL);
  return virtual_method (self, chan_type, handle_type, handle, request,
      ret, error);
}

void
tp_channel_factory_iface_emit_new_channel (gpointer instance,
                                           TpChannelIface *channel,
                                           gpointer context)
{
  g_signal_emit (instance, signals[NEW_CHANNEL], 0, channel, context);
}

void
tp_channel_factory_iface_emit_channel_error (gpointer instance,
                                             TpChannelIface *channel,
                                             GError *error,
                                             gpointer context)
{
  g_signal_emit (instance, signals[CHANNEL_ERROR], 0, channel, error, context);
}
