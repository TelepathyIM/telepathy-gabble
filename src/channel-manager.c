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

#include "exportable-channel.h"
#include "gabble-signals-marshal.h"

enum {
    NEW_CHANNELS,
    CHANNEL_CLOSED,
    REQUEST_SATISFIED,
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
      /* New channels have been created */
      signals[NEW_CHANNELS] = g_signal_new ("new-channels",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__POINTER,
          G_TYPE_NONE, 1, G_TYPE_POINTER);

      /* A QUEUED request has been satisfied by a channel */
      signals[REQUEST_SATISFIED] = g_signal_new ("request-satisfied",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          gabble_marshal_VOID__POINTER_OBJECT,
          G_TYPE_NONE, 1, G_TYPE_POINTER, GABBLE_TYPE_EXPORTABLE_CHANNEL);

      /* A channel has been closed */
      signals[CHANNEL_CLOSED] = g_signal_new ("channel-closed",
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
