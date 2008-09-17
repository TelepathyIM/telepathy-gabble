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

#include "debug.h"
#include "connection.h"
#include "util.h"
#include "namespaces.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "jingle-content.h"

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
  PROP_CONNECTION,
  PROP_SESSION,
  PROP_CONTENT,
  LAST_PROPERTY
};

typedef struct _GabbleJingleTransportGooglePrivate GabbleJingleTransportGooglePrivate;
struct _GabbleJingleTransportGooglePrivate
{
  GabbleConnection *conn;
  GabbleJingleSession *session;
  GabbleJingleContent *content;

  GList *local_candidates;
  // GList *remote_candidates;
  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_PRIVATE(o)\
  ((GabbleJingleTransportGooglePrivate*)((o)->priv))

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
_free_candidates (GList *candidates)
{
  while (candidates != NULL)
    {
      JingleCandidate *c = (JingleCandidate *) candidates->data;

      g_free (c->address);
      g_free (c->username);
      g_free (c->password);

      g_free (c);

      candidates = g_list_remove (candidates, c);
    }
}

static void
gabble_jingle_transport_google_dispose (GObject *object)
{
  GabbleJingleTransportGoogle *trans = GABBLE_JINGLE_TRANSPORT_GOOGLE (object);
  GabbleJingleTransportGooglePrivate *priv = GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_PRIVATE (trans);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  // _free_candidates (priv->remote_candidates); // FIXME: huge bug, malloc/free hell
  // priv->remote_candidates = NULL;

  _free_candidates (priv->local_candidates);
  priv->local_candidates = NULL;

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
  GabbleJingleTransportGooglePrivate *priv = GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_PRIVATE (trans);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_SESSION:
      g_value_set_object (value, priv->session);
      break;
    case PROP_CONTENT:
      g_value_set_object (value, priv->content);
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
  GabbleJingleTransportGooglePrivate *priv =
      GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_PRIVATE (trans);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_SESSION:
      priv->session = g_value_get_object (value);
      break;
    case PROP_CONTENT:
      priv->content = g_value_get_object (value);
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
  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object used for exchanging "
                                    "messages.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("session", "GabbleJingleSession object",
                                    "The session using this transport object.",
                                    GABBLE_TYPE_JINGLE_SESSION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);

  param_spec = g_param_spec_object ("content", "GabbleJingleContent object",
                                    "Jingle content object using this transport.",
                                    GABBLE_TYPE_JINGLE_CONTENT,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT, param_spec);

  /* signal definitions */
  signals[NEW_CANDIDATES] = g_signal_new (
    "new-candidates",
    G_TYPE_FROM_CLASS (cls),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_POINTER);

}

static GabbleJingleTransportIface *
new_transport (GabbleJingleContent *content)
{
  GabbleJingleTransportGoogle *self;
  GabbleJingleSession *sess;
  GabbleConnection *conn;

  g_object_get (content, "connection", &conn,
      "session", &sess, NULL);

  self = g_object_new (GABBLE_TYPE_JINGLE_TRANSPORT_GOOGLE,
    "connection", conn,
    "session", sess,
    "content", content,
    NULL);

  return GABBLE_JINGLE_TRANSPORT_IFACE (self);
}

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

static void
parse_candidates (GabbleJingleTransportIface *obj,
    LmMessageNode *transport_node, GError **error)
{
  GabbleJingleTransportGoogle *t = GABBLE_JINGLE_TRANSPORT_GOOGLE (obj);
  GabbleJingleTransportGooglePrivate *priv =
    GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_PRIVATE (t);
  GList *candidates = NULL;

  LmMessageNode *cnode, *node;

  /* special-case for GTalk libjingle0.3 */
  cnode = lm_message_node_get_child (transport_node, "candidate");

  if (cnode == NULL)
    {
      JingleDialect dialect;

      cnode = transport_node;

      g_object_get (priv->session, "dialect", &dialect, NULL);

      if (dialect == JINGLE_DIALECT_GTALK4)
        {
          /* FIXME: do we need to do anything more than retransmit
           * local candidates and mode switch? */
          g_object_set (priv->session, "dialect",
              JINGLE_DIALECT_GTALK3, NULL);

          transmit_candidates (t, priv->local_candidates);
        }
    }

  for (node = cnode->children; node; node = node->next)
    {
      const gchar *name, *address, *user, *pass, *str;
      guint port, net, gen;
      gdouble pref;
      JingleTransportProtocol proto;
      JingleCandidateType ctype;
      JingleCandidate *c;

      if (tp_strdiff (node->name, "candidate"))
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
          ctype = JINGLE_CANDIDATE_TYPE_DERIVED;
        }
      else if (!tp_strdiff (str, "relay"))
        {
          ctype = JINGLE_CANDIDATE_TYPE_RELAY;
        }
      else
        {
          /* unknown candidate type */
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

      c = g_new0 (JingleCandidate, 1);
      c->address = (gchar *) address;
      c->port = port;
      c->protocol = proto;
      c->preference = pref;
      c->type = ctype;
      c->username = (gchar *) user;
      c->password = (gchar *) pass;
      c->network = net;
      c->generation = gen;

      candidates = g_list_append (candidates, c);
    }

  if (node != NULL)
    {
      /* rollback these */
      while (candidates != NULL)
        {
          JingleCandidate *c = candidates->data;

          g_free (candidates->data);
          candidates = g_list_remove (candidates, c);
        }

      SET_BAD_REQ ("invalid candidate");
      return;
    }

  g_signal_emit_by_name (priv->content, "remote-candidates", candidates);

  /* append them to the known remote candidates */

  /* OK this sucks, do we really need to save it? if we want to save it we
   * can't borrow the strings, malloc hell ensues */
  // priv->remote_candidates = g_list_concat (priv->remote_candidates, candidates);
}

static void
transmit_candidates (GabbleJingleTransportGoogle *transport, GList *candidates)
{
  GabbleJingleTransportGooglePrivate *priv =
    GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_PRIVATE (transport);
  JingleDialect dialect;
  GList *li;
  LmMessage *msg;
  LmMessageNode *trans_node, *sess_node;

  msg = jingle_session_new_message (priv->session,
    JINGLE_ACTION_TRANSPORT_INFO, &sess_node);

  g_object_get (priv->session, "dialect", &dialect, NULL);

  if (dialect == JINGLE_DIALECT_GTALK3)
    {
      trans_node = sess_node;
    }
  else
    {
      trans_node = lm_message_node_add_child (sess_node, "transport",
        NS_GOOGLE_SESSION);
    }

  for (li = candidates; li; li = li->next)
    {
      JingleCandidate *c = (JingleCandidate *) li->data;
      gchar port_str[16], pref_str[16], *type_str, *proto_str;
      LmMessageNode *cnode;

      sprintf (port_str, "%d", c->port);
      sprintf (pref_str, "%d", c->preference);

      switch (c->type) {
        case JINGLE_CANDIDATE_TYPE_LOCAL:
          type_str = "local";
          break;
        case JINGLE_CANDIDATE_TYPE_DERIVED:
          type_str = "derived";
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

          "name", "rtp",
          "network", "0",
          "generation", "0",
          NULL);
    }

  _gabble_connection_send (priv->conn, msg, NULL);
}

static void
add_candidates (GabbleJingleTransportIface *obj, GList *new_candidates)
{
  GabbleJingleTransportGoogle *transport =
    GABBLE_JINGLE_TRANSPORT_GOOGLE (obj);
  GabbleJingleTransportGooglePrivate *priv =
    GABBLE_JINGLE_TRANSPORT_GOOGLE_GET_PRIVATE (transport);

  transmit_candidates (transport, new_candidates);

  priv->local_candidates = g_list_concat (priv->local_candidates,
      new_candidates);
}

static void
transport_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleJingleTransportIfaceClass *klass = (GabbleJingleTransportIfaceClass *) g_iface;

  klass->parse_candidates = parse_candidates;
  klass->add_candidates = add_candidates;
}

void
jingle_transport_google_register (GabbleJingleFactory *factory)
{
  /* GTalk libjingle0.3 dialect */
  gabble_jingle_factory_register_transport (factory, NULL,
      new_transport);

  /* GTalk libjingle0.4 dialect */
  gabble_jingle_factory_register_transport (factory,
      NS_GOOGLE_SESSION, new_transport);
}

