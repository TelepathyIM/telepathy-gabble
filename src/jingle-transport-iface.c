/*
 * jingle-transport-iface.c - Source for GabbleJingleTransport interface
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

#include "jingle-transport-iface.h"

#include <glib.h>

#include "connection.h"
#include "jingle-content.h"
#include "jingle-session.h"

void
gabble_jingle_transport_iface_parse_candidates (GabbleJingleTransportIface *self,
    LmMessageNode *node, GError **error)
{
  void (*virtual_method)(GabbleJingleTransportIface *,
      LmMessageNode *, GError **) =
    GABBLE_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->parse_candidates;

  g_assert (virtual_method != NULL);
  return virtual_method (self, node, error);
}

/* Takes in a list of slice-allocated JingleCandidate structs */
void
gabble_jingle_transport_iface_add_candidates (GabbleJingleTransportIface *self,
    GList *candidates)
{
  void (*virtual_method)(GabbleJingleTransportIface *,
      GList *) =
    GABBLE_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->add_candidates;

  g_assert (virtual_method != NULL);
  virtual_method (self, candidates);
}

void
gabble_jingle_transport_iface_retransmit_candidates (GabbleJingleTransportIface *self,
    gboolean all)
{
  void (*virtual_method)(GabbleJingleTransportIface *, gboolean) =
    GABBLE_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->retransmit_candidates;

  g_assert (virtual_method != NULL);
  virtual_method (self, all);
}

GList *
gabble_jingle_transport_iface_get_remote_candidates (
    GabbleJingleTransportIface *self)
{
  GList * (*virtual_method)(GabbleJingleTransportIface *) =
    GABBLE_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->get_remote_candidates;

  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

static void
gabble_jingle_transport_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      param_spec = g_param_spec_object (
          "content",
          "GabbleJingleContent object",
          "Jingle content that's using this jingle transport object.",
          GABBLE_TYPE_JINGLE_CONTENT,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_string (
          "transport-ns",
          "Transport namespace",
          "Namespace identifying the transport type.",
          NULL,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "state",
          "Connection state for the transport.",
          "Enum specifying the connection state of the transport.",
          JINGLE_TRANSPORT_STATE_DISCONNECTED,
          JINGLE_TRANSPORT_STATE_CONNECTED,
          JINGLE_TRANSPORT_STATE_DISCONNECTED,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);

      g_object_interface_install_property (klass, param_spec);

      initialized = TRUE;
    }
}

GType
gabble_jingle_transport_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabbleJingleTransportIfaceClass),
      gabble_jingle_transport_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabbleJingleTransportIface",
        &info, 0);
  }

  return type;
}

