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

#include "config.h"
#include "jingle-transport-iface.h"

#include <glib.h>

#include "connection.h"
#include "jingle-content.h"
#include "jingle-session.h"

WockyJingleTransportIface *
wocky_jingle_transport_iface_new (GType type,
                                   WockyJingleContent *content,
                                   const gchar *transport_ns)
{
  g_return_val_if_fail (g_type_is_a (type, WOCKY_TYPE_JINGLE_TRANSPORT_IFACE),
      NULL);

  return g_object_new (type,
      "content", content,
      "transport-ns", transport_ns,
      NULL);
}

void
wocky_jingle_transport_iface_parse_candidates (WockyJingleTransportIface *self,
    WockyNode *node, GError **error)
{
  void (*virtual_method)(WockyJingleTransportIface *,
      WockyNode *, GError **) =
    WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->parse_candidates;

  g_assert (virtual_method != NULL);
  return virtual_method (self, node, error);
}

/* Takes in a list of slice-allocated WockyJingleCandidate structs */
void
wocky_jingle_transport_iface_new_local_candidates (WockyJingleTransportIface *self,
    GList *candidates)
{
  void (*virtual_method)(WockyJingleTransportIface *,
      GList *) =
    WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->new_local_candidates;

  g_assert (virtual_method != NULL);
  virtual_method (self, candidates);
}

/* Inserts candidates into the given <transport/> node, or equivalent, of a
 * session-initiate, session-accept, content-add or content-accept action.
 */
void
wocky_jingle_transport_iface_inject_candidates (
    WockyJingleTransportIface *self,
    WockyNode *transport_node)
{
  void (*virtual_method)(WockyJingleTransportIface *, WockyNode *) =
      WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->inject_candidates;

  if (virtual_method != NULL)
    virtual_method (self, transport_node);
}

/* Transmits outstanding or all candidates (if applicable and @all is set). */
void
wocky_jingle_transport_iface_send_candidates (
    WockyJingleTransportIface *self,
    gboolean all)
{
  void (*virtual_method) (WockyJingleTransportIface *, gboolean) =
      WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->send_candidates;

  if (virtual_method != NULL)
    virtual_method (self, all);
}

/* Returns TRUE if and only if @self has enough candidates to inject into a
 * {session,content}-accept, and is connected.
 */
gboolean
wocky_jingle_transport_iface_can_accept (WockyJingleTransportIface *self)
{
  WockyJingleTransportState state;
  gboolean (*m) (WockyJingleTransportIface *) =
      WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->can_accept;

  g_object_get (self, "state", &state, NULL);

  if (state != WOCKY_JINGLE_TRANSPORT_STATE_CONNECTED)
    return FALSE;

  /* Only Raw UDP *needs* candidates in order to accept. */
  if (m != NULL)
    return m (self);
  else
    return TRUE;
}

GList *
wocky_jingle_transport_iface_get_remote_candidates (
    WockyJingleTransportIface *self)
{
  GList * (*virtual_method)(WockyJingleTransportIface *) =
    WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->get_remote_candidates;

  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

GList *
wocky_jingle_transport_iface_get_local_candidates (
    WockyJingleTransportIface *self)
{
  GList * (*virtual_method)(WockyJingleTransportIface *) =
    WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->get_local_candidates;

  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

gboolean
jingle_transport_get_credentials (WockyJingleTransportIface *self,
    gchar **ufrag, gchar **pwd)
{
  WockyJingleTransportIfaceClass *klass =
      WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self);

  if (klass->get_credentials)
    return klass->get_credentials (self, ufrag, pwd);
  else
    return FALSE;
}

WockyJingleTransportType
wocky_jingle_transport_iface_get_transport_type (WockyJingleTransportIface *self)
{
  WockyJingleTransportType (*virtual_method)(void) =
    WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS (self)->get_transport_type;

  g_assert (virtual_method != NULL);
  return virtual_method ();
}

static void
wocky_jingle_transport_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      param_spec = g_param_spec_object (
          "content",
          "WockyJingleContent object",
          "Jingle content that's using this jingle transport object.",
          WOCKY_TYPE_JINGLE_CONTENT,
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
          WOCKY_JINGLE_TRANSPORT_STATE_DISCONNECTED,
          WOCKY_JINGLE_TRANSPORT_STATE_CONNECTED,
          WOCKY_JINGLE_TRANSPORT_STATE_DISCONNECTED,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);

      g_object_interface_install_property (klass, param_spec);

      initialized = TRUE;
    }
}

GType
wocky_jingle_transport_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (WockyJingleTransportIfaceClass),
      wocky_jingle_transport_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "WockyJingleTransportIface",
        &info, 0);
  }

  return type;
}

WockyJingleCandidate *
wocky_jingle_candidate_new (WockyJingleTransportProtocol protocol,
    WockyJingleCandidateType type, const gchar *id, int component,
    const gchar *address, int port, int generation, int preference,
    const gchar *username, const gchar *password, int network)
{
  WockyJingleCandidate *c = g_slice_new0 (WockyJingleCandidate);

  c->protocol = protocol;
  c->type = type;
  c->id = g_strdup (id);
  c->address = g_strdup (address);
  c->component = component;
  c->port = port;
  c->generation = generation;
  c->preference = preference;
  c->username = g_strdup (username);
  c->password = g_strdup (password);
  c->network = network;

  return c;
}

void
wocky_jingle_candidate_free (WockyJingleCandidate *c)
{
    g_free (c->id);
    g_free (c->address);
    g_free (c->username);
    g_free (c->password);

    g_slice_free (WockyJingleCandidate, c);
}

void
jingle_transport_free_candidates (GList *candidates)
{
  while (candidates != NULL)
    {
      WockyJingleCandidate *c = (WockyJingleCandidate *) candidates->data;
      wocky_jingle_candidate_free (c);
      candidates = g_list_remove (candidates, c);
    }
}

