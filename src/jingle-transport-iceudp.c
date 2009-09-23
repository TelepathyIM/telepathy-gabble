/*
 * jingle-transport-iceudp.c - Source for GabbleJingleTransportIceUdp
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

#include "jingle-transport-iceudp.h"

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

G_DEFINE_TYPE_WITH_CODE (GabbleJingleTransportIceUdp,
    gabble_jingle_transport_iceudp, G_TYPE_OBJECT,
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

struct _GabbleJingleTransportIceUdpPrivate
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

  /* next ID to send with a candidate */
  int id_sequence;

  gboolean dispose_has_run;
};

static void
gabble_jingle_transport_iceudp_init (GabbleJingleTransportIceUdp *obj)
{
  GabbleJingleTransportIceUdpPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP,
         GabbleJingleTransportIceUdpPrivate);
  obj->priv = priv;

  priv->id_sequence = 1;
  priv->dispose_has_run = FALSE;
}

static void
gabble_jingle_transport_iceudp_dispose (GObject *object)
{
  GabbleJingleTransportIceUdp *trans = GABBLE_JINGLE_TRANSPORT_ICEUDP (object);
  GabbleJingleTransportIceUdpPrivate *priv = trans->priv;

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

  if (G_OBJECT_CLASS (gabble_jingle_transport_iceudp_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_transport_iceudp_parent_class)->dispose (object);
}

static void
gabble_jingle_transport_iceudp_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleTransportIceUdp *trans = GABBLE_JINGLE_TRANSPORT_ICEUDP (object);
  GabbleJingleTransportIceUdpPrivate *priv = trans->priv;

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
gabble_jingle_transport_iceudp_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleTransportIceUdp *trans = GABBLE_JINGLE_TRANSPORT_ICEUDP (object);
  GabbleJingleTransportIceUdpPrivate *priv = trans->priv;

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
gabble_jingle_transport_iceudp_class_init (GabbleJingleTransportIceUdpClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleTransportIceUdpPrivate));

  object_class->get_property = gabble_jingle_transport_iceudp_get_property;
  object_class->set_property = gabble_jingle_transport_iceudp_set_property;
  object_class->dispose = gabble_jingle_transport_iceudp_dispose;

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

static void
parse_candidates (GabbleJingleTransportIface *obj,
    LmMessageNode *transport_node, GError **error)
{
  GabbleJingleTransportIceUdp *t = GABBLE_JINGLE_TRANSPORT_ICEUDP (obj);
  GabbleJingleTransportIceUdpPrivate *priv = t->priv;
  gboolean node_contains_a_candidate = FALSE;
  GList *candidates = NULL;
  NodeIter i;

  DEBUG ("called");

  for (i = node_iter (transport_node); i; i = node_iter_next (i))
    {
      LmMessageNode *node = node_iter_data (i);
      const gchar *id, *address, *user, *pass, *str;
      guint port, net, gen, component = 1;
      gdouble pref;
      JingleTransportProtocol proto;
      JingleCandidateType ctype;
      JingleCandidate *c;

      if (tp_strdiff (lm_message_node_get_name (node), "candidate"))
        continue;

      node_contains_a_candidate = TRUE;

      id = lm_message_node_get_attribute (node, "foundation");
      if (id == NULL)
        {
          DEBUG ("candidate doesn't contain foundation");
          continue;
        }

      address = lm_message_node_get_attribute (node, "ip");
      if (address == NULL)
        {
          DEBUG ("candidate doesn't contain ip");
          continue;
        }

      str = lm_message_node_get_attribute (node, "port");
      if (str == NULL)
        {
          DEBUG ("candidate doesn't contain port");
          continue;
        }
      port = atoi (str);

      str = lm_message_node_get_attribute (node, "protocol");
      if (str == NULL)
        {
          DEBUG ("candidate doesn't contain protocol");
          continue;
        }

      if (!tp_strdiff (str, "udp"))
        {
          proto = JINGLE_TRANSPORT_PROTOCOL_UDP;
        }
      else
        {
          /* unknown protocol */
          DEBUG ("unknown protocol: %s", str);
          continue;
        }

      str = lm_message_node_get_attribute (node, "priority");
      if (str == NULL)
        {
          DEBUG ("candidate doesn't contain priority");
          continue;
        }
      pref = g_ascii_strtod (str, NULL) / 65536.0;

      str = lm_message_node_get_attribute (node, "type");
      if (str == NULL)
        {
          DEBUG ("candidate doesn't contain type");
          continue;
        }

      if (!tp_strdiff (str, "host"))
        {
          ctype = JINGLE_CANDIDATE_TYPE_LOCAL;
        }
      else if (!tp_strdiff (str, "srflx") || !tp_strdiff (str, "prflx"))
        {
          /* FIXME Strictly speaking a prflx candidate should be a different
           * type, but the TP spec has now way to distinguish and it doesn't
           * matter much anyway.. */
          ctype = JINGLE_CANDIDATE_TYPE_STUN;
        }
      else if (!tp_strdiff (str, "relay"))
        {
          ctype = JINGLE_CANDIDATE_TYPE_RELAY;
        }
      else
        {
          /* unknown candidate type */
          DEBUG ("unknown candidate type: %s", str);
          continue;
        }

      user = lm_message_node_get_attribute (transport_node, "ufrag");
      if (user == NULL)
        {
          DEBUG ("transport doesn't contain ufrag");
          continue;
        }

      pass = lm_message_node_get_attribute (transport_node, "pwd");
      if (pass == NULL)
        {
          DEBUG ("transport doesn't contain pwd");
          continue;
        }

      str = lm_message_node_get_attribute (node, "network");
      if (str == NULL)
        {
          DEBUG ("candidate doesn't contain network");
          continue;
        }
      net = atoi (str);

      str = lm_message_node_get_attribute (node, "generation");
      if (str == NULL)
        {
          DEBUG ("candidate doesn't contain generation");
          continue;
        }
      gen = atoi (str);

      str = lm_message_node_get_attribute (node, "component");
      if (str == NULL)
        {
          DEBUG ("candidate doesn't contain component");
          continue;
        }
      component = atoi (str);

      c = jingle_candidate_new (proto, ctype, id, component,
          address, port, gen, pref, user, pass, net);

      candidates = g_list_append (candidates, c);
    }

  if (candidates == NULL)
    {
      if (node_contains_a_candidate)
        {
          NODE_DEBUG (transport_node,
              "couldn't parse any of the given candidates");
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
              "could not parse any of the given candidates");
        }
      else
        {
          DEBUG ("no candidates in this stanza");
        }
    }
  else
    {
      DEBUG ("emitting %d new remote candidates", g_list_length (candidates));

      g_signal_emit (obj, signals[NEW_CANDIDATES], 0, candidates);

      priv->remote_candidates = g_list_concat (priv->remote_candidates,
          candidates);
    }
}

static void
inject_candidates (GabbleJingleTransportIface *obj,
    LmMessageNode *transport_node)
{
  GabbleJingleTransportIceUdp *self = GABBLE_JINGLE_TRANSPORT_ICEUDP (obj);
  GabbleJingleTransportIceUdpPrivate *priv = self->priv;
  const gchar *username = NULL;

  for (; priv->pending_candidates != NULL;
      priv->pending_candidates = priv->pending_candidates->next)
    {
      JingleCandidate *c = (JingleCandidate *) priv->pending_candidates->data;
      gchar port_str[16], pref_str[16], comp_str[16], id_str[16],
          *type_str, *proto_str;
      LmMessageNode *cnode;

      if (username == NULL)
        {
          username = c->username;
        }
      else if (tp_strdiff (username, c->username))
        {
          DEBUG ("found a candidate with a different username (%s not %s); "
              "will send in a separate batch", c->username, username);
          break;
        }

      sprintf (pref_str, "%d", (int) (65536.0 * c->preference));
      sprintf (port_str, "%d", c->port);
      sprintf (comp_str, "%d", c->component);
      sprintf (id_str, "%d", priv->id_sequence++);

      switch (c->type) {
        case JINGLE_CANDIDATE_TYPE_LOCAL:
          type_str = "host";
          break;
        case JINGLE_CANDIDATE_TYPE_STUN:
          type_str = "srflx";
          break;
        case JINGLE_CANDIDATE_TYPE_RELAY:
          type_str = "relay";
          break;
        default:
          DEBUG ("skipping candidate with unknown type %u", c->type);
          continue;
      }

      switch (c->protocol) {
        case JINGLE_TRANSPORT_PROTOCOL_UDP:
          proto_str = "udp";
          break;
        case JINGLE_TRANSPORT_PROTOCOL_TCP:
          DEBUG ("ignoring TCP candidate");
          continue;
        default:
          DEBUG ("skipping candidate with unknown protocol %u", c->protocol);
          continue;
      }

      lm_message_node_set_attributes (transport_node,
          "ufrag", c->username,
          "pwd", c->password,
          NULL);

      cnode = lm_message_node_add_child (transport_node, "candidate", NULL);
      lm_message_node_set_attributes (cnode,
          "ip", c->address,
          "port", port_str,
          "priority", pref_str,
          "protocol", proto_str,
          "type", type_str,
          "component", comp_str,
          "foundation", c->id,
          "id", id_str,
          "network", "0",
          "generation", "0",
          NULL);
    }
}

/* We never have to retransmit candidates we've already sent, so we ignore
 * @all.
 */
static void
send_candidates (GabbleJingleTransportIface *iface,
    gboolean all G_GNUC_UNUSED)
{
  GabbleJingleTransportIceUdp *self = GABBLE_JINGLE_TRANSPORT_ICEUDP (iface);
  GabbleJingleTransportIceUdpPrivate *priv = self->priv;

  while (priv->pending_candidates != NULL)
    {
      LmMessageNode *trans_node, *sess_node;
      LmMessage *msg;

      msg = gabble_jingle_session_new_message (priv->content->session,
          JINGLE_ACTION_TRANSPORT_INFO, &sess_node);

      gabble_jingle_content_produce_node (priv->content, sess_node, FALSE,
          TRUE, &trans_node);
      inject_candidates (iface, trans_node);

      _gabble_connection_send_with_reply (priv->content->conn, msg, NULL, NULL,
          NULL, NULL);
      lm_message_unref (msg);
    }

  DEBUG ("sent all pending candidates");
}

/* Takes in a list of slice-allocated JingleCandidate structs */
static void
new_local_candidates (GabbleJingleTransportIface *obj, GList *new_candidates)
{
  GabbleJingleTransportIceUdp *transport =
    GABBLE_JINGLE_TRANSPORT_ICEUDP (obj);
  GabbleJingleTransportIceUdpPrivate *priv = transport->priv;

  priv->local_candidates = g_list_concat (priv->local_candidates,
      new_candidates);

  /* If all previous candidates have been signalled, set the new
   * ones as pending. If there are existing pending candidates,
   * the new ones will just be appended to that list. */
  if (priv->pending_candidates == NULL)
      priv->pending_candidates = new_candidates;
}

static GList *
get_remote_candidates (GabbleJingleTransportIface *iface)
{
  GabbleJingleTransportIceUdp *transport =
    GABBLE_JINGLE_TRANSPORT_ICEUDP (iface);
  GabbleJingleTransportIceUdpPrivate *priv = transport->priv;

  return priv->remote_candidates;
}

static JingleTransportType
get_transport_type (void)
{
  return JINGLE_TRANSPORT_ICE_UDP;
}

static void
transport_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleJingleTransportIfaceClass *klass = (GabbleJingleTransportIfaceClass *) g_iface;

  klass->parse_candidates = parse_candidates;

  klass->new_local_candidates = new_local_candidates;
  klass->inject_candidates = inject_candidates;
  klass->send_candidates = send_candidates;

  klass->get_remote_candidates = get_remote_candidates;
  klass->get_transport_type = get_transport_type;
}

void
jingle_transport_iceudp_register (GabbleJingleFactory *factory)
{
  gabble_jingle_factory_register_transport (factory,
      NS_JINGLE_TRANSPORT_ICEUDP,
      GABBLE_TYPE_JINGLE_TRANSPORT_ICEUDP);
}

