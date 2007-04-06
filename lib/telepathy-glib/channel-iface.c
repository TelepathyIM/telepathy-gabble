/*
 * tp-channel-iface.c - Stubs for Telepathy Channel interface
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

/**
 * SECTION:channel-iface
 * @title: TpChannelIface
 * @short_description: interface representing basic channel properties
 * @see_also: #TpSvcChannel, #TpChannelFactoryIface
 *
 * This interface defines a basic set of channel properties. It's mainly
 * used in #TpChannelFactoryIface to represent the returned channel objects.
 */

#include <telepathy-glib/channel-iface.h>

static void
tp_channel_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    GParamSpec *param_spec;

    initialized = TRUE;

    /**
     * TpChannelIface:object-path:
     *
     * The D-Bus object path used for this object on the bus. Read-only
     * except during construction.
     */
    param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                      "The D-Bus object path used for this "
                                      "object on the bus.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    /**
     * TpChannelIface:channel-type:
     *
     * The D-Bus interface representing the type of this channel. Read-only.
     */
    param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                      "The D-Bus interface representing the "
                                      "type of this channel.",
                                      NULL,
                                      G_PARAM_READABLE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    /**
     * TpChannelIface:handle-type:
     *
     * The type (#TpHandleType) of this channel's associated
     * handle, or 0 if there is no associated handle. Read-only except during
     * construction (and depending on the channel type, attempts to set it
     * during construction might also be ignored).
     */

    param_spec = g_param_spec_uint ("handle-type", "Handle type",
                                    "The TpHandleType of this channel's "
                                    "associated handle.",
                                    0, G_MAXUINT32, 0,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    /**
     * TpChannelIface:handle:
     *
     * This channel's associated
     * handle, or 0 if there is no associated handle. Read-only except during
     * construction (and depending on the channel type, attempts to set it
     * during construction might also be ignored).
     */

    param_spec = g_param_spec_uint ("handle", "Handle",
                                    "The TpHandle representing the "
                                    "contact, group, etc. with which "
                                    "this channel communicates, whose "
                                    "type is given by the handle-type "
                                    "property.",
                                    0, G_MAXUINT32, 0,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);
  }
}

GType
tp_channel_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (TpChannelIfaceClass),
      tp_channel_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "TpChannelIface", &info,
        0);
  }

  return type;
}
