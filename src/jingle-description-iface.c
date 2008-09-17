/*
 * jingle-description-iface.c - Source for GabbleJingleDescription interface
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

#include "jingle-description-iface.h"
#include "connection.h"
#include "jingle-session.h"
#include <glib.h>

void
gabble_jingle_description_iface_produce (GabbleJingleDescriptionIface *self,
    LmMessageNode *node)
{
  void (*virtual_method)(GabbleJingleDescriptionIface *, 
      LmMessageNode *) =
    GABBLE_JINGLE_DESCRIPTION_IFACE_GET_CLASS (self)->produce;

  g_assert (virtual_method != NULL);
  virtual_method (self, node);
}

void
gabble_jingle_description_iface_parse (GabbleJingleDescriptionIface *self,
    LmMessageNode *node, GError **error)
{
  void (*virtual_method)(GabbleJingleDescriptionIface *, 
      LmMessageNode *, GError **) =
    GABBLE_JINGLE_DESCRIPTION_IFACE_GET_CLASS (self)->parse;

  g_assert (virtual_method != NULL);
  virtual_method (self, node, error);
}

static void
gabble_jingle_description_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      param_spec = g_param_spec_object (
          "connection",
          "GabbleConnection object",
          "Gabble connection object that owns this jingle description object.",
          GABBLE_TYPE_CONNECTION,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_object (
          "session",
          "GabbleJingleSession object",
          "Jingle session that's using this jingle description object.",
          GABBLE_TYPE_JINGLE_SESSION,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "handle",
          "Handle",
          "The TpHandle associated with the jingle descriptions channel that"
          "owns this jingle description object.",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      initialized = TRUE;
    }
}

GType
gabble_jingle_description_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabbleJingleDescriptionIfaceClass),
      gabble_jingle_description_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabbleJingleDescriptionIface",
        &info, 0);
  }

  return type;
}
