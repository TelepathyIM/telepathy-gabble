/*
 * jingle-transport-google.c - Source for GabbleJingleTransportGoogle
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

#include "jingle-transport-google.h"

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

G_DEFINE_TYPE_WITH_CODE (GabbleJingleTransportGoogle,
    gabble_jingle_transport_google, G_TYPE_OBJECT,
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

struct _GabbleJingleTransportGooglePrivate
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

static void transmit_candidates (GabbleJingleTransportGoogle *transport,
    GList *candidates);

static void
gabble_jingle_transport_google_init (GabbleJingleTransportGoogle *obj)
{
  GabbleJingleTransportGooglePrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE,
         GabbleJingleTransportGooglePrivate);
  obj->priv = priv;

  priv->dispose_has_run = FALSE;
}

static void
gabble_jingle_transport_google_dispose (GObject *object)
{
  GabbleJingleTransportGoogle *trans = GABBLE_JINGLE_TRANSPORT_GOOGLE (object);
  GabbleJingleTransportGooglePrivate *priv = trans->priv;

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

  if (G_OBJECT_CLASS (gabble_jingle_transport_google_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_transport_google_parent_class)->dispose (object);
}

static void
gabble_jingle_transport_google_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleTransportGoogle *trans = GABBLE_JINGLE_TRANSPORT_GOOGLE (object);
  GabbleJingleTransportGooglePrivate *priv = trans->priv;

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
gabble_jingle_transport_google_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleTransportGoogle *trans = GABBLE_JINGLE_TRANSPORT_GOOGLE (object);
  GabbleJingleTransportGooglePrivate *priv = trans->priv;

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
gabble_jingle_transport_google_class_init (GabbleJingleTransportGoogleClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleTransportGooglePrivate));

  object_class->get_property = gabble_jingle_transport_google_get_property;
  object_class->set_property = gabble_jingle_transport_google_set_property;
  object_class->dispose = gabble_jingle_transport_google_dispose;

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
  GabbleJingleTransportGoogle *t = GABBLE_JINGLE_TRANSPORT_GOOGLE (obj);
  GabbleJingleTransportGooglePrivate *priv = t->priv;
  GList *candidates = NULL;
  LmMessageNode *node;

  for (node = transport_node->children; node; node = node->next)
    {
      const gchar *name, *address, *user, *pass, *str;
      guint port, net, gen, component = 1;
      gdouble pref;
      JingleTransportProtocol proto;
      JingleCandidateType ctype;
      JingleCandidate *c;

      if (tp_strdiff (lm_message_node_get_name (node), "candidate"))
          continue;

      name = lm_message_node_get_attribute (node, "name");
      if (name == NULL || tp_strdiff (name, "rtp"))
          break;

      address = lm_message_node_get_attribute (node, "address");
      if (address == NULL)
          break;

      str = lm_message_node_get_attribute (node, "port");
      if (str == NULL)
          break;
      port = atoi (str);

      str = lm_message_node_get_attribute (node, "protocol");
      if (str == NULL)
          break;

      if (!tp_strdiff (str, "udp"))
        {
          proto = JINGLE_TRANSPORT_PROTOCOL_UDP;
        }
      else if (!tp_strdiff (str, "tcp"))
        {
          /* candiates on port 443 must be "ssltcp" */
          if (port == 443)
              break;

          proto = JINGLE_TRANSPORT_PROTOCOL_TCP;
        }
      else if (!tp_strdiff (str, "ssltcp"))
        {
          /* "ssltcp" must use port 443 */
          if (port != 443)
              break;

          /* we really don't care about "ssltcp" otherwise */
          proto = JINGLE_TRANSPORT_PROTOCOL_TCP;
        }
      else
        {
          /* unknown protocol */
          DEBUG ("unknown protocol: %s", str);
          break;
        }

      str = lm_message_node_get_attribute (node, "preference");
      if (str == NULL)
          break;

      pref = g_ascii_strtod (str, NULL);

      str = lm_message_node_get_attribute (node, "type");
      if (str == NULL)
          break;

      if (!tp_strdiff (str, "local"))
        {
          ctype = JINGLE_CANDIDATE_TYPE_LOCAL;
        }
      else if (!tp_strdiff (str, "stun"))
        {
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
          break;
        }

      user = lm_message_node_get_attribute (node, "username");
      if (user == NULL)
          break;

      pass = lm_message_node_get_attribute (node, "password");
      if (pass == NULL)
          break;

      str = lm_message_node_get_attribute (node, "network");
      if (str == NULL)
          break;
      net = atoi (str);

      str = lm_message_node_get_attribute (node, "generation");
      if (str == NULL)
          break;
      gen = atoi (str);

      str = lm_message_node_get_attribute (node, "component");
      if (str != NULL)
          component = atoi (str);

      c = jingle_candidate_new (component, address, port, proto,
          pref, ctype, user, pass, net, gen);

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

  /* append them to the known remote candidates */
  priv->remote_candidates = g_list_concat (priv->remote_candidates, candidates);
}

static void
transmit_candidates (GabbleJingleTransportGoogle *transport, GList *candidates)
{
  GabbleJingleTransportGooglePrivate *priv = transport->priv;
  JingleDialect dialect;
  GList *li;
  LmMessage *msg;
  LmMessageNode *trans_node, *sess_node;
  JingleMediaType media_type;

  msg = gabble_jingle_session_new_message (priv->content->session,
    JINGLE_ACTION_TRANSPORT_INFO, &sess_node);

  g_object_get (priv->content->session, "dialect", &dialect, NULL);
  g_object_get (priv->content, "media-type", &media_type, NULL);

  if (dialect == JINGLE_DIALECT_GTALK3)
    {
      trans_node = sess_node;
    }
  else if (dialect == JINGLE_DIALECT_GTALK4)
    {
      trans_node = lm_message_node_add_child (sess_node, "transport", NULL);
      lm_message_node_set_attribute (trans_node, "xmlns", NS_GOOGLE_TRANSPORT_P2P);
    }
  else
    {
      const gchar *cname, *cns;

      g_object_get (GABBLE_JINGLE_CONTENT (priv->content),
          "name", &cname, "content-ns", &cns, NULL);

      /* we need the <content> ... */
      trans_node = lm_message_node_add_child (sess_node, "content", NULL);
      lm_message_node_set_attribute (trans_node, "xmlns", cns);
      lm_message_node_set_attribute (trans_node, "name", cname);

      /* .. and the <transport> node */
      trans_node = lm_message_node_add_child (trans_node, "transport", NULL);
      lm_message_node_set_attribute (trans_node, "xmlns", priv->transport_ns);
    }

  for (li = candidates; li; li = li->next)
    {
      JingleCandidate *c = (JingleCandidate *) li->data;
      gchar port_str[16], pref_str[16], comp_str[16], *type_str, *proto_str;
      LmMessageNode *cnode;

      if (c->component < 1 || c->component > 2)
        {
          DEBUG ("ignoring unknown compontent %d", c->component);
          continue;
        }

      sprintf (port_str, "%d", c->port);
      sprintf (pref_str, "%lf", c->preference);
      sprintf (comp_str, "%d", c->component);

      switch (c->type) {
        case JINGLE_CANDIDATE_TYPE_LOCAL:
          type_str = "local";
          break;
        case JINGLE_CANDIDATE_TYPE_STUN:
          type_str = "stun";
          break;
        case JINGLE_CANDIDATE_TYPE_RELAY:
          type_str = "relay";
          break;
        default:
          g_assert_not_reached ();
      }

      switch (c->protocol) {
        case JINGLE_TRANSPORT_PROTOCOL_UDP:
          proto_str = "udp";
          break;
        case JINGLE_TRANSPORT_PROTOCOL_TCP:
          if ((c->port == 443) && (c->type == JINGLE_CANDIDATE_TYPE_RELAY))
            proto_str = "ssltcp";
          else
            proto_str = "tcp";
          break;
        default:
          g_assert_not_reached ();
      }

      cnode = lm_message_node_add_child (trans_node, "candidate", NULL);
      lm_message_node_set_attributes (cnode,
          "address", c->address,
          "port", port_str,
          "username", c->username,
          "password", c->password,
          "preference", pref_str,
          "protocol", proto_str,
          "type", type_str,
          "component", comp_str,
          "network", "0",
          "generation", "0",
          NULL);

      if (media_type == JINGLE_MEDIA_TYPE_VIDEO &&
          JINGLE_IS_GOOGLE_DIALECT (dialect))
        lm_message_node_set_attribute (cnode,
          "name", c->component == 1 ? "video_rtp" : "video_rtcp");
      else
        lm_message_node_set_attribute (cnode,
          "name", c->component == 1 ? "rtp" : "rtcp");

    }

  _gabble_connection_send (priv->content->conn, msg, NULL);
  lm_message_unref (msg);
}

/* Takes in a list of slice-allocated JingleCandidate structs */
static void
add_candidates (GabbleJingleTransportIface *obj, GList *new_candidates)
{
  GabbleJingleTransportGoogle *transport =
    GABBLE_JINGLE_TRANSPORT_GOOGLE (obj);
  GabbleJingleTransportGooglePrivate *priv = transport->priv;
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

static void
retransmit_candidates (GabbleJingleTransportIface *obj, gboolean all)
{
  GabbleJingleTransportGoogle *transport =
    GABBLE_JINGLE_TRANSPORT_GOOGLE (obj);
  GabbleJingleTransportGooglePrivate *priv = transport->priv;

  if (all)
    {
      /* for gtalk3, we might have to retransmit everything */
      transmit_candidates (transport, priv->local_candidates);
      priv->pending_candidates = NULL;
    }
  else
    {
      /* in case content was ready after we wanted to transmit
       * them originally, we are called to retranmit them */
      if (priv->pending_candidates != NULL) {
          transmit_candidates (transport, priv->pending_candidates);
          priv->pending_candidates = NULL;
      }
    }
}

static GList *
get_remote_candidates (GabbleJingleTransportIface *iface)
{
  GabbleJingleTransportGoogle *transport =
    GABBLE_JINGLE_TRANSPORT_GOOGLE (iface);
  GabbleJingleTransportGooglePrivate *priv = transport->priv;

  return priv->remote_candidates;
}

static void
transport_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleJingleTransportIfaceClass *klass = (GabbleJingleTransportIfaceClass *) g_iface;

  klass->parse_candidates = parse_candidates;
  klass->add_candidates = add_candidates;
  klass->retransmit_candidates = retransmit_candidates;
  klass->get_remote_candidates = get_remote_candidates;
}

void
jingle_transport_google_register (GabbleJingleFactory *factory)
{
  /* GTalk libjingle0.3 dialect */
  gabble_jingle_factory_register_transport (factory, "",
      GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE);

  /* GTalk libjingle0.4 dialect */
  gabble_jingle_factory_register_transport (factory,
      NS_GOOGLE_TRANSPORT_P2P,
      GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE);
}

