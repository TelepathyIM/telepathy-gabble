/*
 * jingle-transport-rawudp.c - Source for WockyJingleTransportRawUdp
 *
 * Copyright (C) 2008 Collabora Ltd.
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
#include "jingle-transport-rawudp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-content.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "namespaces.h"

static void
transport_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WockyJingleTransportRawUdp,
    wocky_jingle_transport_rawudp, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WOCKY_TYPE_JINGLE_TRANSPORT_IFACE,
        transport_iface_init));

/* signal enum */
enum
{
  NEW_CANDIDATES,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONTENT = 1,
  PROP_TRANSPORT_NS,
  PROP_STATE,
  LAST_PROPERTY
};

struct _WockyJingleTransportRawUdpPrivate
{
  WockyJingleContent *content;
  WockyJingleTransportState state;
  gchar *transport_ns;

  GList *local_candidates;
  GList *remote_candidates;
  gboolean dispose_has_run;
};

static void
wocky_jingle_transport_rawudp_init (WockyJingleTransportRawUdp *obj)
{
  WockyJingleTransportRawUdpPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP,
         WockyJingleTransportRawUdpPrivate);
  obj->priv = priv;

  priv->dispose_has_run = FALSE;
}

static void
wocky_jingle_transport_rawudp_dispose (GObject *object)
{
  WockyJingleTransportRawUdp *trans = WOCKY_JINGLE_TRANSPORT_RAWUDP (object);
  WockyJingleTransportRawUdpPrivate *priv = trans->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  jingle_transport_free_candidates (priv->remote_candidates);
  priv->remote_candidates = NULL;

  jingle_transport_free_candidates (priv->local_candidates);
  priv->local_candidates = NULL;

  g_free (priv->transport_ns);
  priv->transport_ns = NULL;

  if (G_OBJECT_CLASS (wocky_jingle_transport_rawudp_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_jingle_transport_rawudp_parent_class)->dispose (object);
}

static void
wocky_jingle_transport_rawudp_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
  WockyJingleTransportRawUdp *trans = WOCKY_JINGLE_TRANSPORT_RAWUDP (object);
  WockyJingleTransportRawUdpPrivate *priv = trans->priv;

  switch (property_id) {
    case PROP_CONTENT:
      g_value_set_object (value, priv->content);
      break;
    case PROP_TRANSPORT_NS:
      g_value_set_string (value, priv->transport_ns);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
wocky_jingle_transport_rawudp_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
  WockyJingleTransportRawUdp *trans = WOCKY_JINGLE_TRANSPORT_RAWUDP (object);
  WockyJingleTransportRawUdpPrivate *priv = trans->priv;

  switch (property_id) {
    case PROP_CONTENT:
      priv->content = g_value_get_object (value);
      break;
    case PROP_TRANSPORT_NS:
      g_free (priv->transport_ns);
      priv->transport_ns = g_value_dup_string (value);
      break;
    case PROP_STATE:
      priv->state = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
wocky_jingle_transport_rawudp_class_init (WockyJingleTransportRawUdpClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (WockyJingleTransportRawUdpPrivate));

  object_class->get_property = wocky_jingle_transport_rawudp_get_property;
  object_class->set_property = wocky_jingle_transport_rawudp_set_property;
  object_class->dispose = wocky_jingle_transport_rawudp_dispose;

  /* property definitions */
  param_spec = g_param_spec_object ("content", "WockyJingleContent object",
                                    "Jingle content object using this transport.",
                                    WOCKY_TYPE_JINGLE_CONTENT,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT, param_spec);

  param_spec = g_param_spec_string ("transport-ns", "Transport namespace",
                                    "Namespace identifying the transport type.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TRANSPORT_NS, param_spec);

  param_spec = g_param_spec_uint ("state",
                                  "Connection state for the transport.",
                                  "Enum specifying the connection state of the transport.",
                                  WOCKY_JINGLE_TRANSPORT_STATE_DISCONNECTED,
                                  WOCKY_JINGLE_TRANSPORT_STATE_CONNECTED,
                                  WOCKY_JINGLE_TRANSPORT_STATE_DISCONNECTED,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_NICK |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  /* signal definitions */
  signals[NEW_CANDIDATES] = g_signal_new (
    "new-candidates",
    G_TYPE_FROM_CLASS (cls),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

}

static void
parse_candidates (WockyJingleTransportIface *obj,
    WockyNode *transport_node, GError **error)
{
  WockyJingleTransportRawUdp *t = WOCKY_JINGLE_TRANSPORT_RAWUDP (obj);
  WockyJingleTransportRawUdpPrivate *priv = t->priv;
  GList *candidates = NULL;
  WockyNodeIter i;
  WockyNode *node;

  DEBUG ("called");

  if (priv->remote_candidates != NULL)
    {
      DEBUG ("already have raw udp candidates, ignoring extra ones");
      return;
    }

  wocky_node_iter_init (&i, transport_node, "candidate", NULL);
  while (wocky_node_iter_next (&i, &node))
    {
      const gchar *id, *ip, *str;
      guint port, gen, component = 1;
      WockyJingleCandidate *c;

      str = wocky_node_get_attribute (node, "component");
      if (str != NULL)
          component = atoi (str);

      if ((component != 1) && (component != 2))
        {
          DEBUG ("Ignoring non-RTP/RTCP component %d", component);
          continue;
        }

      id = wocky_node_get_attribute (node, "id");
      if (id == NULL)
          break;

      ip = wocky_node_get_attribute (node, "ip");
      if (ip == NULL)
          break;

      str = wocky_node_get_attribute (node, "port");
      if (str == NULL)
          break;
      port = atoi (str);

      str = wocky_node_get_attribute (node, "generation");
      if (str == NULL)
          break;
      gen = atoi (str);

      c = wocky_jingle_candidate_new (WOCKY_JINGLE_TRANSPORT_PROTOCOL_UDP,
          WOCKY_JINGLE_CANDIDATE_TYPE_LOCAL, id, component, ip, port,
          gen, 1.0, NULL, NULL, 0);

      candidates = g_list_append (candidates, c);
    }

  if (wocky_node_iter_next (&i, NULL))
    {
      DEBUG ("not all nodes were processed, reporting error");
      /* rollback these */
      jingle_transport_free_candidates (candidates);
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "invalid candidate");
      return;
    }

  DEBUG ("emitting %d new remote candidates", g_list_length (candidates));
  g_signal_emit (obj, signals[NEW_CANDIDATES], 0, candidates);
  priv->remote_candidates = candidates;
}

static void
inject_candidates (WockyJingleTransportIface *obj,
    WockyNode *transport_node)
{
  WockyJingleTransportRawUdp *self = WOCKY_JINGLE_TRANSPORT_RAWUDP (obj);
  WockyJingleTransportRawUdpPrivate *priv = self->priv;
  WockyJingleCandidate *c;
  GList *li;
  gchar port_str[16], comp_str[16];
  WockyNode *cnode;

  /* If we don't have the local candidates yet, we should've waited with
   * the session initiation, or can_accept would have returned FALSE.
   */
  g_assert (priv->local_candidates != NULL);

  for (li = priv->local_candidates; li != NULL; li = li->next)
    {
      c = (WockyJingleCandidate *) li->data;
      sprintf (port_str, "%d", c->port);
      sprintf (comp_str, "%d", c->component);

      cnode = wocky_node_add_child (transport_node, "candidate");
      wocky_node_set_attributes (cnode,
          "ip", c->address,
          "port", port_str,
          "generation", "0",
          "id", c->id,
          "component", comp_str,
          NULL);
    }
}

/* Takes in a list of slice-allocated WockyJingleCandidate structs */
static void
new_local_candidates (WockyJingleTransportIface *obj, GList *new_candidates)
{
  WockyJingleTransportRawUdp *transport =
    WOCKY_JINGLE_TRANSPORT_RAWUDP (obj);
  WockyJingleTransportRawUdpPrivate *priv = transport->priv;

  if (priv->local_candidates != NULL)
    {
      DEBUG ("ignoring new local candidates for RAW UDP");
      jingle_transport_free_candidates (new_candidates);
      return;
    }

  priv->local_candidates = new_candidates;
}

static gboolean
can_accept (WockyJingleTransportIface *iface)
{
  WockyJingleTransportRawUdp *self = WOCKY_JINGLE_TRANSPORT_RAWUDP (iface);

  return (self->priv->local_candidates != NULL);
}

static GList *
get_local_candidates (WockyJingleTransportIface *iface)
{
  WockyJingleTransportRawUdp *transport =
    WOCKY_JINGLE_TRANSPORT_RAWUDP (iface);
  WockyJingleTransportRawUdpPrivate *priv = transport->priv;

  return priv->local_candidates;
}

static GList *
get_remote_candidates (WockyJingleTransportIface *iface)
{
  WockyJingleTransportRawUdp *transport =
    WOCKY_JINGLE_TRANSPORT_RAWUDP (iface);
  WockyJingleTransportRawUdpPrivate *priv = transport->priv;

  return priv->remote_candidates;
}

static WockyJingleTransportType
get_transport_type (void)
{
  DEBUG ("called");

  return JINGLE_TRANSPORT_RAW_UDP;
}

static void
transport_iface_init (gpointer g_iface, gpointer iface_data)
{
  WockyJingleTransportIfaceClass *klass = (WockyJingleTransportIfaceClass *) g_iface;

  klass->parse_candidates = parse_candidates;

  klass->new_local_candidates = new_local_candidates;
  klass->inject_candidates = inject_candidates;
  /* Not implementing _send: XEP-0177 says that the candidates live in
   * content-{add,accept}, not in transport-info.
   */
  klass->can_accept = can_accept;

  klass->get_remote_candidates = get_remote_candidates;
  klass->get_local_candidates = get_local_candidates;
  klass->get_transport_type = get_transport_type;
}

void
jingle_transport_rawudp_register (WockyJingleFactory *factory)
{
  wocky_jingle_factory_register_transport (factory,
      NS_JINGLE_TRANSPORT_RAWUDP,
      WOCKY_TYPE_JINGLE_TRANSPORT_RAWUDP);
}

