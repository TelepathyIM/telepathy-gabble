/*
 * channel-manager.c - factory and manager for channels relating to a
 *  particular protocol feature
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
#include "channel-manager.h"

#include <telepathy-glib/dbus.h>

#include "exportable-channel.h"
#include "gabble-signals-marshal.h"

enum {
    S_NEW_CHANNELS,
    S_REQUEST_ALREADY_SATISFIED,
    S_REQUEST_FAILED,
    S_CHANNEL_CLOSED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};


static void
channel_manager_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      initialized = TRUE;

      /* FIXME: should probably have a better GType for @channels */
      /**
       * GabbleChannelManager::new-channels:
       * @self: the channel manager
       * @channels: a #GHashTable where the keys are
       *  #GabbleExportableChannel instances (hashed and compared
       *  by g_direct_hash() and g_direct_equal()) and the values are
       *  linked lists (#GSList) of requests (opaque pointers) satisfied by
       *  these channels
       *
       * Emitted when new channels have been created. The Connection should
       * generally emit NewChannels (and NewChannel) in response to this
       * signal, and then return from pending CreateChannel, EnsureChannel
       * and/or RequestChannel calls if appropriate.
       */
      signals[S_NEW_CHANNELS] = g_signal_new ("new-channels",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__POINTER,
          G_TYPE_NONE, 1, G_TYPE_POINTER);

      /**
       * GabbleChannelManager::request-already-satisfied:
       * @self: the channel manager
       * @request_token: opaque pointer supplied by the requester,
       *  representing a request
       * @channel: the existing #GabbleExportableChannel that satisfies the
       *  request
       *
       * Emitted when a channel request is satisfied by an existing channel.
       * The Connection should generally respond to this signal by returning
       * success from EnsureChannel or RequestChannel.
       */
      signals[S_REQUEST_ALREADY_SATISFIED] = g_signal_new (
          "request-already-satisfied",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          gabble_marshal_VOID__POINTER_OBJECT,
          G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_OBJECT);

      /**
       * GabbleChannelManager::request-failed:
       * @self: the channel manager
       * @request_token: opaque pointer supplied by the requester,
       *  representing a request
       * @domain: the domain of a #GError indicating why the request
       *  failed
       * @code: the error code of a #GError indicating why the request
       *  failed
       * @message: the string part of a #GError indicating why the request
       *  failed
       *
       * Emitted when a channel request has failed. The Connection should
       * generally respond to this signal by returning failure from
       * CreateChannel, EnsureChannel or RequestChannel.
       */
      signals[S_REQUEST_FAILED] = g_signal_new ("request-failed",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          gabble_marshal_VOID__POINTER_UINT_INT_STRING,
          G_TYPE_NONE, 4, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_INT,
          G_TYPE_STRING);

      /**
       * GabbleChannelManager::channel-closed:
       * @self: the channel manager
       * @path: the channel's object-path
       *
       * Emitted when a channel has been closed. The Connection should
       * generally respond to this signal by emitting ChannelClosed.
       */
      signals[S_CHANNEL_CLOSED] = g_signal_new ("channel-closed",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__STRING,
          G_TYPE_NONE, 1, G_TYPE_STRING);

    }
}

GType
gabble_channel_manager_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo info = {
        sizeof (GabbleChannelManagerIface),
        channel_manager_base_init,   /* base_init */
        NULL,   /* base_finalize */
        NULL,   /* class_init */
        NULL,   /* class_finalize */
        NULL,   /* class_data */
        0,
        0,      /* n_preallocs */
        NULL    /* instance_init */
      };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "GabbleChannelManager", &info, 0);
    }

  return type;
}


/* Signal emission wrappers */


/**
 * gabble_channel_manager_emit_new_channels:
 * @instance: An object implementing #GabbleChannelManager
 * @channels: a #GHashTable where the keys are
 *  #GabbleExportableChannel instances (hashed and compared
 *  by g_direct_hash() and g_direct_equal()) and the values are
 *  linked lists (#GSList) of requests (opaque pointers) satisfied by
 *  these channels
 *
 * If @channels is non-empty, emit the #GabbleChannelManager::new-channels
 * signal indicating that those channels have been created.
 */
void
gabble_channel_manager_emit_new_channels (gpointer instance,
                                          GHashTable *channels)
{
  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));

  if (g_hash_table_size (channels) == 0)
    return;

  g_signal_emit (instance, signals[S_NEW_CHANNELS], 0, channels);
}


/**
 * gabble_channel_manager_emit_new_channel:
 * @instance: An object implementing #GabbleChannelManager
 * @channel: A #GabbleExportableChannel
 *
 * Emit the #GabbleChannelManager::new-channels signal indicating that the
 * channel has been created. (This is a convenient shortcut for calling
 * gabble_channel_manager_emit_new_channels() with a one-entry hash table.)
 */
void
gabble_channel_manager_emit_new_channel (gpointer instance,
                                         GabbleExportableChannel *channel,
                                         GSList *requests)
{
  GHashTable *channels;

  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));
  g_return_if_fail (GABBLE_IS_EXPORTABLE_CHANNEL (channel));

  channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, NULL);
  g_hash_table_insert (channels, channel, requests);
  g_signal_emit (instance, signals[S_NEW_CHANNELS], 0, channels);
  g_hash_table_destroy (channels);
}


/**
 * gabble_channel_manager_emit_channel_closed:
 * @instance: An object implementing #GabbleChannelManager
 * @path: A channel's object-path
 *
 * Emit the #GabbleChannelManager::channel-closed signal indicating that
 * the channel at the given object path has been closed.
 */
void
gabble_channel_manager_emit_channel_closed (gpointer instance,
                                            const gchar *path)
{
  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));
  g_return_if_fail (tp_dbus_check_valid_object_path (path, NULL));

  g_signal_emit (instance, signals[S_CHANNEL_CLOSED], 0, path);
}


/**
 * gabble_channel_manager_emit_channel_closed_for_object:
 * @instance: An object implementing #GabbleChannelManager
 * @channel: A #GabbleExportableChannel
 *
 * Emit the #GabbleChannelManager::channel-closed signal indicating that
 * the given channel has been closed. (This is a convenient shortcut for
 * calling gabble_channel_manager_emit_channel_closed() with the
 * #GabbleExportableChannel:object-path property of @channel.)
 */
void
gabble_channel_manager_emit_channel_closed_for_object (gpointer instance,
    GabbleExportableChannel *channel)
{
  gchar *path;

  g_return_if_fail (GABBLE_IS_EXPORTABLE_CHANNEL (channel));
  g_object_get (channel,
      "object-path", &path,
      NULL);
  gabble_channel_manager_emit_channel_closed (instance, path);
  g_free (path);
}


/**
 * gabble_channel_manager_emit_request_already_satisfied:
 * @instance: An object implementing #GabbleChannelManager
 * @request_token: An opaque pointer representing the request that
 *  succeeded
 * @channel: The channel that satisfies the request
 *
 * Emit the #GabbleChannelManager::request-already-satisfied signal indicating
 * that the pre-existing channel @channel satisfies @request_token.
 */
void
gabble_channel_manager_emit_request_already_satisfied (gpointer instance,
    gpointer request_token,
    GabbleExportableChannel *channel)
{
  g_return_if_fail (GABBLE_IS_EXPORTABLE_CHANNEL (channel));
  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));

  g_signal_emit (instance, signals[S_REQUEST_ALREADY_SATISFIED], 0,
      request_token, channel);
}


/**
 * gabble_channel_manager_emit_request_failed:
 * @instance: An object implementing #GabbleChannelManager
 * @request_token: An opaque pointer representing the request that failed
 * @domain: a #GError domain
 * @code: a #GError code appropriate for @domain
 * @message: the error message
 *
 * Emit the #GabbleChannelManager::request-failed signal indicating that
 * the request @request_token failed for the given reason.
 */
void
gabble_channel_manager_emit_request_failed (gpointer instance,
                                            gpointer request_token,
                                            GQuark domain,
                                            gint code,
                                            const gchar *message)
{
  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));

  g_signal_emit (instance, signals[S_REQUEST_FAILED], 0, request_token,
      domain, code, message);
}


/**
 * gabble_channel_manager_emit_request_failed_printf:
 * @instance: An object implementing #GabbleChannelManager
 * @request_token: An opaque pointer representing the request that failed
 * @domain: a #GError domain
 * @code: a #GError code appropriate for @domain
 * @format: a printf-style format string for the error message
 * @...: arguments for the format string
 *
 * Emit the #GabbleChannelManager::request-failed signal indicating that
 * the request @request_token failed for the given reason.
 */
void
gabble_channel_manager_emit_request_failed_printf (gpointer instance,
                                                   gpointer request_token,
                                                   GQuark domain,
                                                   gint code,
                                                   const gchar *format,
                                                   ...)
{
  va_list ap;
  gchar *message;

  va_start (ap, format);
  message = g_strdup_vprintf (format, ap);
  va_end (ap);

  gabble_channel_manager_emit_request_failed (instance, request_token,
      domain, code, message);

  g_free (message);
}


/* Virtual-method wrappers */

void
gabble_channel_manager_foreach_channel (GabbleChannelManager *manager,
                                        GabbleExportableChannelFunc func,
                                        gpointer user_data)
{
  GabbleChannelManagerIface *iface = GABBLE_CHANNEL_MANAGER_GET_INTERFACE (
      manager);
  GabbleChannelManagerForeachChannelFunc method = iface->foreach_channel;

  if (method != NULL)
    {
      method (manager, func, user_data);
    }
  /* ... else assume it has no channels, and do nothing */
}


void
gabble_channel_manager_foreach_channel_class (GabbleChannelManager *manager,
    GabbleChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GabbleChannelManagerIface *iface = GABBLE_CHANNEL_MANAGER_GET_INTERFACE (
      manager);
  GabbleChannelManagerForeachChannelClassFunc method =
      iface->foreach_channel_class;

  if (method != NULL)
    {
      method (manager, func, user_data);
    }
  /* ... else assume it has no classes of requestable channel */
}


gboolean
gabble_channel_manager_create_channel (GabbleChannelManager *manager,
                                       gpointer request_token,
                                       GHashTable *request_properties)
{
  GabbleChannelManagerIface *iface = GABBLE_CHANNEL_MANAGER_GET_INTERFACE (
      manager);
  GabbleChannelManagerRequestFunc method = iface->create_channel;

  /* A missing implementation is equivalent to one that always returns FALSE,
   * meaning "can't do that, ask someone else" */
  if (method != NULL)
    return method (manager, request_token, request_properties);
  else
    return FALSE;
}


gboolean
gabble_channel_manager_request_channel (GabbleChannelManager *manager,
                                        gpointer request_token,
                                        GHashTable *request_properties)
{
  GabbleChannelManagerIface *iface = GABBLE_CHANNEL_MANAGER_GET_INTERFACE (
      manager);
  GabbleChannelManagerRequestFunc method = iface->request_channel;

  /* A missing implementation is equivalent to one that always returns FALSE,
   * meaning "can't do that, ask someone else" */
  if (method != NULL)
    return method (manager, request_token, request_properties);
  else
    return FALSE;
}
