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
    S_REQUEST_SUCCEEDED,
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

      /* FIXME: should probably have a better GType for a GPtrArray of
       * ExportableChannel */
      /**
       * GabbleChannelManager::new-channels:
       * @self: the channel manager
       * @channels: a #GPtrArray of #GabbleExportableChannel
       *
       * Emitted when new channels have been created. The Connection should
       * generally emit NewChannels (and NewChannel) in response to this
       * signal.
       */
      signals[S_NEW_CHANNELS] = g_signal_new ("new-channels",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__POINTER,
          G_TYPE_NONE, 1, G_TYPE_POINTER);

      /**
       * GabbleChannelManager::request-succeeded:
       * @self: the channel manager
       * @request_token: opaque pointer supplied by the requester,
       *  representing a request
       * @channel: the channel that satisfied the request
       *
       * Emitted when a channel request has been satisfied by a channel.
       * The Connection should generally respond to this signal by returning
       * success from CreateChannel or RequestChannel.
       */
      signals[S_REQUEST_SUCCEEDED] = g_signal_new ("request-succeeded",
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
       * CreateChannel or RequestChannel.
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
 * @channels: A #GPtrArray of #GabbleExportableChannel, which may be empty
 *
 * If @channels is non-empty, emit the #GabbleChannelManager::new-channels
 * signal indicating that those channels have been created.
 */
void
gabble_channel_manager_emit_new_channels (gpointer instance,
                                          GPtrArray *channels)
{
  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));

  if (channels->len == 0)
    return;

  /* just a quick sanity-check */
  g_return_if_fail (GABBLE_IS_EXPORTABLE_CHANNEL (g_ptr_array_index (
          channels, 0)));

  g_signal_emit (instance, signals[S_NEW_CHANNELS], 0, channels);
}


/**
 * gabble_channel_manager_emit_new_channel:
 * @instance: An object implementing #GabbleChannelManager
 * @channel: A #GabbleExportableChannel
 *
 * Emit the #GabbleChannelManager::new-channels signal indicating that the
 * channel has been created. (This is a convenient shortcut for calling
 * gabble_channel_manager_emit_new_channels() with an array of length 1.)
 */
void
gabble_channel_manager_emit_new_channel (gpointer instance,
                                         GabbleExportableChannel *channel)
{
  GPtrArray *array = g_ptr_array_sized_new (1);

  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));
  g_return_if_fail (GABBLE_IS_EXPORTABLE_CHANNEL (channel));

  g_ptr_array_add (array, channel);
  g_signal_emit (instance, signals[S_NEW_CHANNELS], 0, array);
  g_ptr_array_free (array, TRUE);
}


/**
 * gabble_channel_manager_emit_channel_closed:
 * @instance: An object implementing #GabbleChannelManager
 * @path: A channel's object-path
 *
 * Emit the #GabbleChannelManager::channel-closed signal indicating that
 * the given channel has been closed. (This is a convenient shortcut for
 * calling gabble_channel_manager_emit_channel_closed() with the
 * #GabbleExportableChannel:object-path property of @channel.)
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
 * gabble_channel_manager_emit_request_succeeded:
 * @instance: An object implementing #GabbleChannelManager
 * @request_token: An opaque pointer representing the request that
 *  succeeded
 * @channel: The channel that satisfies the request
 *
 * Emit the #GabbleChannelManager::request-succeeded signal indicating that
 * @channel satisfies @request_token.
 */
void
gabble_channel_manager_emit_request_succeeded (gpointer instance,
    gpointer request_token,
    GabbleExportableChannel *channel)
{
  g_return_if_fail (GABBLE_IS_EXPORTABLE_CHANNEL (channel));
  g_return_if_fail (GABBLE_IS_CHANNEL_MANAGER (instance));

  g_signal_emit (instance, signals[S_REQUEST_SUCCEEDED], 0, request_token,
      channel);
}


/**
 * gabble_channel_manager_emit_request_failed:
 * @instance: An object implementing #GabbleChannelManager
 * @request_token: An opaque pointer representing the request that
 *  succeeded
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
 * gabble_channel_manager_emit_request_failed:
 * @instance: An object implementing #GabbleChannelManager
 * @request_token: An opaque pointer representing the request that
 *  succeeded
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
