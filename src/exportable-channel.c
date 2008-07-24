/*
 * exportable-channel.c - A channel usable with the Channel Manager
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
#include "exportable-channel.h"

#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/svc-channel.h>


static void
exportable_channel_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      initialized = TRUE;

      /**
       * GabbleExportableChannel:object-path:
       *
       * The D-Bus object path used for this object on the bus. Read-only
       * except during construction.
       */
      param_spec = g_param_spec_string ("object-path", "D-Bus object path",
          "The D-Bus object path used for this object on the bus.", NULL,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
      g_object_interface_install_property (klass, param_spec);

      /**
       * GabbleExportableChannel:channel-properties:
       *
       * The D-Bus properties to be announced in the NewChannels signal
       * and in the Channels property, as a map from
       * inter.face.name.propertyname to GValue.
       *
       * This can only change when the closed signal is emitted.
       */
      param_spec = g_param_spec_boxed ("channel-properties",
          "Channel properties",
          "The channel properties",
          TP_HASH_TYPE_QUALIFIED_PROPERTY_VALUE_MAP,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
      g_object_interface_install_property (klass, param_spec);

      /**
       * GabbleExportableChannel:channel-destroyed:
       *
       * If true, the closed signal on the Channel interface indicates that
       * the channel can go away.
       *
       * If false, the closed signal indicates that the channel should
       * appear to go away and be re-created.
       */
      param_spec = g_param_spec_boolean ("channel-destroyed",
          "Destroyed?",
          "If true, the channel has *really* closed, rather than just "
          "appearing to do so",
          FALSE,
          G_PARAM_READABLE |
          G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
      g_object_interface_install_property (klass, param_spec);
    }
}

GType
gabble_exportable_channel_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo info = {
        sizeof (GabbleExportableChannelIface),
        exportable_channel_base_init,   /* base_init */
        NULL,   /* base_finalize */
        NULL,   /* class_init */
        NULL,   /* class_finalize */
        NULL,   /* class_data */
        0,
        0,      /* n_preallocs */
        NULL    /* instance_init */
      };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "GabbleExportableChannel", &info, 0);

      g_type_interface_add_prerequisite (type, TP_TYPE_SVC_CHANNEL);
    }

  return type;
}
