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


static GValueArray *
get_channel_details (GObject *obj)
{
  GValueArray *structure = g_value_array_new (1);
  GHashTable *table;
  GValue *value;
  gchar *object_path;

  g_object_get (obj,
      "object-path", &object_path,
      NULL);

  g_value_array_append (structure, NULL);
  value = g_value_array_get_nth (structure, 0);
  g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
  g_value_take_boxed (value, object_path);
  object_path = NULL;

  g_assert (GABBLE_IS_EXPORTABLE_CHANNEL (obj) || TP_IS_CHANNEL_IFACE (obj));

  if (GABBLE_IS_EXPORTABLE_CHANNEL (obj))
    {
      g_object_get (obj,
          "channel-properties", &table,
          NULL);
    }
  else
    {
     table = g_hash_table_new_full (g_str_hash, g_str_equal,
          NULL, (GDestroyNotify) tp_g_value_slice_free);

      value = tp_g_value_slice_new (G_TYPE_UINT);
      g_object_get_property (obj, "handle", value);
      g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandle", value);

      value = tp_g_value_slice_new (G_TYPE_UINT);
      g_object_get_property (obj, "handle-type", value);
      g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType", value);

      value = tp_g_value_slice_new (G_TYPE_STRING);
      g_object_get_property (obj, "channel-type", value);
      g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);
    }

  g_value_array_append (structure, NULL);
  value = g_value_array_get_nth (structure, 1);
  g_value_init (value, TP_HASH_TYPE_STRING_VARIANT_MAP);
  g_value_take_boxed (value, table);

  return structure;
}


/* Most of this is cut and pasted from telepathy-glib, and should
 * make its way back there once stable */

typedef struct _ChannelRequest ChannelRequest;

typedef enum {
    METHOD_REQUEST_CHANNEL,
    METHOD_CREATE_CHANNEL,
#if 0
    METHOD_ENSURE_CHANNEL,
#endif
    NUM_METHODS
} ChannelRequestMethod;

struct _ChannelRequest
{
  DBusGMethodInvocation *context;
  ChannelRequestMethod method;

  /* relevant if the method is METHOD_REQUEST_CHANNEL */
  gchar *channel_type;
  guint handle_type;
  guint handle;
  /* always TRUE for CREATE */
  gboolean suppress_handler;
};

static ChannelRequest *
channel_request_new (DBusGMethodInvocation *context,
                     ChannelRequestMethod method,
                     const char *channel_type,
                     guint handle_type,
                     guint handle,
                     gboolean suppress_handler)
{
  ChannelRequest *ret;

  g_assert (NULL != context);
  g_assert (NULL != channel_type);
  g_assert (method < NUM_METHODS);

  ret = g_slice_new0 (ChannelRequest);
  ret->context = context;
  ret->method = method;
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
satisfy_request (GabbleConnection *self,
                 ChannelRequest *request,
                 GObject *channel,
                 const gchar *object_path)
{
  DEBUG ("completing queued request %p with success, "
      "channel_type=%s, handle_type=%u, "
      "handle=%u, suppress_handler=%u", request, request->channel_type,
      request->handle_type, request->handle, request->suppress_handler);

  switch (request->method)
    {
    case METHOD_REQUEST_CHANNEL:
      tp_svc_connection_return_from_request_channel (request->context,
          object_path);
      break;

    case METHOD_CREATE_CHANNEL:
        {
          GHashTable *properties;

          g_assert (GABBLE_IS_EXPORTABLE_CHANNEL (channel));
          g_object_get (channel,
              "channel-properties", &properties,
              NULL);
          gabble_svc_connection_interface_requests_return_from_create_channel (
              request->context, object_path, properties);
          g_hash_table_destroy (properties);
        }
        break;

    default:
      g_assert_not_reached ();
    }
  request->context = NULL;

  g_ptr_array_remove (self->channel_requests, request);

  channel_request_free (request);
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
    {
      GPtrArray *array = g_ptr_array_sized_new (1);

      tp_svc_connection_emit_new_channel (self, object_path, channel_type,
          handle_type, handle, suppress_handler);

      g_ptr_array_add (array, get_channel_details (G_OBJECT (chan)));
      gabble_svc_connection_interface_requests_emit_new_channels (self,
          array);
      g_value_array_free (g_ptr_array_index (array, 0));
      g_ptr_array_free (array, TRUE);
    }

  for (i = 0; i < tmp->len; i++)
    satisfy_request (self, g_ptr_array_index (tmp, i), G_OBJECT (chan),
        object_path);

  g_ptr_array_free (tmp, TRUE);

  g_free (object_path);
  g_free (channel_type);
}

static void
channel_closed_cb (GObject *channel,
                   GabbleConnection *self)
{
  gchar *object_path;

  g_object_get (channel,
      "object-path", &object_path,
      NULL);

  gabble_svc_connection_interface_requests_emit_channel_closed (self,
      object_path);

  g_free (object_path);
}

static void
connection_new_channel_cb (TpChannelFactoryIface *factory,
                           GObject *chan,
                           ChannelRequest *channel_request,
                           gpointer data)
{
  satisfy_requests (GABBLE_CONNECTION (data), factory,
      TP_CHANNEL_IFACE (chan), channel_request, TRUE);

  g_signal_connect (chan, "closed", (GCallback) channel_closed_cb, data);
}


static void
fail_channel_request (GabbleConnection *self,
                      ChannelRequest *request,
                      GError *error)
{
  DEBUG ("completing queued request %p with error, channel_type=%s, "
      "handle_type=%u, handle=%u, suppress_handler=%u",
      request, request->channel_type,
      request->handle_type, request->handle, request->suppress_handler);

  dbus_g_method_return_error (request->context, error);
  request->context = NULL;

  g_ptr_array_remove (self->channel_requests, request);

  channel_request_free (request);
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
    fail_channel_request (self, g_ptr_array_index (tmp, i), error);

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
  GHashTable *request_properties;
  GValue *v;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED ((TpBaseConnection *) self,
      context);

  request = channel_request_new (context, METHOD_REQUEST_CHANNEL,
      type, handle_type, handle, suppress_handler);
  g_ptr_array_add (self->channel_requests, request);

  /* First try the channel managers */

  request_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  v = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_string (v, type);
  g_hash_table_insert (request_properties, TP_IFACE_CHANNEL ".ChannelType", v);

  v = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (v, handle_type);
  g_hash_table_insert (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", v);

  v = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (v, handle);
  g_hash_table_insert (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", v);

  for (i = 0; i < self->channel_managers->len; i++)
    {
      GabbleChannelManager *manager = GABBLE_CHANNEL_MANAGER (
          g_ptr_array_index (self->channel_managers, i));

      if (gabble_channel_manager_request_channel (manager, request,
            request_properties))
        return;
    }

  g_hash_table_destroy (request_properties);

  /* OK, none of them wanted it. Now try the channel factories */

  for (i = 0; i < self->channel_factories->len; i++)
    {
      TpChannelFactoryIface *factory = g_ptr_array_index (
        self->channel_factories, i);
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
  /* We deliberately don't iterate over channel managers here -
   * they don't need this, and are expected to listen to status-changed
   * for themselves. */

  /* cancel all queued channel requests when disconnected */
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      /* trigger close_all on all channel factories */
      g_ptr_array_foreach (self->channel_factories,
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
      g_ptr_array_foreach (self->channel_factories,
          (GFunc) tp_channel_factory_iface_connecting, NULL);
      break;

    case TP_CONNECTION_STATUS_CONNECTED:
      self->has_tried_connection = TRUE;
      g_ptr_array_foreach (self->channel_factories,
          (GFunc) tp_channel_factory_iface_connected, NULL);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      if (self->has_tried_connection)
        g_ptr_array_foreach (self->channel_factories,
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

  g_assert (TP_IS_CHANNEL_IFACE (channel));

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
exportable_channel_get_old_info (GabbleExportableChannel *channel,
                                 gchar **object_path_out,
                                 gchar **channel_type_out,
                                 guint *handle_type_out,
                                 guint *handle_out)
{
  gchar *object_path;
  GHashTable *channel_properties;
  gboolean valid;

  g_object_get (channel,
      "object-path", &object_path,
      "channel-properties", &channel_properties,
      NULL);

  g_assert (object_path != NULL);
  g_assert (tp_dbus_check_valid_object_path (object_path, NULL));

  if (object_path_out != NULL)
    *object_path_out = object_path;
  else
    g_free (object_path);

  if (channel_type_out != NULL)
    {
      *channel_type_out = g_strdup (tp_asv_get_string (channel_properties,
          TP_IFACE_CHANNEL ".ChannelType"));
      g_assert (*channel_type_out != NULL);
      g_assert (tp_dbus_check_valid_interface_name (*channel_type_out, NULL));
    }

  if (handle_type_out != NULL)
    {
      *handle_type_out = tp_asv_get_uint32 (channel_properties,
          TP_IFACE_CHANNEL ".TargetHandleType", &valid);
      g_assert (valid);
    }

  if (handle_out != NULL)
    {
      *handle_out = tp_asv_get_uint32 (channel_properties,
          TP_IFACE_CHANNEL ".TargetHandle", &valid);
      g_assert (valid);

      if (handle_type_out != NULL)
        {
          if (*handle_type_out == TP_HANDLE_TYPE_NONE)
            g_assert (*handle_out == 0);
          else
            g_assert (*handle_out != 0);
        }
    }

  g_hash_table_destroy (channel_properties);
}


static void
list_channel_manager_foreach_one (GabbleExportableChannel *channel,
                                  gpointer data)
{
  GPtrArray *values = (GPtrArray *) data;
  gchar *path, *type;
  guint handle_type, handle;
  GValue *entry = tp_dbus_specialized_value_slice_new
      (TP_STRUCT_TYPE_CHANNEL_INFO);

  g_assert (GABBLE_IS_EXPORTABLE_CHANNEL (channel));

  exportable_channel_get_old_info (channel, &path, &type, &handle_type,
      &handle);

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
  values = g_ptr_array_sized_new (self->channel_factories->len * 2
      + self->channel_managers->len * 2);

  for (i = 0; i < self->channel_factories->len; i++)
    {
      TpChannelFactoryIface *factory = g_ptr_array_index
        (self->channel_factories, i);

      tp_channel_factory_iface_foreach (factory,
          list_channel_factory_foreach_one, values);
    }

  for (i = 0; i < self->channel_managers->len; i++)
    {
      GabbleChannelManager *manager = g_ptr_array_index
        (self->channel_managers, i);

      gabble_channel_manager_foreach_channel (manager,
          list_channel_manager_foreach_one, values);
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


/* The new Requests API */


static void
factory_get_channel_details_foreach (TpChannelIface *chan,
                                     gpointer data)
{
  GPtrArray *details = data;

  g_ptr_array_add (details, get_channel_details (G_OBJECT (chan)));
}


static void
manager_get_channel_details_foreach (GabbleExportableChannel *chan,
                                     gpointer data)
{
  GPtrArray *details = data;

  g_ptr_array_add (details, get_channel_details (G_OBJECT (chan)));
}


static GPtrArray *
conn_requests_get_channel_details (GabbleConnection *self)
{
  /* guess that each ChannelManager and each ChannelFactory has two
   * channels, on average */
  GPtrArray *details = g_ptr_array_sized_new (self->channel_managers->len * 2
      + self->channel_factories->len * 2);
  guint i;

  for (i = 0; i < self->channel_factories->len; i++)
    {
      TpChannelFactoryIface *factory = TP_CHANNEL_FACTORY_IFACE (
          g_ptr_array_index (self->channel_factories, i));

      tp_channel_factory_iface_foreach (factory,
          factory_get_channel_details_foreach, details);
    }

  for (i = 0; i < self->channel_managers->len; i++)
    {
      GabbleChannelManager *manager = GABBLE_CHANNEL_MANAGER (
          g_ptr_array_index (self->channel_managers, i));

      gabble_channel_manager_foreach_channel (manager,
          manager_get_channel_details_foreach, details);
    }

  return details;
}


static void
get_requestables_foreach (GabbleChannelManager *manager,
                          GHashTable *fixed_properties,
                          const gchar * const *required_properties,
                          const gchar * const *optional_properties,
                          gpointer user_data)
{
  GPtrArray *details = user_data;
  GValueArray *requestable = g_value_array_new (3);
  GValue *value;
  GPtrArray *allowed;
  guint i = 0;
  const gchar * const *iter;

  allowed = g_ptr_array_new ();

  for (iter = required_properties;
       iter != NULL && *iter != NULL;
       iter++)
    g_ptr_array_add (allowed, g_strdup (*iter));

  for (iter = optional_properties;
       iter != NULL && *iter != NULL;
       iter++)
    g_ptr_array_add (allowed, g_strdup (*iter));

  g_ptr_array_add (allowed, NULL);

  g_value_array_append (requestable, NULL);
  value = g_value_array_get_nth (requestable, 0);
  g_value_init (value, GABBLE_HASH_TYPE_CHANNEL_CLASS);
  g_value_set_boxed (value, fixed_properties);

  g_value_array_append (requestable, NULL);
  value = g_value_array_get_nth (requestable, 1);
  g_value_init (value, G_TYPE_STRV);
  g_value_take_boxed (value, g_ptr_array_free (allowed, FALSE));

  g_ptr_array_add (details, requestable);
}


static GPtrArray *
conn_requests_get_requestables (GabbleConnection *self)
{
  /* generously guess that each ChannelManager has about 2 ChannelClasses */
  GPtrArray *details = g_ptr_array_sized_new (self->channel_managers->len * 2);
  guint i;

  for (i = 0; i < self->channel_managers->len; i++)
    {
      GabbleChannelManager *manager = GABBLE_CHANNEL_MANAGER (
          g_ptr_array_index (self->channel_managers, i));

      gabble_channel_manager_foreach_channel_class (manager,
          get_requestables_foreach, details);
    }

  return details;
}


void
gabble_conn_requests_get_dbus_property (GObject *object,
                                        GQuark interface,
                                        GQuark name,
                                        GValue *value,
                                        gpointer unused G_GNUC_UNUSED)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);

  g_return_if_fail (interface ==
      GABBLE_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS);

  if (name == g_quark_from_static_string ("Channels"))
    {
      g_value_take_boxed (value, conn_requests_get_channel_details (self));
    }
  else if (name == g_quark_from_static_string ("RequestableChannelClasses"))
    {
      g_value_take_boxed (value, conn_requests_get_requestables (self));
    }
  else
    {
      g_return_if_reached ();
    }
}


static void
conn_requests_requestotron (GabbleConnection *self,
                            GHashTable *requested_properties,
                            ChannelRequestMethod method,
                            DBusGMethodInvocation *context)
{
  guint i;
  ChannelRequest *request = NULL;
  GHashTable *altered_properties = NULL;
  const gchar *type;
  GabbleChannelManagerRequestFunc func;
  gboolean suppress_handler;
  TpHandleType target_handle_type;
  TpHandle target_handle;
  gboolean valid;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED ((TpBaseConnection *) self,
      context);

  type = tp_asv_get_string (requested_properties,
        TP_IFACE_CHANNEL ".ChannelType");

  if (type == NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "ChannelType is required" };

      dbus_g_method_return_error (context, &e);
      goto out;
    }

  target_handle_type = tp_asv_get_uint32 (requested_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", &valid);

  /* Allow it to be missing, but not to be otherwise broken */
  if (!valid && tp_asv_lookup (requested_properties,
          TP_IFACE_CHANNEL ".TargetHandleType") != NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "TargetHandleType must be an integer in range 0 to 2**32-1" };

      dbus_g_method_return_error (context, &e);
      goto out;
    }

  target_handle = tp_asv_get_uint32 (requested_properties,
      TP_IFACE_CHANNEL ".TargetHandle", &valid);

  /* Allow it to be missing, but not to be otherwise broken */
  if (!valid && tp_asv_lookup (requested_properties,
          TP_IFACE_CHANNEL ".TargetHandle") != NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "TargetHandle must be an integer in range 0 to 2**32-1" };

      dbus_g_method_return_error (context, &e);
      goto out;
    }

  /* Handle type 0 cannot have a handle */
  if (target_handle_type == TP_HANDLE_TYPE_NONE && target_handle != 0)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "When TargetHandleType is NONE, TargetHandle must be omitted or 0" };

      dbus_g_method_return_error (context, &e);
      goto out;
    }

  /* FIXME: when TargetID is officially supported, if it has
   * target_handle_type == TP_HANDLE_TYPE_NONE and has a TargetID, raise
   * an error */

  /* FIXME: when TargetID is officially supported, if it has both a TargetID
   * and a TargetHandle, raise an error */

  /* FIXME: when InitiatorHandle, InitiatorID and Requested are officially
   * supported, if the request has any of them, raise an error */

  /* FIXME: when TargetID is officially supported, if it has TargetID but
   * no TargetHandle, copy requested_properties to altered_properties,
   * remove TargetID, add TargetHandle, and set
   * requested_properties = altered_properties (shadowing the original).
   * If handle normalization fails, raise an error */

  switch (method)
    {
    case METHOD_CREATE_CHANNEL:
      func = gabble_channel_manager_create_channel;
      suppress_handler = TRUE;
      break;

#if 0
    case METHOD_ENSURE_CHANNEL:
      func = gabble_channel_manager_ensure_channel;
      suppress_handler = FALSE;
      break;
#endif

    default:
      g_assert_not_reached ();
    }

  request = channel_request_new (context, method,
      type, target_handle_type, target_handle, suppress_handler);
  g_ptr_array_add (self->channel_requests, request);

  for (i = 0; i < self->channel_managers->len; i++)
    {
      GabbleChannelManager *manager = GABBLE_CHANNEL_MANAGER (
          g_ptr_array_index (self->channel_managers, i));

      if (func (manager, request, requested_properties))
        goto out;
    }

  /* Nobody accepted the request */
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Not implemented" };

      dbus_g_method_return_error (context, &e);
      g_ptr_array_remove (self->channel_requests, request);
      channel_request_free (request);
    }

out:
  if (altered_properties != NULL)
    g_hash_table_destroy (altered_properties);

  return;
}


static void
conn_requests_create_channel (GabbleSvcConnectionInterfaceRequests *svc,
                              GHashTable *requested_properties,
                              DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (svc);

  return conn_requests_requestotron (self, requested_properties,
      METHOD_CREATE_CHANNEL, context);
}


#if 0
static void
conn_requests_ensure_channel (GabbleSvcConnectionInterfaceRequests *svc,
                              GHashTable *requested_properties,
                              DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (svc);

  return conn_requests_requestotron (self, requested_properties,
      METHOD_ENSURE_CHANNEL, context);
}
#endif


/* Initialization and glue */


static void
manager_new_channel (gpointer key,
                     gpointer value,
                     gpointer data)
{
  GabbleExportableChannel *channel = GABBLE_EXPORTABLE_CHANNEL (key);
  GSList *request_tokens = value;
  GabbleConnection *self = GABBLE_CONNECTION (data);
  gchar *object_path, *channel_type;
  guint handle_type, handle;
  GSList *iter;
  gboolean suppress_handler = FALSE;

  exportable_channel_get_old_info (channel, &object_path, &channel_type,
      &handle_type, &handle);

  for (iter = request_tokens; iter != NULL; iter = iter->next)
    {
      ChannelRequest *request = iter->data;

      if (request->suppress_handler)
        {
          suppress_handler = TRUE;
          break;
        }
    }

  tp_svc_connection_emit_new_channel (self, object_path, channel_type,
      handle_type, handle, suppress_handler);

  for (iter = request_tokens; iter != NULL; iter = iter->next)
    {
      satisfy_request (self, iter->data, G_OBJECT (channel),
          object_path);
    }

  g_free (object_path);
  g_free (channel_type);
}


static void
manager_new_channels_foreach (gpointer key,
                              gpointer value,
                              gpointer data)
{
  GPtrArray *details = data;

  g_ptr_array_add (details, get_channel_details (G_OBJECT (key)));
}


static void
manager_new_channels_cb (GabbleChannelManager *manager,
                         GHashTable *channels,
                         GabbleConnection *self)
{
  GPtrArray *array;

  g_assert (GABBLE_IS_CHANNEL_MANAGER (manager));
  g_assert (GABBLE_IS_CONNECTION (self));

  array = g_ptr_array_sized_new (g_hash_table_size (channels));
  g_hash_table_foreach (channels, manager_new_channels_foreach, array);
  gabble_svc_connection_interface_requests_emit_new_channels (self,
      array);
  g_ptr_array_foreach (array, (GFunc) g_value_array_free, NULL);
  g_ptr_array_free (array, TRUE);

  g_hash_table_foreach (channels, manager_new_channel, self);
}


static void
manager_request_already_satisfied_cb (GabbleChannelManager *manager,
                                      gpointer request_token,
                                      GabbleExportableChannel *channel,
                                      GabbleConnection *self)
{
  gchar *object_path;

  g_assert (GABBLE_IS_CHANNEL_MANAGER (manager));
  g_assert (GABBLE_IS_EXPORTABLE_CHANNEL (channel));
  g_assert (GABBLE_IS_CONNECTION (self));

  g_object_get (channel,
      "object-path", &object_path,
      NULL);

  satisfy_request (self, request_token, G_OBJECT (channel), object_path);
  g_free (object_path);
}


static void
manager_request_failed_cb (GabbleChannelManager *manager,
                           gpointer request_token,
                           guint domain,
                           gint code,
                           gchar *message,
                           GabbleConnection *self)
{
  GError error = { domain, code, message };

  g_assert (GABBLE_IS_CHANNEL_MANAGER (manager));
  g_assert (domain > 0);
  g_assert (message != NULL);
  g_assert (GABBLE_IS_CONNECTION (self));

  fail_channel_request (self, request_token, &error);
}


static void
manager_channel_closed_cb (GabbleChannelManager *manager,
                           const gchar *path,
                           GabbleConnection *self)
{
  g_assert (GABBLE_IS_CHANNEL_MANAGER (manager));
  g_assert (path != NULL);
  g_assert (GABBLE_IS_CONNECTION (self));

  gabble_svc_connection_interface_requests_emit_channel_closed (self, path);
}


void
gabble_conn_requests_init (GabbleConnection *self)
{
  guint i;

  g_signal_connect (self, "status-changed",
      (GCallback) connection_status_changed, NULL);

  self->channel_requests = g_ptr_array_new ();

  g_assert (self->channel_factories != NULL);
  g_assert (self->channel_managers != NULL);

  for (i = 0; i < self->channel_factories->len; i++)
    {
      GObject *factory = g_ptr_array_index (self->channel_factories, i);

      g_assert (TP_IS_CHANNEL_FACTORY_IFACE (factory));

      g_signal_connect (factory, "new-channel",
          (GCallback) connection_new_channel_cb, self);
      g_signal_connect (factory, "channel-error",
          (GCallback) connection_channel_error_cb, self);

    }

  for (i = 0; i < self->channel_managers->len; i++)
    {
      GabbleChannelManager *manager = GABBLE_CHANNEL_MANAGER (
          g_ptr_array_index (self->channel_managers, i));

      g_signal_connect (manager, "new-channels",
          (GCallback) manager_new_channels_cb, self);
      g_signal_connect (manager, "request-already-satisfied",
          (GCallback) manager_request_already_satisfied_cb, self);
      g_signal_connect (manager, "request-failed",
          (GCallback) manager_request_failed_cb, self);
      g_signal_connect (manager, "channel-closed",
          (GCallback) manager_channel_closed_cb, self);
    }
}


void
gabble_conn_requests_dispose (GabbleConnection *self)
{
  if (self->channel_factories != NULL)
    {
      g_ptr_array_foreach (self->channel_factories, (GFunc) g_object_unref,
          NULL);
      g_ptr_array_free (self->channel_factories, TRUE);
      self->channel_factories = NULL;
    }

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
