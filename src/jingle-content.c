/*
 * gabble-jingle-session.c - Source for GabbleJingleContent
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

#include "jingle-content.h"

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
#include "jingle-transport-iface.h"

/* signal enum */
enum
{
  READY,
  NEW_CANDIDATES,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_SESSION,
  PROP_CONTENT_NS,
  PROP_TRANSPORT_NS,
  PROP_NAME,
  PROP_SENDERS,
  PROP_STATE,
  PROP_READY,
  LAST_PROPERTY
};

typedef struct _GabbleJingleContentPrivate GabbleJingleContentPrivate;
struct _GabbleJingleContentPrivate
{
  gchar *name;
  gchar *creator;
  gboolean created_by_initiator;
  gboolean created_by_us;
  JingleContentState state;
  JingleContentSenders senders;
  gboolean ready;

  gchar *content_ns;
  gchar *transport_ns;

  GabbleJingleTransportIface *transport;

  gboolean media_ready;
  gboolean transport_ready;

  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_CONTENT_GET_PRIVATE(o)\
  ((GabbleJingleContentPrivate *) ((o)->priv))

/* lookup tables */

static const gchar *content_senders_table[] = {
  "initiator",
  "responder",
  "both",
  NULL
};

G_DEFINE_TYPE(GabbleJingleContent, gabble_jingle_content, G_TYPE_OBJECT);

static void new_transport_candidates_cb (GabbleJingleTransportIface *trans,
    GList *candidates, GabbleJingleContent *content);

static void
gabble_jingle_content_init (GabbleJingleContent *obj)
{
  GabbleJingleContentPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_CONTENT,
         GabbleJingleContentPrivate);
  obj->priv = priv;

  priv->state = JINGLE_CONTENT_STATE_EMPTY;
  priv->created_by_initiator = TRUE;
  priv->created_by_us = TRUE;
  priv->media_ready = FALSE;
  priv->transport_ready = FALSE;
  priv->dispose_has_run = FALSE;

  obj->conn = NULL;
  obj->session = NULL;
}

static void
gabble_jingle_content_dispose (GObject *object)
{
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (object);
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (content);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  g_free (priv->name);
  priv->name = NULL;

  g_free (priv->creator);
  priv->creator = NULL;

  g_free (priv->content_ns);
  priv->content_ns = NULL;

  g_free (priv->transport_ns);
  priv->transport_ns = NULL;

  if (G_OBJECT_CLASS (gabble_jingle_content_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_content_parent_class)->dispose (object);
}

static void
gabble_jingle_content_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GabbleJingleContent *self = GABBLE_JINGLE_CONTENT (object);
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, self->conn);
      break;
    case PROP_SESSION:
      g_value_set_object (value, self->session);
      break;
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_SENDERS:
      g_value_set_uint (value, priv->senders);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    case PROP_READY:
      g_value_set_boolean (value, priv->ready);
      break;
    case PROP_CONTENT_NS:
      g_value_set_string (value, priv->content_ns);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_content_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GabbleJingleContent *self = GABBLE_JINGLE_CONTENT (object);
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  switch (property_id) {
    case PROP_CONNECTION:
      self->conn = g_value_get_object (value);
      DEBUG ("setting self->conn to %p", self->conn);
      break;
    case PROP_SESSION:
      self->session = g_value_get_object (value);
      break;
    case PROP_CONTENT_NS:
      g_free (priv->content_ns);
      priv->content_ns = g_value_dup_string (value);
      break;
    case PROP_TRANSPORT_NS:
      g_free (priv->transport_ns);
      priv->transport_ns = g_value_dup_string (value);

      /* We can't switch transports. */
      g_assert (priv->transport == NULL);

      if (priv->transport_ns != NULL) {
          GType transport_type = GPOINTER_TO_INT (
              g_hash_table_lookup (self->conn->jingle_factory->transports,
                  priv->transport_ns));

          g_assert (transport_type != 0);

          priv->transport = g_object_new (transport_type,
              "content", self, "transport-ns", priv->transport_ns, NULL);

          g_signal_connect (priv->transport, "new-candidates",
              (GCallback) new_transport_candidates_cb, self);
      }
      break;
    case PROP_NAME:
      /* can't rename */
      g_assert (priv->name == NULL);

      priv->name = g_value_dup_string (value);
      break;
    case PROP_SENDERS:
      priv->senders = g_value_get_uint (value);
      break;
    case PROP_STATE:
      priv->state = g_value_get_uint (value);
      DEBUG ("setting content state to %u", priv->state);
      break;
    case PROP_READY:
      DEBUG ("setting content ready from %u to %u",
          priv->ready, g_value_get_boolean (value));

      if (priv->ready == g_value_get_boolean (value))
        {
          DEBUG ("we're already ready, doing nothing");
          return;
        }

      priv->ready = g_value_get_boolean (value);

      /* if we have pending local candidates, now's the time
       * to transmit them */
      gabble_jingle_transport_iface_retransmit_candidates (priv->transport);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_content_class_init (GabbleJingleContentClass *cls)
{
  GParamSpec *param_spec;
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (GabbleJingleContentPrivate));

  object_class->get_property = gabble_jingle_content_get_property;
  object_class->set_property = gabble_jingle_content_set_property;
  object_class->dispose = gabble_jingle_content_dispose;

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
                                    "Jingle session object that owns this content.",
                                    GABBLE_TYPE_JINGLE_SESSION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);

  param_spec = g_param_spec_string ("name", "Content name",
                                    "A unique content name in the session.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_string ("content-ns", "Content namespace",
                                    "Namespace identifying the content type.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT_NS, param_spec);

  param_spec = g_param_spec_string ("transport-ns", "Transport namespace",
                                    "Namespace identifying the transport type.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TRANSPORT_NS, param_spec);



  param_spec = g_param_spec_uint ("senders", "Stream senders",
                                  "Valid senders for the stream.",
                                  0, G_MAXUINT32, JINGLE_CONTENT_SENDERS_NONE,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SENDERS, param_spec);

  param_spec = g_param_spec_uint ("state", "Content state",
                                  "The current state that the content is in.",
                                  0, G_MAXUINT32, JINGLE_CONTENT_STATE_EMPTY,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_boolean ("ready", "Ready?",
                                     "A boolean signifying whether media for "
                                     "this content is ready to be signalled.",
                                     FALSE,
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_READY, param_spec);

  /* signal definitions */

  signals[READY] = g_signal_new ("ready",
    G_OBJECT_CLASS_TYPE (cls),
    G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE, 0);

  signals[NEW_CANDIDATES] = g_signal_new (
    "new-candidates",
    G_TYPE_FROM_CLASS (cls),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

}

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

static void
new_transport_candidates_cb (GabbleJingleTransportIface *trans,
    GList *candidates, GabbleJingleContent *content)
{
  DEBUG ("JingleContent %p: passing the signal on", content);

  /* just pass the signal on */
  g_signal_emit (content, signals[NEW_CANDIDATES], 0, candidates);
}

static void
parse_description (GabbleJingleContent *c, LmMessageNode *desc_node,
    GError **error)
{
  void (*virtual_method)(GabbleJingleContent *, LmMessageNode *,
      GError **) = GABBLE_JINGLE_CONTENT_GET_CLASS (c)->parse_description;

  g_assert (virtual_method != NULL);
  virtual_method (c, desc_node, error);
}

void
gabble_jingle_content_parse_add (GabbleJingleContent *c,
    LmMessageNode *content_node, gboolean google_mode, GError **error)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (c);
  const gchar *name, *creator, *senders;
  LmMessageNode *trans_node, *desc_node;
  GType transport_type = 0;
  GabbleJingleTransportIface *trans = NULL;
  JingleDialect dialect;

  g_object_get (c->session, "dialect", &dialect, NULL);

  desc_node = lm_message_node_get_child (content_node, "description");
  trans_node = lm_message_node_get_child (content_node, "transport");
  creator = lm_message_node_get_attribute (content_node, "creator");
  name = lm_message_node_get_attribute (content_node, "name");
  senders = lm_message_node_get_attribute (content_node, "senders");

  g_assert (priv->transport_ns == NULL);

  if (google_mode)
    {
      DEBUG ("content in google mode!");
      if (creator == NULL)
          creator = "initiator";

      if (name == NULL)
          name = "gtalk";

      if (senders == NULL)
          senders = "both";

      if (trans_node == NULL)
        {
          /* gtalk lj0.3 assumes google-p2p transport */
          DEBUG ("detecting GTalk3 dialect");

          dialect = JINGLE_DIALECT_GTALK3;
          g_object_set (c->session, "dialect", JINGLE_DIALECT_GTALK3, NULL);
          transport_type = GPOINTER_TO_INT (
              g_hash_table_lookup (c->conn->jingle_factory->transports, ""));
          priv->transport_ns = g_strdup ("");
        }
    }
  else
    {
      /* senders weren't mandatory back then */
      if (dialect == JINGLE_DIALECT_V015) {
        DEBUG ("old gabble detected, settings senders = both");
        senders = "both";
      }

      if ((trans_node == NULL) || (creator == NULL) || (name == NULL) || (senders == NULL))
        {
          SET_BAD_REQ ("missing required content attributes or elements");
          return;
        }
    }

  /* if we didn't set it to google-p2p implicitly already, detect it */
  if (transport_type == 0)
    {
      const gchar *ns = lm_message_node_get_attribute (trans_node, "xmlns");
      DEBUG ("ns is %s", ns);

      transport_type = GPOINTER_TO_INT (
          g_hash_table_lookup (c->conn->jingle_factory->transports, ns));

      if (transport_type == 0)
        {
          SET_BAD_REQ ("unsupported content transport");
          return;
        }

      priv->transport_ns = g_strdup (ns);
    }

  priv->created_by_us = FALSE;
  priv->created_by_initiator = (!tp_strdiff (creator, "initiator"));
  priv->senders = _string_to_enum (content_senders_table, senders);
  if (priv->senders == JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders");
      return;
    }

  parse_description (c, desc_node, error);
  if (*error != NULL)
      return;

  DEBUG ("content creating new transport type %s", g_type_name (transport_type));

  trans = g_object_new (transport_type,
                       "content", c,
                       "transport-ns", priv->transport_ns,
                       NULL);

  g_signal_connect (trans, "new-candidates",
      (GCallback) new_transport_candidates_cb, c);

  /* FIXME: I think candidates can't be specified in content addition/session
   * init, so disabling this:
  gabble_jingle_transport_iface_parse_candidates (trans, trans_node, error);
  if (*error)
    {
      g_object_unref (trans);
      return;
    } */

  g_assert (priv->transport == NULL);
  priv->transport = trans;

  g_assert (priv->name == NULL);
  priv->name = g_strdup (name);

  g_assert (priv->creator == NULL);
  priv->creator = g_strdup (creator);

  priv->state = JINGLE_CONTENT_STATE_NEW;

  return;
}

void
gabble_jingle_content_parse_accept (GabbleJingleContent *c,
    LmMessageNode *content_node, gboolean google_mode, GError **error)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (c);
  const gchar *senders;
  LmMessageNode *trans_node, *desc_node;

  desc_node = lm_message_node_get_child (content_node, "description");
  trans_node = lm_message_node_get_child (content_node, "transport");
  senders = lm_message_node_get_attribute (content_node, "senders");

  if (google_mode)
    {
      DEBUG ("parsing content-accept in google mode");

      if (senders == NULL)
          senders = "both";

      if (trans_node == NULL)
        {
          DEBUG ("no transport node, assuming GTalk3 dialect");
          /* gtalk lj0.3 assumes google-p2p transport */
          g_object_set (c->session, "dialect", JINGLE_DIALECT_GTALK3, NULL);
        }
    }

  DEBUG ("changing senders from %s to %s", _enum_to_string(content_senders_table, priv->senders), senders);
  priv->senders = _string_to_enum (content_senders_table, senders);
  if (priv->senders == JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders");
      return;
    }

  parse_description (c, desc_node, error);
  if (*error != NULL)
      return;

  // FIXME: this overlaps with _update_senders, maybe merge?
  g_object_notify ((GObject *) c, "senders");

  // If all went well, it means the content is finally ackd
  priv->state = JINGLE_CONTENT_STATE_ACKNOWLEDGED;
  g_object_notify ((GObject *) c, "state");
}

void
gabble_jingle_content_produce_node (GabbleJingleContent *c,
  LmMessageNode *parent, gboolean full)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (c);
  LmMessageNode *content_node, *trans_node;
  JingleDialect dialect;
  void (*produce_desc)(GabbleJingleContent *, LmMessageNode *) =
    GABBLE_JINGLE_CONTENT_GET_CLASS (c)->produce_description;

  g_object_get (c->session, "dialect", &dialect, NULL);

  if ((dialect == JINGLE_DIALECT_GTALK3) ||
      (dialect == JINGLE_DIALECT_GTALK4))
    {
      DEBUG ("content node setting to parent??");

      /* content-* isn't used in GTalk anyways, so we always have to include
       * the full content description */
      g_assert (full == TRUE);

      content_node = parent;
    }
  else
    {
      DEBUG ("creator: %s", priv->creator);
      DEBUG ("name: %s", priv->name);
      DEBUG ("senders: %s", _enum_to_string (content_senders_table, priv->senders));

      content_node = lm_message_node_add_child (parent, "content", NULL);
      lm_message_node_set_attributes (content_node,
          "creator", priv->created_by_initiator ? "initiator" : "responder",
          "name", priv->name,
          "senders", _enum_to_string (content_senders_table, priv->senders),
          NULL);
      DEBUG ("created new content node %p", content_node);
    }

  if (!full)
    return;

  produce_desc (c, content_node);

  /* We can do it here, don't need to call into transport object for this */
  trans_node = lm_message_node_add_child (content_node, "transport", NULL);
  lm_message_node_set_attribute (trans_node, "xmlns", priv->transport_ns);
}

void
gabble_jingle_content_update_senders (GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (c);
  JingleContentSenders senders;

  senders = _string_to_enum (content_senders_table,
      lm_message_node_get_attribute (content_node, "senders"));

  if (senders == JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders in stream");
      return;
    }

  priv->senders = senders;
  g_object_notify ((GObject *) c, "senders");
}

void
gabble_jingle_content_parse_transport_info (GabbleJingleContent *self,
  LmMessageNode *trans_node, GError **error)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  gabble_jingle_transport_iface_parse_candidates (priv->transport, trans_node, error);
}

void
gabble_jingle_content_add_candidates (GabbleJingleContent *self, GList *li)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  gabble_jingle_transport_iface_add_candidates (priv->transport, li);
}

/* Returns whether the content is ready to be signalled (initiated, for local
 * streams, or acknowledged, for remote streams. */
gboolean
gabble_jingle_content_is_ready (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  /* If it's created by us, media ready and not signalled,
   * it's ready to be added. */
  if (priv->created_by_us && priv->media_ready &&
      (priv->state == JINGLE_CONTENT_STATE_EMPTY))
          return TRUE;

  /* If it's created by peer, media and transports ready,
   * and not acknowledged yet, it's ready for acceptance. */
  if (!priv->created_by_us && priv->media_ready && priv->transport_ready &&
      (priv->state == JINGLE_CONTENT_STATE_NEW))
          return TRUE;

  return FALSE;
}

static void
try_content_add_or_accept (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);
  LmMessage *msg;
  LmMessageNode *sess_node;
  JingleAction action = JINGLE_ACTION_UNKNOWN;
  JingleContentState new_state = JINGLE_CONTENT_STATE_EMPTY;

  /* FIXME: we should only do this if the content-disposition != "session"
   * so we ignore it for now */
  DEBUG ("called, but doing nothing for now");
  return;

  if (!gabble_jingle_content_is_ready (self))
      return;

  if (priv->created_by_us)
    {
      /* TODO: set a timer for acknowledgement */
      action = JINGLE_ACTION_CONTENT_ADD;
      new_state = JINGLE_CONTENT_STATE_SENT;
    }
  else
    {
      action = JINGLE_ACTION_CONTENT_ACCEPT;
      new_state = JINGLE_CONTENT_STATE_ACKNOWLEDGED;
    }

  msg = gabble_jingle_session_new_message (self->session,
      action, &sess_node);
  gabble_jingle_content_produce_node (self, sess_node, TRUE);
  _gabble_connection_send (self->conn, msg, NULL);
  lm_message_unref (msg);

  priv->state = new_state;
  g_object_notify (G_OBJECT (self), "state");
}

/* Called by a subclass when the media is ready (e.g. we got local codecs) */
void
_gabble_jingle_content_set_media_ready (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  priv->media_ready = TRUE;
  try_content_add_or_accept (self);

  /* FIXME we abuse this to signal to session that we might be ready */
  g_object_notify (G_OBJECT (self), "ready");

}

void
gabble_jingle_content_set_transport_state (GabbleJingleContent *self,
    JingleTransportState state)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  g_object_set (priv->transport, "state", state, NULL);

  if (state == JINGLE_TRANSPORT_STATE_CONNECTED)
    {
      priv->transport_ready = TRUE;
      try_content_add_or_accept (self);
    }

  /* FIXME we abuse this to signal to session that we might be ready */
  g_object_notify (G_OBJECT (self), "ready");

}

void
gabble_jingle_content_accept (GabbleJingleContent *c)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (c);
  LmMessage *msg;
  LmMessageNode *sess_node;

  g_assert (!priv->created_by_us);
  g_assert (gabble_jingle_content_is_ready (c));

  msg = gabble_jingle_session_new_message (c->session,
      JINGLE_ACTION_CONTENT_ACCEPT, &sess_node);

  gabble_jingle_content_produce_node (c, sess_node, TRUE);
  _gabble_connection_send (c->conn, msg, NULL);
  lm_message_unref (msg);

  g_object_set (c, "state", JINGLE_CONTENT_STATE_ACKNOWLEDGED, NULL);
}
