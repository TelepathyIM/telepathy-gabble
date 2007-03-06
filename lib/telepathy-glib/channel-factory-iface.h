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

/** TpChannelFactoryIface:
 *
 * A channel factory is attached to a connection. It carries out channel
 * requests from the connection, and responds to channel-related events
 * on the underlying network connection (e.g. incoming calls).
 *
 * The connection has an array of channel factories. In a trivial
 * implementation there might be a single channel factory which handles
 * all requests and all incoming events, but in general, there will be
 * multiple channel factories handling different types of channel.
 *
 * For example, at the time of writing, Gabble has a roster channel factory
 * which handles contact lists and groups, an IM channel factory which
 * handles one-to-one messaging, a MUC channel factory which handles
 * multi-user chat rooms and the index of chat rooms, and a media channel
 * factory which handles VoIP calls.
 */
typedef struct _TpChannelFactoryIface TpChannelFactoryIface;
/** TpChannelFactoryIfaceClass:
 *
 * The class of a TpChannelFactoryIface.
 */
typedef struct _TpChannelFactoryIfaceClass TpChannelFactoryIfaceClass;

/** TpChannelRequestStatus:
 * Indicates the result of a channel request.
 */
typedef enum {
  /** TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED:
   * Same as the Telepathy error NotImplemented. The connection will try
   * the next factory in its list; if all return NotImplemented, that will
   * be the overall result of the request. *ret and *error are not set
   */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED = 0,
  /** TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE:
   * Same as the Telepathy error NotAvailable. *ret and *error are not set */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE,
  /** TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE:
   *  Same as the Telepathy error InvalidHandle. *ret and *error are not set */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE,
  /** TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR:
   *  An error other than the above. *ret is not set, *error is set */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR,
  /** TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED:
   *  A new channel was created (possibly in response to more than one
   * request). new-channel has already been emitted and *ret is set to
   * the new channel. */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED,
  /** TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED:
   *  A new channel will be created, or was created but is not ready yet.
   * Either new-channel or channel-error will be emitted later.
   * *ret and *error are not set. */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED,
  /** TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING:
   *  An existing channel satisfies the request: new-channel was not emitted.
   * *ret is set to the existing channel. */
  TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING
} TpChannelFactoryRequestStatus;

struct _TpChannelFactoryIfaceClass {
  GTypeInterface parent_class;

  /**
   * close_all:
   * @self: An object implementing #TpChannelFactoryIface
   *
   * Close all channels and shut down the channel factory. It is not expected
   * to be usable afterwards. This is called when the connection goes to
   * disconnected state, before emitting the StatusChanged signal or calling
   * disconnected(). May not be NULL.
   */
  void (*close_all) (TpChannelFactoryIface *self);

  /**
   * connecting:
   * @self: An object implementing #TpChannelFactoryIface
   *
   * Called just after the connection goes from disconnected to connecting
   * state. May be NULL if nothing special needs to happen.
   */
  void (*connecting) (TpChannelFactoryIface *self);

  /** connected:
   * @self: An object implementing #TpChannelFactoryIface
   *
   * Called just after the connection goes from connecting to connected
   * state. May be NULL if nothing special needs to happen. */
  void (*connected) (TpChannelFactoryIface *self);

  /** disconnected:
   * @self: An object implementing #TpChannelFactoryIface
   *
   * Called just after the connection goes to disconnected state. This is
   * always called after close_all(). May be NULL if nothing special needs to
   * happen. */
  void (*disconnected) (TpChannelFactoryIface *self);

  /** foreach:
   * @self: An object implementing #TpChannelFactoryIface
   * @func: A function 
   * @data: Arbitrary data to pass to @param func as the second argument
   *
   * Call func(channel, data) for each channel managed by this factory.
   * May not be NULL. */
  void (*foreach) (TpChannelFactoryIface *self, TpChannelFunc func,
      gpointer data);

  /** request:
   * @self: An object implementing #TpChannelFactoryIface
   * @chan_type: The channel type, e.g. %TP_IFACE_CHANNEL_TYPE_TEXT
   * @handle_type: The handle type of the channel's associated handle,
   *               or 0 if the channel has no associated handle
   * @handle: The channel's associated handle, of type @param handle_type,
   *          or 0 if the channel has no associated handle
   * @request: An opaque data structure representing the channel request;
   *           if this request is satisfied by a newly created channel,
   *           this structure MUST be included in the new-channel signal
   *           if the newly created channel has handle 0, and MAY be
   *           included in the signal if the newly created channel has
   *           nonzero handle.
   * @ret: Set to the new channel if it is available immediately, as
   *       documented in the description of #TpChannelFactoryRequestStatus
   * @error: Set to the error if the return is 
   *         %TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR, unset otherwise
   *
   * Request a channel. May not be NULL.
   *
   * Returns: one of the values of #TpChannelFactoryRequestStatus, and
   *          behaves as documented for that return value
   * */
  TpChannelFactoryRequestStatus (*request) (TpChannelFactoryIface *self,
      const gchar *chan_type, TpHandleType handle_type, guint handle,
      gpointer request, TpChannelIface **ret, GError **error);
};

GType tp_channel_factory_iface_get_type (void);

/** tp_channel_factory_iface_close_all:
 * @self: An implementation of the channel factory interface
 *
 * Close all channels and shut down the channel factory. It is not expected
 * to be usable afterwards.
 */
void tp_channel_factory_iface_close_all (TpChannelFactoryIface *self);

/** tp_channel_factory_iface_connecting:
 * @self: An implementation of the channel factory interface
 *
 * Indicate that the connection has gone from disconnected to connecting
 * state.
 */
void tp_channel_factory_iface_connecting (TpChannelFactoryIface *self);

/** tp_channel_factory_iface_connected:
 * @self: An implementation of the channel factory interface
 *
 * Indicate that the connection has gone from connecting to connected state.
 */
void tp_channel_factory_iface_connected (TpChannelFactoryIface *self);

/** tp_channel_factory_iface_disconnected:
 * @self: An implementation of the channel factory interface
 *
 * Indicate that the connection has become disconnected.
 */
void tp_channel_factory_iface_disconnected (TpChannelFactoryIface *self);

/** tp_channel_factory_iface_disconnected:
 * @self: An implementation of the channel factory interface
 * @func: A callback to be called once per channel
 * @data: Extra data to be passed to @param func
 *
 * Call func(channel, data) for each channel managed by this factory.
 */
void tp_channel_factory_iface_foreach (TpChannelFactoryIface *self,
    TpChannelFunc func, gpointer data);

/** tp_channel_factory_iface_request:
 * @self: An object implementing #TpChannelFactoryIface
 * @chan_type: The channel type, e.g. %TP_IFACE_CHANNEL_TYPE_TEXT
 * @handle_type: The handle type of the channel's associated handle,
 *               or 0 if the channel has no associated handle
 * @handle: The channel's associated handle, of type @param handle_type,
 *          or 0 if the channel has no associated handle
 * @request: An opaque data structure representing the channel request;
 *           if this request is satisfied by a newly created channel,
 *           this structure MUST be included in the new-channel signal
 *           if the newly created channel has handle 0, and MAY be
 *           included in the signal if the newly created channel has
 *           nonzero handle.
 * @ret: Set to the new channel if it is available immediately, as
 *       documented in the description of #TpChannelFactoryRequestStatus
 * @error: Set to the error if the return is 
 *         %TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR, unset otherwise
 *
 * Request a channel.
 *
 * Returns: one of the values of #TpChannelFactoryRequestStatus, and
 *          behaves as documented for that return value
 */
TpChannelFactoryRequestStatus tp_channel_factory_iface_request (
    TpChannelFactoryIface *, const gchar *chan_type,
    TpHandleType handle_type, guint handle, gpointer request,
    TpChannelIface **ret, GError **error);

/** tp_channel_factory_iface_emit_new_channel:
 * @instance: An object implementing #TpChannelFactoryIface
 * @channel: The new channel
 * @request: A request context as passed to #tp_channel_factory_iface_request,
 *           or %NULL
 *
 * Signal that a new channel has been created (new-channel signal).
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

/** tp_channel_factory_iface_emit_channel_error:
 * @instance: An object implementing #TpChannelFactoryIface
 * @channel: The new channel
 * @error: The error that made the channel request fail
 * @request: A request context as passed to #tp_channel_factory_iface_request,
 *           or %NULL
 *
 * Signal that a new channel was created, but an error occurred before it
 * could become useful.
 *
 * request is as for #tp_channel_factory_iface_emit_new_channel.
 */
void tp_channel_factory_iface_emit_channel_error (gpointer instance,
    TpChannelIface *channel, GError *error, gpointer request);

G_END_DECLS

#endif /* __TP_CHANNEL_FACTORY_IFACE_H__ */
