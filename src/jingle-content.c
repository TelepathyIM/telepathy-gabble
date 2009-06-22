/*
 * gabble-jingle-content.c - Source for GabbleJingleContent
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

#include "connection.h"
#include "debug.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "jingle-transport-iface.h"
#include "namespaces.h"
#include "util.h"

/* signal enum */
enum
{
  READY,
  NEW_CANDIDATES,
  REMOVED,
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
  PROP_DISPOSITION,
  PROP_LOCALLY_CREATED,
  LAST_PROPERTY
};

struct _GabbleJingleContentPrivate
{
  gchar *name;
  gchar *creator;
  gboolean created_by_us;
  JingleContentState state;
  JingleContentSenders senders;

  gchar *content_ns;
  gchar *transport_ns;
  gchar *disposition;

  GabbleJingleTransportIface *transport;

  gboolean media_ready;
  gboolean transport_ready;

  guint timer_id;
  guint gtalk4_event_id;

  gboolean dispose_has_run;
};

#define DEFAULT_CONTENT_TIMEOUT 60000

/* lookup tables */

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

  DEBUG ("%p", obj);

  priv->state = JINGLE_CONTENT_STATE_EMPTY;
  priv->created_by_us = TRUE;
  priv->media_ready = FALSE;
  priv->transport_ready = FALSE;
  priv->timer_id = 0;
  priv->gtalk4_event_id = 0;
  priv->dispose_has_run = FALSE;

  obj->conn = NULL;
  obj->session = NULL;
}

static void
gabble_jingle_content_dispose (GObject *object)
{
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (object);
  GabbleJingleContentPrivate *priv = content->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("%p", object);
  priv->dispose_has_run = TRUE;

  /* If we're in the middle of content-add/-accept when the session is
   * terminated, we'll get disposed without being explicitly removed from
   * the session. So, remove the timer here. */
  if (priv->timer_id != 0)
    {
      g_source_remove (priv->timer_id);
      priv->timer_id = 0;
    }

  if (priv->gtalk4_event_id != 0)
    {
      g_source_remove (priv->gtalk4_event_id);
      priv->gtalk4_event_id = 0;
    }

  g_free (priv->name);
  priv->name = NULL;

  g_free (priv->creator);
  priv->creator = NULL;

  g_free (priv->content_ns);
  priv->content_ns = NULL;

  g_free (priv->transport_ns);
  priv->transport_ns = NULL;

  g_free (priv->disposition);
  priv->disposition = NULL;

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
  GabbleJingleContentPrivate *priv = self->priv;

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
    case PROP_CONTENT_NS:
      g_value_set_string (value, priv->content_ns);
      break;
    case PROP_TRANSPORT_NS:
      g_value_set_string (value, priv->transport_ns);
      break;
    case PROP_DISPOSITION:
      g_value_set_string (value, priv->disposition);
      break;
    case PROP_LOCALLY_CREATED:
      g_value_set_boolean (value, priv->created_by_us);
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
  GabbleJingleContentPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      self->conn = g_value_get_object (value);
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

      if (priv->transport_ns != NULL)
        {
          GType transport_type = gabble_jingle_factory_lookup_transport (
              self->conn->jingle_factory, priv->transport_ns);

          g_assert (transport_type != 0);

          priv->transport = gabble_jingle_transport_iface_new (transport_type,
              self, priv->transport_ns);

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
      break;
    case PROP_DISPOSITION:
      g_assert (priv->disposition == NULL);
      priv->disposition = g_value_dup_string (value);
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
      "Gabble connection object used for exchanging messages.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("session", "GabbleJingleSession object",
      "Jingle session object that owns this content.",
      GABBLE_TYPE_JINGLE_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);

  param_spec = g_param_spec_string ("name", "Content name",
      "A unique content name in the session.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_string ("content-ns", "Content namespace",
      "Namespace identifying the content type.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTENT_NS, param_spec);

  param_spec = g_param_spec_string ("transport-ns", "Transport namespace",
      "Namespace identifying the transport type.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TRANSPORT_NS, param_spec);

  param_spec = g_param_spec_uint ("senders", "Stream senders",
      "Valid senders for the stream.",
      0, G_MAXUINT32, JINGLE_CONTENT_SENDERS_NONE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SENDERS, param_spec);

  param_spec = g_param_spec_uint ("state", "Content state",
      "The current state that the content is in.",
      0, G_MAXUINT32, JINGLE_CONTENT_STATE_EMPTY,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_string ("disposition", "Content disposition",
      "Distinguishes between 'session' and other contents.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DISPOSITION, param_spec);

  param_spec = g_param_spec_boolean ("locally-created", "Locally created",
      "True if the content was created by the local client.",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCALLY_CREATED, param_spec);

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

  /* This signal serves as notification that the GabbleJingleContent is now
   * meaningless; everything holding a reference should drop it after receiving
   * 'removed'.
   */
  signals[REMOVED] = g_signal_new ("removed",
    G_OBJECT_CLASS_TYPE (cls),
    G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE, 0);
}

static JingleContentSenders
parse_senders (const gchar *txt)
{
  if (txt == NULL)
      return JINGLE_CONTENT_SENDERS_NONE;

  if (!tp_strdiff (txt, "initiator"))
      return JINGLE_CONTENT_SENDERS_INITIATOR;
  else if (!tp_strdiff (txt, "responder"))
      return JINGLE_CONTENT_SENDERS_RESPONDER;
  else if (!tp_strdiff (txt, "both"))
      return JINGLE_CONTENT_SENDERS_BOTH;

  return JINGLE_CONTENT_SENDERS_NONE;
}

static const gchar *
produce_senders (JingleContentSenders senders)
{
  switch (senders) {
    case JINGLE_CONTENT_SENDERS_INITIATOR:
      return "initiator";
    case JINGLE_CONTENT_SENDERS_RESPONDER:
      return "responder";
    case JINGLE_CONTENT_SENDERS_BOTH:
      return "both";
    default:
      DEBUG ("invalid content senders %u", senders);
      g_assert_not_reached ();
  }

  /* to make gcc not complain */
  return NULL;
}


#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

static void
new_transport_candidates_cb (GabbleJingleTransportIface *trans,
    GList *candidates, GabbleJingleContent *content)
{
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

static gboolean
send_gtalk4_transport_accept (gpointer user_data)
{
  GabbleJingleContent *c = GABBLE_JINGLE_CONTENT (user_data);
  GabbleJingleContentPrivate *priv = c->priv;
  LmMessageNode *sess_node, *tnode;
  LmMessage *msg = gabble_jingle_session_new_message (c->session,
      JINGLE_ACTION_TRANSPORT_ACCEPT, &sess_node);

  DEBUG ("Sending Gtalk4 'transport-accept' message to peer");
  tnode = lm_message_node_add_child (sess_node, "transport", NULL);
  lm_message_node_set_attribute (tnode, "xmlns", priv->transport_ns);

  gabble_jingle_session_send (c->session, msg, NULL, NULL);

  return FALSE;
}

void
gabble_jingle_content_parse_add (GabbleJingleContent *c,
    LmMessageNode *content_node, gboolean google_mode, GError **error)
{
  GabbleJingleContentPrivate *priv = c->priv;
  const gchar *name, *creator, *senders, *disposition;
  LmMessageNode *trans_node, *desc_node;
  GType transport_type = 0;
  GabbleJingleTransportIface *trans = NULL;
  JingleDialect dialect;

  g_object_get (c->session, "dialect", &dialect, NULL);

  desc_node = lm_message_node_get_child_any_ns (content_node, "description");
  trans_node = lm_message_node_get_child_any_ns (content_node, "transport");
  creator = lm_message_node_get_attribute (content_node, "creator");
  name = lm_message_node_get_attribute (content_node, "name");
  senders = lm_message_node_get_attribute (content_node, "senders");

  g_assert (priv->transport_ns == NULL);

  if (senders == NULL)
      senders = "both";

  if (google_mode)
    {
      if (creator == NULL)
          creator = "initiator";

      /* the google protocols don't give the contents names, so put in a dummy
       * value if none was set by the session*/
      if (priv->name == NULL)
        name = priv->name = g_strdup ("gtalk");
      else
        name = priv->name;

      if (trans_node == NULL)
        {
          /* gtalk lj0.3 assumes google-p2p transport */
          DEBUG ("detected GTalk3 dialect");

          dialect = JINGLE_DIALECT_GTALK3;
          g_object_set (c->session, "dialect", JINGLE_DIALECT_GTALK3, NULL);
          transport_type = gabble_jingle_factory_lookup_transport (
              c->conn->jingle_factory, "");
          priv->transport_ns = g_strdup ("");
        }
    }
  else
    {
      if ((trans_node == NULL) || (creator == NULL) || (name == NULL))
        {
          SET_BAD_REQ ("missing required content attributes or elements");
          return;
        }

      /* In proper protocols the name comes from the stanza */
      g_assert (priv->name == NULL);
      priv->name = g_strdup (name);
    }

  /* if we didn't set it to google-p2p implicitly already, detect it */
  if (transport_type == 0)
    {
      const gchar *ns = lm_message_node_get_namespace (trans_node);

      transport_type = gabble_jingle_factory_lookup_transport (
          c->conn->jingle_factory, ns);

      if (transport_type == 0)
        {
          SET_BAD_REQ ("unsupported content transport");
          return;
        }

      priv->transport_ns = g_strdup (ns);
    }

  priv->created_by_us = FALSE;
  priv->senders = parse_senders (senders);
  if (priv->senders == JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders");
      return;
    }

  parse_description (c, desc_node, error);
  if (*error != NULL)
      return;

  disposition = lm_message_node_get_attribute (content_node, "disposition");
  if (disposition == NULL)
      disposition = "session";

  priv->disposition = g_strdup (disposition);

  DEBUG ("content creating new transport type %s", g_type_name (transport_type));

  trans = gabble_jingle_transport_iface_new (transport_type,
      c, priv->transport_ns);

  g_signal_connect (trans, "new-candidates",
      (GCallback) new_transport_candidates_cb, c);

  /* Depending on transport, there may be initial candidates specified here */
  if (trans_node != NULL)
    {
      gabble_jingle_transport_iface_parse_candidates (trans, trans_node, error);
      if (*error)
        {
          g_object_unref (trans);
          return;
        }
    }

  g_assert (priv->transport == NULL);
  priv->transport = trans;

  g_assert (priv->creator == NULL);
  priv->creator = g_strdup (creator);

  priv->state = JINGLE_CONTENT_STATE_NEW;

  /* GTALK4 seems to require "transport-accept" for acknowledging
   * the transport type? */
  if (dialect == JINGLE_DIALECT_GTALK4)
    {
      priv->gtalk4_event_id = g_idle_add (send_gtalk4_transport_accept, c);
    }

  return;
}

void
gabble_jingle_content_parse_accept (GabbleJingleContent *c,
    LmMessageNode *content_node, gboolean google_mode, GError **error)
{
  GabbleJingleContentPrivate *priv = c->priv;
  const gchar *senders;
  LmMessageNode *trans_node, *desc_node;
  JingleDialect dialect;
  JingleContentSenders newsenders;

  desc_node = lm_message_node_get_child_any_ns (content_node, "description");
  trans_node = lm_message_node_get_child_any_ns (content_node, "transport");
  senders = lm_message_node_get_attribute (content_node, "senders");

  g_object_get (c->session, "dialect", &dialect, NULL);

  /* FIXME: if we examine dialect manually, we don't need google_mode param flag */
  if (google_mode)
    {
      if (trans_node == NULL)
        {
          DEBUG ("no transport node, assuming GTalk3 dialect");
          /* gtalk lj0.3 assumes google-p2p transport */
          g_object_set (c->session, "dialect", JINGLE_DIALECT_GTALK3, NULL);
        }
    }

  if (senders == NULL)
      senders = "both";

  newsenders = parse_senders (senders);
  if (newsenders == JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders");
      return;
    }

  if (newsenders != priv->senders)
    {
      DEBUG ("changing senders from %s to %s", produce_senders (priv->senders), senders);
      priv->senders = newsenders;
      g_object_notify ((GObject *) c, "senders");
    }

  parse_description (c, desc_node, error);
  if (*error != NULL)
      return;

  if (priv->timer_id != 0)
      g_source_remove (priv->timer_id);
  priv->timer_id = 0;

  priv->state = JINGLE_CONTENT_STATE_ACKNOWLEDGED;
  g_object_notify ((GObject *) c, "state");
}

void
gabble_jingle_content_parse_description_info (GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  GabbleJingleContentPrivate *priv = c->priv;
  LmMessageNode *desc_node;
  desc_node = lm_message_node_get_child_any_ns (content_node, "description");
  if (desc_node == NULL)
    {
      SET_BAD_REQ ("invalid description-info action");
      return;
    }

  if (priv->created_by_us && priv->state < JINGLE_CONTENT_STATE_ACKNOWLEDGED)
    {
      /* The stream was created by us and the other side didn't acknowledge it
       * yet, thus we don't have their codec information, thus the
       * description-info isn't meaningful and can be ignored */
      DEBUG ("Ignoring description-info as we didn't receive the codecs yet");
      return;
    }

  parse_description (c, desc_node, error);
}


void
gabble_jingle_content_produce_node (GabbleJingleContent *c,
  LmMessageNode *parent, gboolean full)
{
  GabbleJingleContentPrivate *priv = c->priv;
  LmMessageNode *content_node, *trans_node;
  JingleDialect dialect;
  void (*produce_desc)(GabbleJingleContent *, LmMessageNode *) =
    GABBLE_JINGLE_CONTENT_GET_CLASS (c)->produce_description;

  g_object_get (c->session, "dialect", &dialect, NULL);

  if ((dialect == JINGLE_DIALECT_GTALK3) ||
      (dialect == JINGLE_DIALECT_GTALK4))
    {
      /* content-* isn't used in GTalk anyways, so we always have to include
       * the full content description */
      g_assert (full == TRUE);

      content_node = parent;
    }
  else
    {
      content_node = lm_message_node_add_child (parent, "content", NULL);
      lm_message_node_set_attributes (content_node,
          "name", priv->name,
          "senders", produce_senders (priv->senders),
          "xmlns", priv->content_ns,
          NULL);

      if (gabble_jingle_content_creator_is_initiator (c))
        lm_message_node_set_attribute (content_node, "creator", "initiator");
      else
        lm_message_node_set_attribute (content_node, "creator", "responder");
    }

  if (!full)
    return;

  produce_desc (c, content_node);

  /* We can do it here, don't need to call into transport object for this */
  if (dialect != JINGLE_DIALECT_GTALK3)
    {
      /* Galk 03 doesn't use a transport, but assumes Gtalk p2p */
      trans_node = lm_message_node_add_child (content_node, "transport", NULL);
      lm_message_node_set_attribute (trans_node, "xmlns", priv->transport_ns);
    }
}

void
gabble_jingle_content_update_senders (GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  GabbleJingleContentPrivate *priv = c->priv;
  JingleContentSenders senders;

  senders = parse_senders (lm_message_node_get_attribute (content_node, "senders"));

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
  GabbleJingleContentPrivate *priv = self->priv;

  gabble_jingle_transport_iface_parse_candidates (priv->transport, trans_node, error);
}

/* Takes in a list of slice-allocated JingleCandidate structs */
void
gabble_jingle_content_add_candidates (GabbleJingleContent *self, GList *li)
{
  GabbleJingleContentPrivate *priv = self->priv;

  gabble_jingle_transport_iface_add_candidates (priv->transport, li);
}

/* Returns whether the content is ready to be signalled (initiated, for local
 * streams, or acknowledged, for remote streams. */
gboolean
gabble_jingle_content_is_ready (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = self->priv;

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

static gboolean
timeout_content (gpointer data)
{
  GabbleJingleContent *c = data;

  c->priv->timer_id = 0;
  DEBUG ("content timed out");

  /* we're handling it as if it were rejected */
  gabble_jingle_content_remove (c, FALSE);
  return FALSE;
}

static void
send_content_add_or_accept (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = self->priv;
  LmMessage *msg;
  LmMessageNode *sess_node;
  JingleAction action;
  JingleContentState new_state = JINGLE_CONTENT_STATE_EMPTY;

  g_assert (gabble_jingle_content_is_ready (self));

  if (priv->created_by_us)
    {
      /* TODO: set a timer for acknowledgement */
      action = JINGLE_ACTION_CONTENT_ADD;
      new_state = JINGLE_CONTENT_STATE_SENT;

      g_assert (priv->timer_id == 0);
      priv->timer_id = g_timeout_add (DEFAULT_CONTENT_TIMEOUT,
        timeout_content, self);
    }
  else
    {
      action = JINGLE_ACTION_CONTENT_ACCEPT;
      new_state = JINGLE_CONTENT_STATE_ACKNOWLEDGED;
    }

  msg = gabble_jingle_session_new_message (self->session,
      action, &sess_node);
  gabble_jingle_content_produce_node (self, sess_node, TRUE);
  gabble_jingle_session_send (self->session, msg, NULL, NULL);

  priv->state = new_state;
  g_object_notify (G_OBJECT (self), "state");
}

static void
_maybe_ready (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = self->priv;
  JingleSessionState state;

  if (!gabble_jingle_content_is_ready (self))
      return;

  /* If content disposition is session and session
   * is not yet acknowledged/active, we signall
   * the readiness to the session and let it take
   * care of it. Otherwise, we can deal with it
   * ourselves. */

  g_object_get (self->session, "state", &state, NULL);

  if (!tp_strdiff (priv->disposition, "session") &&
      (state < JS_STATE_PENDING_ACCEPT_SENT))
    {
      /* Notify the session that we're ready for
       * session-initiate/session-accept */
      g_signal_emit (self, signals[READY], 0);
    }
  else
    {
      if (state >= JS_STATE_PENDING_INITIATE_SENT)
        {
          send_content_add_or_accept (self);

          /* if neccessary, transmit the candidates */
          gabble_jingle_transport_iface_retransmit_candidates (priv->transport,
              FALSE);
        }
      else
        {
          /* non session-disposition content ready without session
           * being initiated at all? */
          DEBUG ("session not initiated yet, ignoring non-session ready content");
          return;
        }
    }
}

void
gabble_jingle_content_maybe_send_description (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = self->priv;
  LmMessage *msg;
  LmMessageNode *sess_node;

  /* If we didn't send the content yet there is no reason to send a
   * description-info to update it */
  if (priv->state < JINGLE_CONTENT_STATE_SENT)
    return;

  msg = gabble_jingle_session_new_message (self->session,
      JINGLE_ACTION_DESCRIPTION_INFO, &sess_node);
  gabble_jingle_content_produce_node (self, sess_node, TRUE);
  gabble_jingle_session_send (self->session, msg, NULL, NULL);
}


/* Used when session-initiate is sent (so all initial contents transmit their
 * candidates), and when we detect gtalk3 after we've transmitted some
 * candidates. */
void
gabble_jingle_content_retransmit_candidates (GabbleJingleContent *self)
{
  gabble_jingle_transport_iface_retransmit_candidates (self->priv->transport, TRUE);
}

/* Called by a subclass when the media is ready (e.g. we got local codecs) */
void
_gabble_jingle_content_set_media_ready (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = self->priv;


  priv->media_ready = TRUE;

  _maybe_ready (self);
}

void
gabble_jingle_content_set_transport_state (GabbleJingleContent *self,
    JingleTransportState state)
{
  GabbleJingleContentPrivate *priv = self->priv;

  g_object_set (priv->transport, "state", state, NULL);

  if (state == JINGLE_TRANSPORT_STATE_CONNECTED)
    {
      priv->transport_ready = TRUE;
      _maybe_ready (self);
    }
}

GList *
gabble_jingle_content_get_remote_candidates (GabbleJingleContent *c)
{
  GabbleJingleContentPrivate *priv = c->priv;

  return gabble_jingle_transport_iface_get_remote_candidates (priv->transport);
}

gboolean
gabble_jingle_content_change_direction (GabbleJingleContent *c,
    JingleContentSenders senders)
{
  GabbleJingleContentPrivate *priv = c->priv;
  LmMessage *msg;
  LmMessageNode *sess_node;
  JingleDialect dialect;

  g_object_get (c->session, "dialect", &dialect, NULL);

  if (JINGLE_IS_GOOGLE_DIALECT (dialect))
    {
      DEBUG ("ignoring direction change request for GTalk stream");
      return FALSE;
    }

  priv->senders = senders;

  msg = gabble_jingle_session_new_message (c->session,
      JINGLE_ACTION_CONTENT_MODIFY, &sess_node);
  gabble_jingle_content_produce_node (c, sess_node, FALSE);
  gabble_jingle_session_send (c->session, msg, NULL, NULL);

  /* FIXME: actually check whether remote end accepts our content-modify */
  return TRUE;
}

static void
_on_remove_reply (GObject *c_as_obj,
    gboolean success,
    LmMessage *reply)
{
  GabbleJingleContent *c = GABBLE_JINGLE_CONTENT (c_as_obj);
  GabbleJingleContentPrivate *priv = c->priv;

  g_assert (priv->state == JINGLE_CONTENT_STATE_REMOVING);

  DEBUG ("%p", c);

  /* Everything holding a reference to a content should drop it after receiving
   * 'removed'.
   */
  g_signal_emit (c, signals[REMOVED], 0);
}

void
gabble_jingle_content_remove (GabbleJingleContent *c, gboolean signal_peer)
{
  GabbleJingleContentPrivate *priv = c->priv;
  LmMessage *msg;
  LmMessageNode *sess_node;

  DEBUG ("called for %p (%s)", c, priv->name);

  if (priv->timer_id != 0)
    {
      g_source_remove (priv->timer_id);
      priv->timer_id = 0;
    }

  /* If we were already signalled and removal is not a side-effect of
   * something else (sesssion termination, or removal by peer),
   * we have to signal removal to the peer. */
  if (signal_peer && (priv->state != JINGLE_CONTENT_STATE_EMPTY))
    {
      if (priv->state == JINGLE_CONTENT_STATE_REMOVING)
        {
          DEBUG ("ignoring request to remove content which is already being removed");
          return;
        }

      priv->state = JINGLE_CONTENT_STATE_REMOVING;
      g_object_notify ((GObject *) c, "state");

      msg = gabble_jingle_session_new_message (c->session,
          JINGLE_ACTION_CONTENT_REMOVE, &sess_node);
      gabble_jingle_content_produce_node (c, sess_node, FALSE);
      gabble_jingle_session_send (c->session, msg, _on_remove_reply,
          (GObject *) c);
    }
  else
    {
      DEBUG ("signalling removed with %u refs", G_OBJECT (c)->ref_count);
      /* Everything holding a reference to a content should drop it after receiving
       * 'removed'.
       */
      g_signal_emit (c, signals[REMOVED], 0);
    }
}

gboolean
gabble_jingle_content_is_created_by_us (GabbleJingleContent *c)
{
  return c->priv->created_by_us;
}

gboolean
gabble_jingle_content_creator_is_initiator (GabbleJingleContent *c)
{
  gboolean session_created_by_us;

  g_object_get (c->session, "local-initiator", &session_created_by_us, NULL);

  return (c->priv->created_by_us == session_created_by_us);
}

const gchar *
gabble_jingle_content_get_name (GabbleJingleContent *self)
{
  return self->priv->name;
}

const gchar *
gabble_jingle_content_get_ns (GabbleJingleContent *self)
{
  return self->priv->content_ns;
}

const gchar *
gabble_jingle_content_get_disposition (GabbleJingleContent *self)
{
  return self->priv->disposition;
}
