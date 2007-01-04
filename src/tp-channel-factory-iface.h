/*
 * tp-channel-factory-iface.h - Headers for Telepathy Channel Factory interface
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

#ifndef __TP_CHANNEL_FACTORY_IFACE_H__
#define __TP_CHANNEL_FACTORY_IFACE_H__

#include <glib-object.h>

#include <telepathy-glib/tp-enums.h>
#include "tp-channel-iface.h"

G_BEGIN_DECLS

#define TP_TYPE_CHANNEL_FACTORY_IFACE tp_channel_factory_iface_get_type()

#define TP_CHANNEL_FACTORY_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  TP_TYPE_CHANNEL_FACTORY_IFACE, TpChannelFactoryIface))

#define TP_CHANNEL_FACTORY_IFACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  TP_TYPE_CHANNEL_FACTORY_IFACE, TpChannelFactoryIfaceClass))

#define TP_IS_CHANNEL_FACTORY_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  TP_TYPE_CHANNEL_FACTORY_IFACE))

#define TP_IS_CHANNEL_FACTORY_IFACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  TP_TYPE_CHANNEL_FACTORY_IFACE))

#define TP_CHANNEL_FACTORY_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  TP_TYPE_CHANNEL_FACTORY_IFACE, TpChannelFactoryIfaceClass))

typedef struct _TpChannelFactoryIface TpChannelFactoryIface;
typedef struct _TpChannelFactoryIfaceClass TpChannelFactoryIfaceClass;

typedef enum {
  TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED = 0,
  TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE,
  TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE,
  TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR,
  TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE,
  TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED
} TpChannelFactoryRequestStatus;

struct _TpChannelFactoryIfaceClass {
  GTypeInterface parent_class;

  void (*close_all) (TpChannelFactoryIface *);
  void (*connecting) (TpChannelFactoryIface *);
  void (*connected) (TpChannelFactoryIface *);
  void (*disconnected) (TpChannelFactoryIface *);
  void (*foreach) (TpChannelFactoryIface *, TpChannelFunc func, gpointer data);
  TpChannelFactoryRequestStatus (*request) (TpChannelFactoryIface *, const gchar *chan_type, TpHandleType handle_type, guint handle, TpChannelIface **ret, GError **error);
};

GType tp_channel_factory_iface_get_type (void);

void tp_channel_factory_iface_close_all (TpChannelFactoryIface *);
void tp_channel_factory_iface_connecting (TpChannelFactoryIface *);
void tp_channel_factory_iface_connected (TpChannelFactoryIface *);
void tp_channel_factory_iface_disconnected (TpChannelFactoryIface *);
void tp_channel_factory_iface_foreach (TpChannelFactoryIface *, TpChannelFunc func, gpointer data);
TpChannelFactoryRequestStatus tp_channel_factory_iface_request (TpChannelFactoryIface *, const gchar *chan_type, TpHandleType handle_type, guint handle, TpChannelIface **ret, GError **error);

G_END_DECLS

#endif /* __TP_CHANNEL_FACTORY_IFACE_H__ */
