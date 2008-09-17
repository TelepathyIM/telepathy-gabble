/*
 * jingle-factory.c - Support for XEP-0166 (Jingle)
 *
 * Copyright (C) 2006-2008 Collabora Ltd.
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

#include "jingle-factory.h"

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
#include "jingle-session.h"

#include <loudmouth/loudmouth.h>


G_DEFINE_TYPE(GabbleJingleFactory, gabble_jingle_factory, G_TYPE_OBJECT);

/* signal enum */
enum
{
    NEW_SESSION,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};


typedef struct _GabbleJingleFactoryPrivate GabbleJingleFactoryPrivate;
struct _GabbleJingleFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *jingle_cb;
  GHashTable *sessions;

  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_FACTORY_GET_PRIVATE(o)\
  ((GabbleJingleFactoryPrivate*)((o)->priv))

static LmHandlerResult
jingle_cb (LmMessageHandler *handler, LmConnection *lmconn,
    LmMessage *message, gpointer user_data);
static GabbleJingleSession *create_session (GabbleJingleFactory *fac,
    const gchar *sid, TpHandle peer);

static void
gabble_jingle_factory_init (GabbleJingleFactory *obj)
{
  GabbleJingleFactoryPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_FACTORY,
         GabbleJingleFactoryPrivate);
  obj->priv = priv;

  priv->sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  obj->transports = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  obj->descriptions = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  priv->jingle_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

static void
gabble_jingle_factory_dispose (GObject *object)
{
  GabbleJingleFactory *fac = GABBLE_JINGLE_FACTORY (object);
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  g_hash_table_destroy (priv->sessions);
  priv->sessions = NULL;

  g_hash_table_destroy (fac->descriptions);
  fac->descriptions = NULL;

  g_hash_table_destroy (fac->transports);
  fac->descriptions = NULL;

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->jingle_cb, LM_MESSAGE_TYPE_IQ);

  if (G_OBJECT_CLASS (gabble_jingle_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_factory_parent_class)->dispose (object);
}

static void
gabble_jingle_factory_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabbleJingleFactory *chan = GABBLE_JINGLE_FACTORY (object);
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_factory_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabbleJingleFactory *chan = GABBLE_JINGLE_FACTORY (object);
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static GObject *
gabble_jingle_factory_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleJingleFactory *self;
  GabbleJingleFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_jingle_factory_parent_class)->
      constructor (type, n_props, props);

  self = GABBLE_JINGLE_FACTORY (obj);
  priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (self);

  priv->jingle_cb = lm_message_handler_new (jingle_cb, self, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
      priv->jingle_cb, LM_MESSAGE_TYPE_IQ, LM_HANDLER_PRIORITY_FIRST);

  return obj;
}

static void
gabble_jingle_factory_class_init (GabbleJingleFactoryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleFactoryPrivate));

  object_class->constructor = gabble_jingle_factory_constructor;
  object_class->get_property = gabble_jingle_factory_get_property;
  object_class->set_property = gabble_jingle_factory_set_property;
  object_class->dispose = gabble_jingle_factory_dispose;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that uses this Jingle Factory object",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  /* signal definitions */

  signals[NEW_SESSION] = g_signal_new ("new-session",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static gboolean
sid_in_use (GabbleJingleFactory *factory, const gchar *sid)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (factory);
  gpointer key, value;

  return g_hash_table_lookup_extended (priv->sessions, sid, &key, &value);
}

static gchar *
get_unique_sid (GabbleJingleFactory *factory)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (factory);
  guint32 val;
  gchar *sid = NULL;
  gboolean unique = FALSE;

  while (!unique)
    {
      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      sid = g_strdup_printf ("%u", val);

      unique = !sid_in_use (factory, sid);
    }

  g_hash_table_insert (priv->sessions, sid, NULL);

  return sid;
}

static void
register_session (GabbleJingleFactory *factory,
                  const gchar *sid,
                  GabbleJingleSession *sess)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (factory);
  gchar *sid_copy;

  if (sid == NULL)
    {
      sid_copy = get_unique_sid (factory);
    }
  else
    {
      sid_copy = g_strdup (sid);
    }

  g_hash_table_insert (priv->sessions, sid_copy, sess);
}

void
_jingle_factory_unregister_session (GabbleJingleFactory *factory,
                                    const gchar *sid)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (factory);
  g_hash_table_remove (priv->sessions, sid);
}

static LmHandlerResult
jingle_cb (LmMessageHandler *handler,
           LmConnection *lmconn,
           LmMessage *msg,
           gpointer user_data)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (user_data);
  GabbleJingleFactoryPrivate *priv =
      GABBLE_JINGLE_FACTORY_GET_PRIVATE (self);
  GError *error = NULL;
  const gchar *sid;
  GabbleJingleSession *sess;
  gboolean new_session = FALSE;

  /* try and validate the message */
  sid = gabble_jingle_session_parse (NULL, msg, &error);
  if (sid == NULL)
    {
      if (error)
        goto REQUEST_ERROR;

      /* else, it's not for us, bail out */
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sess = g_hash_table_lookup (priv->sessions, sid);
  if (sess == NULL)
    {
      new_session = TRUE;
      sess = create_session (self, sid, 0);
    }

  /* now act on the message */
  gabble_jingle_session_parse (sess, msg, &error);

  if (!error)
    {
      if (new_session)
        {
          g_signal_emit (self, signals[NEW_SESSION], 0, sess);
        }

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  /* on parse error */
  g_object_unref (sess);

REQUEST_ERROR:
  _gabble_connection_send_iq_error (
    priv->conn, msg, error->code, error->message);

  g_error_free (error);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/*
 * If sid is set to NULL a unique sid is generated and
 * the "local-initiator" property of the newly created
 * GabbleJingleSession is set to true.
 */
static GabbleJingleSession *
create_session (GabbleJingleFactory *fac,
    const gchar *sid, TpHandle peer)
{
  GabbleJingleFactoryPrivate *priv =
      GABBLE_JINGLE_FACTORY_GET_PRIVATE (fac);
  GabbleJingleSession *sess;
  gboolean local_initiator = TRUE;

  if (sid != NULL)
    {
      g_assert (NULL == g_hash_table_lookup (priv->sessions, sid));
      local_initiator = FALSE;
    }

  sess = g_object_new (GABBLE_TYPE_JINGLE_SESSION,
                       "session-id", sid,
                       "connection", priv->conn,
                       "local-initiator", local_initiator,
                       "peer", peer,
                       NULL);

  register_session (fac, sid, sess);
  return sess;
}

GabbleJingleSession *
gabble_jingle_factory_initiate_session (GabbleJingleFactory *fac,
    TpHandle peer)
{
  return create_session (fac, NULL, peer);
}

void
gabble_jingle_factory_register_transport (GabbleJingleFactory *factory,
    gchar *namespace, JingleTransportMaker maker)
{
  g_hash_table_insert (factory->transports, namespace, maker);
}

void
gabble_jingle_factory_register_description (GabbleJingleFactory *factory,
    gchar *namespace, JingleDescriptionMaker maker)
{
  g_hash_table_insert (factory->descriptions, namespace, maker);
}

