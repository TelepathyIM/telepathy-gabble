/*
 * bytestream-iface.c - Source for GabbleBytestream interface
 * Copyright (C) 2007 Collabora Ltd.
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

#include "bytestream-iface.h"

#include <glib.h>

#include "gabble-connection.h"

gboolean
gabble_bytestream_iface_initiate (GabbleBytestreamIface *self)
{
  gboolean (*virtual_method)(GabbleBytestreamIface *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->initiate;
  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

gboolean
gabble_bytestream_iface_send (GabbleBytestreamIface *self,
                              guint len,
                              const gchar *data)
{
  gboolean (*virtual_method)(GabbleBytestreamIface *, guint, const gchar *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->send;
  g_assert (virtual_method != NULL);
  return virtual_method (self, len, data);
}

void
gabble_bytestream_iface_close (GabbleBytestreamIface *self,
                               GError *error)
{
  void (*virtual_method)(GabbleBytestreamIface *, GError *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->close;
  g_assert (virtual_method != NULL);
  virtual_method (self, error);
}

void
gabble_bytestream_iface_accept (GabbleBytestreamIface *self,
                                GabbleBytestreamAugmentSiAcceptReply func,
                                gpointer user_data)
{
  void (*virtual_method)(GabbleBytestreamIface *,
      GabbleBytestreamAugmentSiAcceptReply, gpointer) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->accept;
  g_assert (virtual_method != NULL);
  virtual_method (self, func, user_data);
}

const gchar *
gabble_bytestream_iface_get_protocol (GabbleBytestreamIface *self)
{
  const gchar * (*virtual_method)(GabbleBytestreamIface *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->get_protocol;
  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

static void
gabble_bytestream_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      param_spec = g_param_spec_object (
          "connection",
          "GabbleConnection object",
          "Gabble connection object that owns this Bytestream object.",
          GABBLE_TYPE_CONNECTION,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "peer-handle",
          "Peer handle",
          "The TpHandle of the remote peer involved in this bytestream",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "peer-handle-type",
          "Peer handle type",
          "The TpHandleType of the remote peer's associated handle",
          0, G_MAXUINT32, 0,
          G_PARAM_READABLE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_string (
          "stream-id",
          "stream ID",
          "the ID of the stream",
          "",
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_string (
          "peer-jid",
          "Peer JID",
          "The JID used by the remote peer during the SI",
          "",
          G_PARAM_READABLE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "state",
          "Bytestream state",
          "An enum (GabbleBytestreamState) signifying the current state of"
          "this bytestream object",
          0, NUM_GABBLE_BYTESTREAM_STATES - 1,
          GABBLE_BYTESTREAM_STATE_LOCAL_PENDING,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      initialized = TRUE;
    }
}

GType
gabble_bytestream_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabbleBytestreamIfaceClass),
      gabble_bytestream_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabbleBytestreamIface",
        &info, 0);
  }

  return type;
}
