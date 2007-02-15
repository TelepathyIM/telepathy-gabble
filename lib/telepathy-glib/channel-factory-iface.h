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

#include <telepathy-glib/enums.h>
#include <telepathy-glib/channel-iface.h>

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
  /** Same as the Telepathy error NotImplemented. *ret and *error are not set */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED = 0,
  /** Same as the Telepathy error NotAvailable. *ret and *error are not set */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE,
  /** Same as the Telepathy error InvalidHandle. *ret and *error are not set */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE,
  /** An error other than the above. *ret is not set, *error is set */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR,
  /** A new channel was created (possibly in response to more than one
   * request). new-channel has already been emitted and *ret is set to
   * the new channel. */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED,
  /** A new channel will be created, or was created but is not ready yet.
   * Either new-channel or channel-error will be emitted later.
   * *ret and *error are not set.
   * */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED,
  /** An existing channel satisfies the request: new-channel was not emitted.
   * *ret is set to the existing channel. */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING
} TpChannelFactoryRequestStatus;

struct _TpChannelFactoryIfaceClass {
  GTypeInterface parent_class;

  /** Close all channels and shut down the channel factory. It is not expected
   * to be usable afterwards. This is called when the connection goes to
   * disconnected state, before emitting the StatusChanged signal or calling
   * disconnected(). May not be NULL.
   */
  void (*close_all) (TpChannelFactoryIface *);

  /** Called just after the connection goes from disconnected to connecting
   * state. May be NULL if nothing special needs to happen. */
  void (*connecting) (TpChannelFactoryIface *);

  /** Called just after the connection goes from connecting to connected
   * state. May be NULL if nothing special needs to happen. */
  void (*connected) (TpChannelFactoryIface *);

  /** Called just after the connection goes to disconnected state. This is
   * always called after close_all(). May be NULL if nothing special needs to
   * happen. */
  void (*disconnected) (TpChannelFactoryIface *);

  /** Call func for each channel owned by this factory. May not be NULL. */
  void (*foreach) (TpChannelFactoryIface *, TpChannelFunc func, gpointer data);
  /** Request a channel. May not be NULL. */
  TpChannelFactoryRequestStatus (*request) (TpChannelFactoryIface *,
      const gchar *chan_type, TpHandleType handle_type, guint handle,
      gpointer request, TpChannelIface **ret, GError **error);
};

GType tp_channel_factory_iface_get_type (void);

void tp_channel_factory_iface_close_all (TpChannelFactoryIface *);
void tp_channel_factory_iface_connecting (TpChannelFactoryIface *);
void tp_channel_factory_iface_connected (TpChannelFactoryIface *);
void tp_channel_factory_iface_disconnected (TpChannelFactoryIface *);
void tp_channel_factory_iface_foreach (TpChannelFactoryIface *,
    TpChannelFunc func, gpointer data);
TpChannelFactoryRequestStatus tp_channel_factory_iface_request (
    TpChannelFactoryIface *, const gchar *chan_type,
    TpHandleType handle_type, guint handle, gpointer request,
    TpChannelIface **ret, GError **error);

/** Signal that a new channel has been created (new-channel signal).
 * If the channel was created in response to a channel request, the request
 * was for a nonzero handle type, and the channel has zero handle type,
 * request will be the request context passed to 
 * tp_channel_factory_iface_request(). Otherwise, request may either be
 * NULL or a request that led to the channel's creation; callers are expected
 * to determine which channels satisfy which requests based on the handle
 * and handle-type.
 */

void tp_channel_factory_iface_emit_new_channel (gpointer instance,
    TpChannelIface *channel, gpointer request);

/** Signal that a new channel was created, but an error occurred before it
 * could become useful.
 *
 * request is as for tp_channel_factory_iface_emit_new_channel.
 */

void tp_channel_factory_iface_emit_channel_error (gpointer instance,
    TpChannelIface *channel, GError *error, gpointer request);

G_END_DECLS

#endif /* __TP_CHANNEL_FACTORY_IFACE_H__ */
