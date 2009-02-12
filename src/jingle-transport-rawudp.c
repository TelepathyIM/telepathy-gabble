/*
 * jingle-transport-rawudp.c - Source for GabbleJingleTransportRawUdp
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

#include "jingle-transport-rawudp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-content.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "namespaces.h"
#include "util.h"

static void
transport_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleJingleTransportRawUdp,
    gabble_jingle_transport_rawudp, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_JINGLE_TRANSPORT_IFACE,
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

struct _GabbleJingleTransportRawUdpPrivate
{
  GabbleJingleContent *content;
  JingleTransportState state;
  gchar *transport_ns;

  GList *local_candidates;

  /* A pointer into "local_candidates" list to mark the
   * candidates that are still not transmitted, or NULL
   * if all of them are transmitted. */

  GList *pending_candidates;
  GList *remote_candidates;
  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE(o) ((o)->priv)

static void transmit_candidates (GabbleJingleTransportRawUdp *transport,
    GList *candidates);

static void
gabble_jingle_transport_rawudp_init (GabbleJingleTransportRawUdp *obj)
{
  GabbleJingleTransportRawUdpPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP,
         GabbleJingleTransportRawUdpPrivate);
  obj->priv = priv;

  priv->dispose_has_run = FALSE;
}

static void
gabble_jingle_transport_rawudp_dispose (GObject *object)
{
  GabbleJingleTransportRawUdp *trans = GABBLE_JINGLE_TRANSPORT_RAWUDP (object);
  GabbleJingleTransportRawUdpPrivate *priv = GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (trans);

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

  if (G_OBJECT_CLASS (gabble_jingle_transport_rawudp_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_transport_rawudp_parent_class)->dispose (object);
}

static void
gabble_jingle_transport_rawudp_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleTransportRawUdp *trans = GABBLE_JINGLE_TRANSPORT_RAWUDP (object);
  GabbleJingleTransportRawUdpPrivate *priv = GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (trans);

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
gabble_jingle_transport_rawudp_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleTransportRawUdp *trans = GABBLE_JINGLE_TRANSPORT_RAWUDP (object);
  GabbleJingleTransportRawUdpPrivate *priv =
      GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (trans);

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
gabble_jingle_transport_rawudp_class_init (GabbleJingleTransportRawUdpClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleTransportRawUdpPrivate));

  object_class->get_property = gabble_jingle_transport_rawudp_get_property;
  object_class->set_property = gabble_jingle_transport_rawudp_set_property;
  object_class->dispose = gabble_jingle_transport_rawudp_dispose;

  /* property definitions */
  param_spec = g_param_spec_object ("content", "GabbleJingleContent object",
                                    "Jingle content object using this transport.",
                                    GABBLE_TYPE_JINGLE_CONTENT,
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
                                  JINGLE_TRANSPORT_STATE_DISCONNECTED,
                                  JINGLE_TRANSPORT_STATE_CONNECTED,
                                  JINGLE_TRANSPORT_STATE_DISCONNECTED,
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

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

static void
parse_candidates (GabbleJingleTransportIface *obj,
    LmMessageNode *transport_node, GError **error)
{
  GabbleJingleTransportRawUdp *t = GABBLE_JINGLE_TRANSPORT_RAWUDP (obj);
  GabbleJingleTransportRawUdpPrivate *priv =
    GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (t);
  GList *candidates = NULL;
  LmMessageNode *node;

  DEBUG ("called");

  if (priv->remote_candidates != NULL)
    {
      DEBUG ("already have raw udp candidates, ignoring extra ones");
      return;
    }

  for (node = transport_node->children; node; node = node->next)
    {
      const gchar *id, *ip, *str;
      guint port, gen, component = 1;
      JingleCandidate *c;

      if (tp_strdiff (node->name, "candidate"))
          continue;

      str = lm_message_node_get_attribute (node, "component");
      if (str != NULL)
          component = atoi (str);

      if ((component != 1) && (component != 2))
        {
          DEBUG ("Ignoring non-RTP/RTCP component %d", component);
          continue;
        }

      id = lm_message_node_get_attribute (node, "id");
      if (id == NULL)
          break;

      ip = lm_message_node_get_attribute (node, "ip");
      if (ip == NULL)
          break;

      str = lm_message_node_get_attribute (node, "port");
      if (str == NULL)
          break;
      port = atoi (str);

      str = lm_message_node_get_attribute (node, "generation");
      if (str == NULL)
          break;
      gen = atoi (str);

      /* Fixme: candidate might be STUN instead of LOCAL, can we know that? */
      c = jingle_candidate_new (JINGLE_TRANSPORT_PROTOCOL_UDP,
          JINGLE_CANDIDATE_TYPE_LOCAL, id, component, ip, port,
          gen, 1.0, NULL, NULL, 0);

      candidates = g_list_append (candidates, c);
    }

  if (node != NULL)
    {
      DEBUG ("not all nodes were processed, reporting error");
      /* rollback these */
      jingle_transport_free_candidates (candidates);
      SET_BAD_REQ ("invalid candidate");
      return;
    }

  DEBUG ("emitting %d new remote candidates", g_list_length (candidates));
  g_signal_emit (obj, signals[NEW_CANDIDATES], 0, candidates);
  priv->remote_candidates = candidates;
}

/* FIXME: this should be turned into a generic virtual method, and be
 * passed a param with desired jingle action (to know what to produce) */
LmMessageNode *
jingle_transport_rawudp_produce_candidate (GabbleJingleTransportRawUdp *transport,
  LmMessageNode *content_node)
{
  GabbleJingleTransportRawUdpPrivate *priv =
    GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (transport);
  JingleCandidate *c;
  GList *li;
  gchar port_str[16];
  LmMessageNode *cnode;
  LmMessageNode *trans_node =
      lm_message_node_add_child (content_node, "transport", NULL);

  lm_message_node_set_attribute (trans_node, "xmlns", priv->transport_ns);

  /* If we don't have the local candidates yet, we should've waited with
   * the session initiation. */
  g_assert (priv->local_candidates != NULL);

  for (li = priv->local_candidates; li != NULL; li = li->next)
    {
      c = (JingleCandidate *) li->data;
      sprintf (port_str, "%d", c->port);

      cnode = lm_message_node_add_child (trans_node, "candidate", NULL);
      lm_message_node_set_attributes (cnode,
          "ip", c->address,
          "port", port_str,
          "generation", "0",
          "id", c->username,
          "component", c->component,
          NULL);
    }

  return trans_node;
}

static void
transmit_candidates (GabbleJingleTransportRawUdp *transport, GList *candidates)
{
  GabbleJingleTransportRawUdpPrivate *priv =
    GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (transport);
  GList *li;
  LmMessage *msg;
  LmMessageNode *trans_node, *sess_node;
  const gchar *cname, *cns;
  gboolean created_by_initiator = FALSE;

  msg = gabble_jingle_session_new_message (priv->content->session,
    JINGLE_ACTION_TRANSPORT_INFO, &sess_node);

  g_object_get (GABBLE_JINGLE_CONTENT (priv->content),
      "name", &cname, "content-ns", &cns, "created-by-initiator",
      &created_by_initiator, NULL);

  /* we need the <content> ... */
  trans_node = lm_message_node_add_child (sess_node, "content", NULL);
  lm_message_node_set_attribute (trans_node, "xmlns", cns);
  lm_message_node_set_attribute (trans_node, "name", cname);
  /* FIXME: this also needs to be done for google-p2p, and checked in tests */
  /* FIXME:2: content object should produce the content node, we shouldn't
   * have to do it here! */
  lm_message_node_set_attribute (trans_node, "creator",
      created_by_initiator ? "initiator" : "responder");

  /* .. and the <transport> node */
  trans_node = lm_message_node_add_child (trans_node, "transport", NULL);
  lm_message_node_set_attribute (trans_node, "xmlns", priv->transport_ns);

  for (li = candidates; li; li = li->next)
    {
      JingleCandidate *c = (JingleCandidate *) li->data;
      gchar port_str[16];
      LmMessageNode *cnode;

      sprintf (port_str, "%d", c->port);

      /* FIXME: we're missing component attrib, and have hardcoded net/gen */
      cnode = lm_message_node_add_child (trans_node, "candidate", NULL);
      lm_message_node_set_attributes (cnode,
          "ip", c->address,
          "port", port_str,
          "id", c->username,
          "component", "1",
          "network", "0",
          "generation", "0",
          NULL);
    }

  gabble_jingle_session_send (priv->content->session, msg, NULL, NULL);
}

/* Takes in a list of slice-allocated JingleCandidate structs */
static void
new_local_candidates (GabbleJingleTransportIface *obj, GList *new_candidates)
{
  GabbleJingleTransportRawUdp *transport =
    GABBLE_JINGLE_TRANSPORT_RAWUDP (obj);
  GabbleJingleTransportRawUdpPrivate *priv =
    GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (transport);
  JingleContentState state;

  g_object_get (priv->content, "state", &state, NULL);

  if (state > JINGLE_CONTENT_STATE_EMPTY)
    {
      DEBUG ("content already signalled, transmitting candidates");
      transmit_candidates (transport, new_candidates);
      priv->pending_candidates = NULL;
    }
  else
    {
      DEBUG ("content not signalled yet, waiting with candidates");

      /* if we already have pending candidates, the new ones will
       * be in the local_candidates list after them. but these
       * are the first pending ones, we must mark them. */
      if (priv->pending_candidates == NULL)
        priv->pending_candidates = new_candidates;
  }

  priv->local_candidates = g_list_concat (priv->local_candidates,
      new_candidates);
}

static GList *
get_remote_candidates (GabbleJingleTransportIface *iface)
{
  GabbleJingleTransportRawUdp *transport =
    GABBLE_JINGLE_TRANSPORT_RAWUDP (iface);
  GabbleJingleTransportRawUdpPrivate *priv =
    GABBLE_JINGLE_TRANSPORT_RAWUDP_GET_PRIVATE (transport);

  return priv->remote_candidates;
}

static JingleTransportType
get_transport_type (void)
{
  DEBUG ("called");

  return JINGLE_TRANSPORT_RAW_UDP;
}

static void
transport_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleJingleTransportIfaceClass *klass = (GabbleJingleTransportIfaceClass *) g_iface;

  klass->parse_candidates = parse_candidates;
  klass->new_local_candidates = new_local_candidates;
  klass->get_remote_candidates = get_remote_candidates;
  klass->get_transport_type = get_transport_type;
}

void
jingle_transport_rawudp_register (GabbleJingleFactory *factory)
{
  gabble_jingle_factory_register_transport (factory,
      NS_JINGLE_TRANSPORT_RAWUDP,
      GABBLE_TYPE_JINGLE_TRANSPORT_RAWUDP);
}

