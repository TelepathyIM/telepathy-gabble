/* GabbleConnection's Requests (requestotron) implementation
 *
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
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
#include "conn-requests.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/util.h>

#include "extensions/extensions.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION
#include "channel-manager.h"
#include "debug.h"
#include "exportable-channel.h"


/* Most of this is cut and pasted from telepathy-glib, and should
 * make its way back there once stable */

typedef struct _ChannelRequest ChannelRequest;

struct _ChannelRequest
{
  DBusGMethodInvocation *context;
  gchar *channel_type;
  guint handle_type;
  guint handle;
  gboolean suppress_handler;
};

static ChannelRequest *
channel_request_new (DBusGMethodInvocation *context,
                     const char *channel_type,
                     guint handle_type,
                     guint handle,
                     gboolean suppress_handler)
{
  ChannelRequest *ret;

  g_assert (NULL != context);
  g_assert (NULL != channel_type);

  ret = g_slice_new0 (ChannelRequest);
  ret->context = context;
  ret->channel_type = g_strdup (channel_type);
  ret->handle_type = handle_type;
  ret->handle = handle;
  ret->suppress_handler = suppress_handler;

  DEBUG("New channel request at %p: ctype=%s htype=%d handle=%d suppress=%d",
        ret, channel_type, handle_type, handle, suppress_handler);

  return ret;
}

static void
channel_request_free (ChannelRequest *request)
{
  g_assert (NULL == request->context);
  DEBUG("Freeing channel request at %p: ctype=%s htype=%d handle=%d "
        "suppress=%d", request, request->channel_type, request->handle_type,
        request->handle, request->suppress_handler);
  g_free (request->channel_type);
  g_slice_free (ChannelRequest, request);
}

static void
channel_request_cancel (gpointer data, gpointer user_data)
{
  ChannelRequest *request = (ChannelRequest *) data;
  GError error = { TP_ERRORS, TP_ERROR_DISCONNECTED,
      "unable to service this channel request, we're disconnecting!" };

  DEBUG ("cancelling request at %p for %s/%u/%u", request,
      request->channel_type, request->handle_type, request->handle);

  dbus_g_method_return_error (request->context, &error);
  request->context = NULL;

  channel_request_free (request);
}

static GPtrArray *
find_matching_channel_requests (GabbleConnection *self,
                                const gchar *channel_type,
                                guint handle_type,
                                guint handle,
                                ChannelRequest *channel_request,
                                gboolean *suppress_handler)
{
  GPtrArray *requests;
  guint i;

  requests = g_ptr_array_sized_new (1);

  if (handle_type == 0)
    {
      /* It's an anonymous channel, which can only satisfy the request for
       * which it was created (or if it's returned as EXISTING, it can only
       * satisfy the request for which it was returned as EXISTING).
       */
      g_assert (handle == 0);
      g_assert (channel_request == NULL ||
          tp_g_ptr_array_contains (self->channel_requests, channel_request));

      if (channel_request)
        {
          g_ptr_array_add (requests, channel_request);

          if (suppress_handler && channel_request->suppress_handler)
            *suppress_handler = TRUE;
        }

      /* whether we've put any matches in requests or not */
      return requests;
    }

  /* for identifiable channels (those which are to a particular handle),
   * satisfy any queued requests.
   */
  for (i = 0; i < self->channel_requests->len; i++)
    {
      ChannelRequest *request = g_ptr_array_index (self->channel_requests, i);

      if (tp_strdiff (request->channel_type, channel_type))
        continue;

      if (handle_type != request->handle_type)
        continue;

      if (handle != request->handle)
        continue;

      if (request->suppress_handler && suppress_handler)
        *suppress_handler = TRUE;

      g_ptr_array_add (requests, request);
    }

  /* if this channel was created or returned as a result of a particular
   * request, that request had better be among the matching ones in the queue
   */
  g_assert (channel_request == NULL ||
      tp_g_ptr_array_contains (requests, channel_request));

  return requests;
}

static void
satisfy_requests (GabbleConnection *self,
                  TpChannelFactoryIface *factory,
                  TpChannelIface *chan,
                  ChannelRequest *channel_request,
                  gboolean is_new)
{
  gchar *object_path = NULL, *channel_type = NULL;
  guint handle_type = 0, handle = 0;
  gboolean suppress_handler = FALSE;
  GPtrArray *tmp;
  guint i;

  g_object_get (chan,
      "object-path", &object_path,
      "channel-type", &channel_type,
      "handle-type", &handle_type,
      "handle", &handle,
      NULL);

  DEBUG ("called for %s", object_path);

  tmp = find_matching_channel_requests (self, channel_type, handle_type,
                                        handle, channel_request,
                                        &suppress_handler);

  if (is_new)
    tp_svc_connection_emit_new_channel (self, object_path, channel_type,
        handle_type, handle, suppress_handler);

  for (i = 0; i < tmp->len; i++)
    {
      ChannelRequest *request = g_ptr_array_index (tmp, i);

      DEBUG ("completing queued request %p with success, "
          "channel_type=%s, handle_type=%u, "
          "handle=%u, suppress_handler=%u", request, request->channel_type,
          request->handle_type, request->handle, request->suppress_handler);

      tp_svc_connection_return_from_request_channel (request->context,
          object_path);
      request->context = NULL;

      g_ptr_array_remove (self->channel_requests, request);

      channel_request_free (request);
    }

  g_ptr_array_free (tmp, TRUE);

  g_free (object_path);
  g_free (channel_type);
}

static void
connection_new_channel_cb (TpChannelFactoryIface *factory,
                           GObject *chan,
                           ChannelRequest *channel_request,
                           gpointer data)
{
  satisfy_requests (GABBLE_CONNECTION (data), factory,
      TP_CHANNEL_IFACE (chan), channel_request, TRUE);
}

static void
connection_channel_error_cb (TpChannelFactoryIface *factory,
                             GObject *chan,
                             GError *error,
                             ChannelRequest *channel_request,
                             gpointer data)
{
  GabbleConnection *self = GABBLE_CONNECTION (data);
  gchar *channel_type = NULL;
  guint handle_type = 0, handle = 0;
  GPtrArray *tmp;
  guint i;

  DEBUG ("channel_type=%s, handle_type=%u, handle=%u, error_code=%u, "
      "error_message=\"%s\"", channel_type, handle_type, handle,
      error->code, error->message);

  g_object_get (chan,
      "channel-type", &channel_type,
      "handle-type", &handle_type,
      "handle", &handle,
      NULL);

  tmp = find_matching_channel_requests (self, channel_type, handle_type,
                                        handle, channel_request, NULL);

  for (i = 0; i < tmp->len; i++)
    {
      ChannelRequest *request = g_ptr_array_index (tmp, i);

      DEBUG ("completing queued request %p with error, channel_type=%s, "
          "handle_type=%u, handle=%u, suppress_handler=%u",
          request, request->channel_type,
          request->handle_type, request->handle, request->suppress_handler);

      dbus_g_method_return_error (request->context, error);
      request->context = NULL;

      g_ptr_array_remove (self->channel_requests, request);

      channel_request_free (request);
    }

  g_ptr_array_free (tmp, TRUE);
  g_free (channel_type);
}


static void
conn_requests_request_channel (TpSvcConnection *iface,
                               const gchar *type,
                               guint handle_type,
                               guint handle,
                               gboolean suppress_handler,
                               DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpChannelFactoryRequestStatus status =
    TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
  GError *error = NULL;
  guint i;
  ChannelRequest *request;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED ((TpBaseConnection *) self,
      context);

  request = channel_request_new (context, type, handle_type, handle,
      suppress_handler);
  g_ptr_array_add (self->channel_requests, request);

  for (i = 0; i < self->channel_managers->len; i++)
    {
      TpChannelFactoryIface *factory = g_ptr_array_index (
        self->channel_managers, i);
      TpChannelFactoryRequestStatus cur_status;
      TpChannelIface *chan = NULL;

      cur_status = tp_channel_factory_iface_request (factory, type,
          (TpHandleType) handle_type, handle, request, &chan, &error);

      switch (cur_status)
        {
        case TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING:
          {
            g_assert (NULL != chan);
            satisfy_requests (self, factory, chan, request, FALSE);
            /* satisfy_requests should remove the request */
            g_assert (!tp_g_ptr_array_contains (self->channel_requests,
                  request));
            return;
          }
        case TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED:
          g_assert (NULL != chan);
          /* the signal handler should have completed the queued request
           * and freed the ChannelRequest already */
          g_assert (!tp_g_ptr_array_contains (self->channel_requests,
                request));
          return;
        case TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED:
          DEBUG ("queued request, channel_type=%s, handle_type=%u, "
              "handle=%u, suppress_handler=%u", type, handle_type,
              handle, suppress_handler);
          return;
        case TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR:
          /* pass through error */
          goto ERROR;
        default:
          /* always return the most specific error */
          if (cur_status > status)
            status = cur_status;
        }
    }

  switch (status)
    {
      case TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE:
        DEBUG ("invalid handle %u", handle);

        error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_HANDLE,
                             "invalid handle %u", handle);

        break;

      case TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE:
        DEBUG ("requested channel is unavailable with "
                 "handle type %u", handle_type);

        error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                             "requested channel is not available with "
                             "handle type %u", handle_type);

        break;

      case TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED:
        DEBUG ("unsupported channel type %s", type);

        error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                             "unsupported channel type %s", type);

        break;

      default:
        g_assert_not_reached ();
    }

ERROR:
  g_assert (error != NULL);
  dbus_g_method_return_error (request->context, error);
  request->context = NULL;
  g_error_free (error);

  g_ptr_array_remove (self->channel_requests, request);
  channel_request_free (request);
}

static void
connection_status_changed (GabbleConnection *self,
                           guint status,
                           guint reason,
                           gpointer unused G_GNUC_UNUSED)
{

  /* cancel all queued channel requests when disconnected */
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      /* trigger close_all on all channel factories */
      g_ptr_array_foreach (self->channel_managers,
          (GFunc) tp_channel_factory_iface_close_all, NULL);

      if (self->channel_requests->len > 0)
        {
          g_ptr_array_foreach (self->channel_requests, (GFunc)
            channel_request_cancel, NULL);
          g_ptr_array_remove_range (self->channel_requests, 0,
            self->channel_requests->len);
        }
    }

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      self->has_tried_connection = TRUE;
      g_ptr_array_foreach (self->channel_managers,
          (GFunc) tp_channel_factory_iface_connecting, NULL);
      break;

    case TP_CONNECTION_STATUS_CONNECTED:
      self->has_tried_connection = TRUE;
      g_ptr_array_foreach (self->channel_managers,
          (GFunc) tp_channel_factory_iface_connected, NULL);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      if (self->has_tried_connection)
        g_ptr_array_foreach (self->channel_managers,
            (GFunc) tp_channel_factory_iface_disconnected, NULL);
      break;

    default:
      g_assert_not_reached ();
    }
}


static void
list_channel_factory_foreach_one (TpChannelIface *chan,
                                  gpointer data)
{
  GObject *channel = G_OBJECT (chan);
  GPtrArray *values = (GPtrArray *) data;
  gchar *path, *type;
  guint handle_type, handle;
  GValue *entry = tp_dbus_specialized_value_slice_new
      (TP_STRUCT_TYPE_CHANNEL_INFO);

  g_object_get (channel,
      "object-path", &path,
      "channel-type", &type,
      "handle-type", &handle_type,
      "handle", &handle,
      NULL);

  dbus_g_type_struct_set (entry,
      0, path,
      1, type,
      2, handle_type,
      3, handle,
      G_MAXUINT);

  g_ptr_array_add (values, entry);

  g_free (path);
  g_free (type);
}

static void
conn_requests_list_channels (TpSvcConnection *iface,
                             DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  GPtrArray *channels, *values;
  guint i;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED ((TpBaseConnection *) self,
      context);

  /* I think on average, each factory will have 2 channels :D */
  values = g_ptr_array_sized_new (self->channel_managers->len * 2);

  for (i = 0; i < self->channel_managers->len; i++)
    {
      TpChannelFactoryIface *factory = g_ptr_array_index
        (self->channel_managers, i);

      tp_channel_factory_iface_foreach (factory,
          list_channel_factory_foreach_one, values);
    }

  channels = g_ptr_array_sized_new (values->len);

  for (i = 0; i < values->len; i++)
    {
      g_ptr_array_add (channels, g_value_get_boxed (g_ptr_array_index
            (values, i)));
    }

  tp_svc_connection_return_from_list_channels (context, channels);

  g_ptr_array_free (channels, TRUE);
  for (i = 0; i < values->len; i++)
    {
      tp_g_value_slice_free (g_ptr_array_index (values, i));
    }
  g_ptr_array_free (values, TRUE);
}


/* The new Requests API (unimplemented) */


static void
conn_requests_create_channel (GabbleSvcConnectionInterfaceRequests *svc,
                              GHashTable *requested_properties,
                              DBusGMethodInvocation *context)
{
  tp_dbus_g_method_return_not_implemented (context);
}


/* Initialization and glue */


void
gabble_conn_requests_init (GabbleConnection *self)
{
  guint i;

  g_signal_connect (self, "status-changed",
      (GCallback) connection_status_changed, NULL);

  self->channel_requests = g_ptr_array_new ();

  g_assert (self->channel_managers != NULL);

  for (i = 0; i < self->channel_managers->len; i++)
    {
      GObject *manager = g_ptr_array_index (self->channel_managers, i);

      g_assert (TP_IS_CHANNEL_FACTORY_IFACE (manager));

      g_signal_connect (manager, "new-channel",
          (GCallback) connection_new_channel_cb, self);
      g_signal_connect (manager, "channel-error",
          (GCallback) connection_channel_error_cb, self);
    }
}


void
gabble_conn_requests_dispose (GabbleConnection *self)
{
  if (self->channel_managers != NULL)
    {
      g_ptr_array_foreach (self->channel_managers, (GFunc) g_object_unref,
          NULL);
      g_ptr_array_free (self->channel_managers, TRUE);
      self->channel_managers = NULL;
    }

  if (self->channel_requests != NULL)
    {
      g_assert (self->channel_requests->len == 0);
      g_ptr_array_free (self->channel_requests, TRUE);
      self->channel_requests = NULL;
    }
}


void
gabble_conn_requests_conn_iface_init (gpointer g_iface,
                                      gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcConnectionClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_implement_##x (klass, \
    conn_requests_##x)
  IMPLEMENT(list_channels);
  IMPLEMENT(request_channel);
#undef IMPLEMENT
}


void
gabble_conn_requests_iface_init (gpointer g_iface,
                                 gpointer iface_data G_GNUC_UNUSED)
{
  GabbleSvcConnectionInterfaceRequestsClass *iface = g_iface;

#define IMPLEMENT(x) \
    gabble_svc_connection_interface_requests_implement_##x (\
        iface, conn_requests_##x)
  IMPLEMENT (create_channel);
#undef IMPLEMENT
}
