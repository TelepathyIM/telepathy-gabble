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
  LAST_PROPERTY
};

typedef struct _GabbleJingleContentPrivate GabbleJingleContentPrivate;
struct _GabbleJingleContentPrivate
{
  gchar *name;
  gchar *creator;
  gboolean created_by_initiator;
  JingleContentState state;
  JingleContentSenders senders;

  gchar *content_ns;
  gchar *transport_ns;

  GabbleJingleTransportIface *transport;
  gboolean has_local_codecs;

  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_CONTENT_GET_PRIVATE(o)\
  ((GabbleJingleContentPrivate*)((o)->priv))

/* lookup tables */

static const gchar *content_senders_table[] = {
  "initiator",
  "responder",
  "both",
  NULL
};

G_DEFINE_TYPE(GabbleJingleContent, gabble_jingle_content, G_TYPE_OBJECT);

static void
gabble_jingle_content_init (GabbleJingleContent *obj)
{
  GabbleJingleContentPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_CONTENT,
         GabbleJingleContentPrivate);
  obj->priv = priv;

  priv->state = JINGLE_CONTENT_STATE_EMPTY;
  priv->created_by_initiator = TRUE;
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
      break;
    case PROP_SENDERS:
      priv->senders = g_value_get_uint (value);
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
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_string ("content-ns", "Content namespace",
                                    "Namespace identifying the content type.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
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
                                  0, G_MAXUINT32, JINGLE_CONTENT_STATE_NEW,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_uint ("state", "Content state",
                                  "The current state that the content is in.",
                                  0, G_MAXUINT32, JINGLE_CONTENT_STATE_NEW,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  /* signal definitions */

  signals[READY] =
    g_signal_new ("ready",
                  G_OBJECT_CLASS_TYPE (cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

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
          g_object_set (c->session, "dialect", JINGLE_DIALECT_GTALK3, NULL);
          transport_type = GPOINTER_TO_INT (
              g_hash_table_lookup (c->conn->jingle_factory->transports, ""));
        }
    }


  if ((trans_node == NULL) || (creator == NULL) || (name == NULL))
    {
      SET_BAD_REQ ("missing required content attributes or elements");
      return;
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

  priv->created_by_initiator = (!tp_strdiff (creator, "initiator"));
  DEBUG ("senders == %s", senders);
  priv->senders = _string_to_enum (content_senders_table, senders);
  if (priv->senders == JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders");
      return;
    }

  parse_description (c, desc_node, error);
  if (*error)
      return;

  DEBUG ("content creating new transport type %s", g_type_name (transport_type));

  trans = g_object_new (transport_type,
                       "content", c,
                       "transport-ns", priv->transport_ns,
                       NULL);

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
          "creator", priv->creator,
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
gabble_jingle_content_add_candidates (GabbleJingleContent *self, GList *li)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  gabble_jingle_transport_iface_add_candidates (priv->transport, li);
}

gboolean
gabble_jingle_content_is_ready (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);
  JingleTransportState state;

  if (!priv->has_local_codecs)
      return FALSE;

  g_object_get (priv->transport, "state", &state, NULL);

  return (state == JINGLE_TRANSPORT_STATE_CONNECTED);
}

/* FIXME: if local codecs are set multiple times, this will be emitted
 * multiple times - is that scenario possible at all? */
static void
maybe_emit_ready (GabbleJingleContent *self)
{
    if (!gabble_jingle_content_is_ready (self))
        return;

    g_signal_emit (self, signals[READY], 0);
}

void
gabble_jingle_content_set_local_codecs (GabbleJingleContent *self)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);
  priv->has_local_codecs = TRUE;

  /* FIXME: actually this function should call appropriate subclass interface
   * method to set/push local codecs */

  maybe_emit_ready (self);
}

/* FIXME: ok, situation sucks because Content sets the transport state,
 * but also catches the change notification. we ideally want user to
 * be able to access transprot directly, not through content? */
void
gabble_jingle_content_set_transport_state (GabbleJingleContent *self,
    JingleTransportState state)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (self);

  g_object_set (priv->transport, "state", state, NULL);
  maybe_emit_ready (self);
}

