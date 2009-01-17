/*
 * tube-iface.c - Source for GabbleTube interface
 * Copyright (C) 2007-2008 Collabora Ltd.
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
#include "tube-iface.h"

#include <telepathy-glib/gtypes.h>

#include "connection.h"
#include "util.h"

gboolean
gabble_tube_iface_accept (GabbleTubeIface *self,
                          GError **error)
{
  gboolean (*virtual_method)(GabbleTubeIface *, GError **) =
    GABBLE_TUBE_IFACE_GET_CLASS (self)->accept;
  g_assert (virtual_method != NULL);
  return virtual_method (self, error);
}

void
gabble_tube_iface_close (GabbleTubeIface *self, gboolean closed_remotely)
{
  void (*virtual_method)(GabbleTubeIface *, gboolean) =
    GABBLE_TUBE_IFACE_GET_CLASS (self)->close;
  g_assert (virtual_method != NULL);
  virtual_method (self, closed_remotely);
}

void
gabble_tube_iface_add_bytestream (GabbleTubeIface *self,
                                  GabbleBytestreamIface *bytestream)
{
  void (*virtual_method)(GabbleTubeIface *, GabbleBytestreamIface *) =
    GABBLE_TUBE_IFACE_GET_CLASS (self)->add_bytestream;
  g_assert (virtual_method != NULL);
  virtual_method (self, bytestream);
}

static void
gabble_tube_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      param_spec = g_param_spec_object (
          "connection",
          "GabbleConnection object",
          "Gabble connection object that owns this tube object.",
          GABBLE_TYPE_CONNECTION,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "handle",
          "Handle",
          "The TpHandle associated with the tubes channel that"
          "owns this tube object.",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "handle-type",
          "Handle type",
          "The TpHandleType of the handle associated with the tubes channel"
          "that owns this tube object.",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "self-handle",
          "Self handle",
          "The handle to use for ourself. This can be different from the "
          "connection's self handle if our handle is a room handle.",
          0, G_MAXUINT, 0,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "id",
          "id",
          "The unique identifier of this tube",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "type",
          "Tube type",
          "The TpTubeType this tube object.",
          0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_string (
          "service",
          "service name",
          "the service associated with this tube object.",
          "",
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_boxed (
          "parameters",
          "parameters GHashTable",
          "GHashTable containing parameters of this tube object.",
          TP_HASH_TYPE_STRING_VARIANT_MAP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "state",
          "Tube state",
          "The GabbleTubeChannelState of this tube object",
          0, G_MAXUINT32, TP_TUBE_STATE_REMOTE_PENDING,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      initialized = TRUE;
    }
}

GType
gabble_tube_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabbleTubeIfaceClass),
      gabble_tube_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabbleTubeIface",
        &info, 0);
  }

  return type;
}


void
gabble_tube_iface_publish_in_node (GabbleTubeIface *tube,
                                   TpBaseConnection *conn,
                                   LmMessageNode *node)
{
  LmMessageNode *parameters_node;
  GHashTable *parameters;
  TpTubeType type;
  gchar *service, *id_str;
  guint tube_id;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle initiator_handle;

  g_object_get (G_OBJECT (tube),
      "type", &type,
      "initiator-handle", &initiator_handle,
      "service", &service,
      "parameters", &parameters,
      "id", &tube_id,
      NULL);

  id_str = g_strdup_printf ("%u", tube_id);

  lm_message_node_set_attributes (node,
      "service", service,
      "id", id_str,
      NULL);

  g_free (id_str);

  switch (type)
    {
      case TP_TUBE_TYPE_DBUS:
        {
          gchar *name, *stream_id;

          g_object_get (G_OBJECT (tube),
              "stream-id", &stream_id,
              "dbus-name", &name,
              NULL);

          lm_message_node_set_attributes (node,
              "type", "dbus",
              "stream-id", stream_id,
              "initiator", tp_handle_inspect (contact_repo, initiator_handle),
              NULL);

          if (name != NULL)
            lm_message_node_set_attribute (node, "dbus-name", name);

          g_free (name);
          g_free (stream_id);
        }
        break;
      case TP_TUBE_TYPE_STREAM:
        {
          lm_message_node_set_attribute (node, "type", "stream");
        }
        break;
      default:
        {
          g_return_if_reached ();
        }
    }

  parameters_node = lm_message_node_add_child (node, "parameters",
      NULL);
  lm_message_node_add_children_from_properties (parameters_node, parameters,
      "parameter");

  g_free (service);
  g_hash_table_unref (parameters);
}

