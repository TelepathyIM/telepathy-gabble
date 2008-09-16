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
#include "gabble-connection.h"
#include "util.h"
#include "namespaces.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "jingle-transport-iface.h"

G_DEFINE_TYPE(GabbleJingleContent, gabble_jingle_content, G_TYPE_OBJECT);

/* signal enum */
enum
{
  LAST_SIGNAL
};

// FIXME static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_FACTORY,
  PROP_SESSION,
  PROP_NAME,
  PROP_SENDERS,
  PROP_STATE,
  LAST_PROPERTY
};

typedef struct _GabbleJingleContentPrivate GabbleJingleContentPrivate;
struct _GabbleJingleContentPrivate
{
  GabbleConnection *conn;
  GabbleJingleFactory *factory;
  GabbleJingleSession *session;

  gchar *name;
  gchar *creator;
  gboolean created_by_initiator;
  JingleContentState state;
  JingleContentSenders senders;

  GabbleJingleDescriptionIface *description;
  GabbleJingleTransportIface *transport;

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
}

static void
gabble_jingle_content_dispose (GObject *object)
{
  GabbleJingleContent *sess = GABBLE_JINGLE_CONTENT (object);
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (sess);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  if (priv->description)
      g_object_unref (priv->description);
  priv->description = NULL;

  if (priv->transport)
      g_object_unref (priv->transport);
  priv->transport = NULL;

  g_free (priv->name);
  priv->name = NULL;

  g_free (priv->creator);
  priv->creator = NULL;

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
      g_value_set_object (value, priv->conn);
      break;
    case PROP_FACTORY:
      g_value_set_object (value, priv->factory);
      break;
    case PROP_SESSION:
      g_value_set_object (value, priv->session);
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
      priv->conn = g_value_get_object (value);
      break;
    case PROP_FACTORY:
      priv->factory = g_value_get_object (value);
      break;
    case PROP_SESSION:
      priv->factory = g_value_get_object (value);
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
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

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

  param_spec = g_param_spec_object ("factory", "GabbleJingleFactory object",
                                    "Jingle factory object that has transport "
                                    "and description namespace handlers.",
                                    GABBLE_TYPE_JINGLE_FACTORY,
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
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("name", "Content name",
                                    "A unique content name in the session.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);


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
}

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

void
gabble_jingle_content_parse_add (GabbleJingleContent *c,
    LmMessageNode *content_node, gboolean google_mode, GError **error)
{
  GabbleJingleContentPrivate *priv = GABBLE_JINGLE_CONTENT_GET_PRIVATE (c);
  const gchar *name, *creator, *senders, *xmlns;
  LmMessageNode *desc_node, *trans_node;
  JingleDescriptionMaker dmaker;
  GabbleJingleDescriptionIface *desc = NULL;
  JingleTransportMaker tmaker;
  GabbleJingleTransportIface *trans = NULL;

  desc_node = lm_message_node_get_child (content_node, "description");
  trans_node = lm_message_node_get_child (content_node, "transport");
  creator = lm_message_node_get_attribute (content_node, "creator");
  name = lm_message_node_get_attribute (content_node, "name");
  senders = lm_message_node_get_attribute (content_node, "senders");

  if (desc_node == NULL)
    {
      SET_BAD_REQ ("content description is missing");
      return;
    }

  xmlns = lm_message_node_get_attribute (desc_node, "xmlns");
  dmaker = g_hash_table_lookup (priv->factory->descriptions, xmlns);

  if (dmaker == NULL)
    {
      SET_BAD_REQ ("unsupported content description");
      return;
    }

  if (!google_mode)
    {
      if ((trans_node == NULL) || (creator == NULL) || (name == NULL))
        {
          SET_BAD_REQ ("missing required content attributes or elements");
          return;
        }
    }
  else
    {
      /* explicit is better than implicit */
      if (creator == NULL)
        creator = "initiator";

      if (name == NULL)
        name = "audio";
    }

  if (trans_node)
    {
      xmlns = lm_message_node_get_attribute (trans_node, "xmlns");
      tmaker = g_hash_table_lookup (priv->factory->transports, NULL);
    }
  else
    {
      /* older gtalk assumes google-p2p */
      g_object_set (priv->session, "dialect", JINGLE_DIALECT_GTALK3, NULL);
      tmaker = g_hash_table_lookup (priv->factory->transports, NULL);
    }

  priv->created_by_initiator = (!tp_strdiff (creator, "initiator"));
  priv->senders = _string_to_enum (content_senders_table, senders);
  if (priv->senders == JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders in stream");
      return;
    }

  desc = dmaker (c);
  trans = tmaker (c);

  gabble_jingle_transport_iface_parse (trans, trans_node, error);
  if (*error)
    {
      g_object_unref (desc);
      g_object_unref (trans);
      return;
    }

  gabble_jingle_description_iface_parse (desc, desc_node, error);
  if (*error)
    {
      g_object_unref (desc);
      g_object_unref (trans);
      return;
    }

  g_assert (priv->description == NULL);
  priv->description = desc;

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
  LmMessageNode *content_node;
  JingleDialect dialect;

  g_object_get (priv->session, "dialect", &dialect, NULL);

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
          "creator", priv->creator,
          "name", priv->name,
          "senders", _enum_to_string (content_senders_table, priv->senders));
    }

  if (!full)
    return;

  gabble_jingle_description_iface_produce (priv->description, content_node);
  gabble_jingle_transport_iface_produce (priv->transport, content_node);
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

