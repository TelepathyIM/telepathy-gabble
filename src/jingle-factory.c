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

#include "config.h"
#include "jingle-factory.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include <wocky/wocky.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"
#include "gabble-signals-marshal.h"
#include "jingle-share.h"
#include "jingle-media-rtp.h"
#include "jingle-session.h"
#include "jingle-transport-google.h"
#include "jingle-transport-rawudp.h"
#include "jingle-transport-iceudp.h"
#include "namespaces.h"

#include "google-relay.h"

G_DEFINE_TYPE(GabbleJingleFactory, gabble_jingle_factory, G_TYPE_OBJECT);

/* signal enum */
enum
{
    NEW_SESSION,
    QUERY_CAP,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_SESSION = 1,
  LAST_PROPERTY
};

struct _GabbleJingleFactoryPrivate
{
  WockySession *session;
  WockyPorter *porter;
  guint jingle_handler_id;
  GHashTable *content_types;
  GHashTable *transports;

  /* instances of SESSION_MAP_KEY_FORMAT => GabbleJingleSession. */
  GHashTable *sessions;

  GabbleJingleInfo *jingle_info;

  gboolean dispose_has_run;
};

static gboolean jingle_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data);
static GabbleJingleSession *create_session (GabbleJingleFactory *fac,
    const gchar *sid,
    const gchar *jid,
    JingleDialect dialect,
    gboolean local_hold);

static gboolean session_query_cap_cb (
    GabbleJingleSession *session,
    WockyContact *contact,
    const gchar *cap_or_quirk,
    gpointer user_data);
static void session_terminated_cb (GabbleJingleSession *sess,
    gboolean local_terminator,
    JingleReason reason,
    const gchar *text,
    GabbleJingleFactory *fac);

static void attach_to_wocky_session (GabbleJingleFactory *self);

static void
gabble_jingle_factory_init (GabbleJingleFactory *obj)
{
  GabbleJingleFactoryPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_FACTORY,
         GabbleJingleFactoryPrivate);
  obj->priv = priv;

  priv->sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  priv->transports = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  priv->content_types = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  priv->dispose_has_run = FALSE;
}

static void
gabble_jingle_factory_dispose (GObject *object)
{
  GabbleJingleFactory *fac = GABBLE_JINGLE_FACTORY (object);
  GabbleJingleFactoryPrivate *priv = fac->priv;
  GHashTableIter iter;
  gpointer val;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_jingle_factory_stop (fac);
  g_clear_object (&priv->session);
  g_clear_object (&priv->porter);

  g_hash_table_iter_init (&iter, priv->sessions);
  while (g_hash_table_iter_next (&iter, NULL, &val))
    g_signal_handlers_disconnect_by_func (val, session_query_cap_cb, fac);
  g_hash_table_unref (priv->sessions);
  priv->sessions = NULL;

  g_hash_table_unref (priv->content_types);
  priv->content_types = NULL;
  g_hash_table_unref (priv->transports);
  priv->transports = NULL;
  g_clear_object (&priv->jingle_info);

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
  GabbleJingleFactoryPrivate *priv = chan->priv;

  switch (property_id) {
    case PROP_SESSION:
      g_value_set_object (value, priv->session);
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
  GabbleJingleFactoryPrivate *priv = chan->priv;

  switch (property_id) {
    case PROP_SESSION:
      priv->session = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_factory_constructed (GObject *obj)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (obj);
  GObjectClass *parent = G_OBJECT_CLASS (gabble_jingle_factory_parent_class);

  if (parent->constructed != NULL)
    parent->constructed (obj);

  attach_to_wocky_session (self);

  jingle_share_register (self);
  jingle_media_rtp_register (self);
  jingle_transport_google_register (self);
  jingle_transport_rawudp_register (self);
  jingle_transport_iceudp_register (self);
}

static void
gabble_jingle_factory_class_init (GabbleJingleFactoryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleFactoryPrivate));

  object_class->constructed = gabble_jingle_factory_constructed;
  object_class->get_property = gabble_jingle_factory_get_property;
  object_class->set_property = gabble_jingle_factory_set_property;
  object_class->dispose = gabble_jingle_factory_dispose;

  param_spec = g_param_spec_object ("session", "WockySession object",
      "WockySession to listen for Jingle sessions on",
      WOCKY_TYPE_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);

  /* signal definitions */

  /*
   * @session: a fresh new Jingle session for your listening pleasure
   * @initiated_locally: %TRUE if this is a new outgoing session; %FALSE if it
   *  is a new incoming session
   */
  signals[NEW_SESSION] = g_signal_new ("new-session",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, gabble_marshal_VOID__OBJECT_BOOL,
        G_TYPE_NONE, 2, GABBLE_TYPE_JINGLE_SESSION, G_TYPE_BOOLEAN);

  /*
   * @contact: the peer in a call
   * @cap: the XEP-0115 feature string the session is interested in.
   *
   * Emitted when a Jingle session wants to check whether the peer has a
   * particular capability. The handler should return %TRUE if @contact has
   * @cap.
   */
  signals[QUERY_CAP] = g_signal_new ("query-cap",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, g_signal_accumulator_first_wins, NULL,
        gabble_marshal_BOOLEAN__OBJECT_STRING,
        G_TYPE_BOOLEAN, 2, WOCKY_TYPE_CONTACT, G_TYPE_STRING);
}

GabbleJingleFactory *
gabble_jingle_factory_new (
    WockySession *session)
{
  return g_object_new (GABBLE_TYPE_JINGLE_FACTORY,
      "session", session,
      NULL);
}

static void
attach_to_wocky_session (GabbleJingleFactory *self)
{
  GabbleJingleFactoryPrivate *priv = self->priv;

  g_assert (priv->session != NULL);

  g_assert (priv->porter == NULL);
  priv->porter = g_object_ref (wocky_session_get_porter (priv->session));

  /* TODO: we could match different dialects here maybe? */
  priv->jingle_handler_id = wocky_porter_register_handler_from_anyone (
      priv->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, jingle_cb, self,
      NULL);

  priv->jingle_info = gabble_jingle_info_new (priv->porter);
}

void
gabble_jingle_factory_stop (GabbleJingleFactory *self)
{
  GabbleJingleFactoryPrivate *priv = self->priv;

  if (priv->porter != NULL &&
      priv->jingle_handler_id != 0)
    {
      wocky_porter_unregister_handler (priv->porter, priv->jingle_handler_id);
      priv->jingle_handler_id = 0;
    }
}

/* The 'session' map is keyed by:
 * "<peer's jid>\n<session id>"
 */
#define SESSION_MAP_KEY_FORMAT "%s\n%s"

static gchar *
make_session_map_key (
    const gchar *jid,
    const gchar *sid)
{
  return g_strdup_printf (SESSION_MAP_KEY_FORMAT, jid, sid);
}

static gchar *
get_unique_sid_for (GabbleJingleFactory *factory,
    const gchar *jid,
    gchar **key)
{
  guint32 val;
  gchar *sid = NULL;
  gchar *key_ = NULL;

  do
    {
      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      g_free (key_);
      sid = g_strdup_printf ("%u", val);
      key_ = make_session_map_key (jid, sid);
    }
  while (g_hash_table_lookup (factory->priv->sessions, key_) != NULL);

  *key = key_;
  return sid;
}

static GabbleJingleSession *
ensure_session (GabbleJingleFactory *self,
    const gchar *sid,
    const gchar *from,
    JingleAction action,
    JingleDialect dialect,
    gboolean *new_session,
    GError **error)
{
  GabbleJingleFactoryPrivate *priv = self->priv;
  gchar *key;
  GabbleJingleSession *sess;

  if (!wocky_decode_jid (from, NULL, NULL, NULL))
    {
      g_prefix_error (error, "Couldn't parse sender '%s': ", from);
      return NULL;
    }

  /* If we can ensure the handle, we can decode the jid */
  key = make_session_map_key (from, sid);
  sess = g_hash_table_lookup (priv->sessions, key);
  g_free (key);

  if (sess == NULL)
    {
      if (action == JINGLE_ACTION_SESSION_INITIATE)
        {
          sess = create_session (self, sid, from, dialect, FALSE);
          *new_session = TRUE;
        }
      else
        {
          g_set_error (error, WOCKY_XMPP_ERROR,
              WOCKY_JINGLE_ERROR_UNKNOWN_SESSION,
              "session %s is unknown", sid);
          return NULL;
        }
    }
  else
    {
      *new_session = FALSE;
    }

  return sess;
}

static gboolean
jingle_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (user_data);
  GError *error = NULL;
  const gchar *sid, *from;
  GabbleJingleSession *sess;
  gboolean new_session = FALSE;
  JingleAction action;
  JingleDialect dialect;

  /* see if it's a jingle message and detect dialect */
  sid = gabble_jingle_session_detect (msg, &action, &dialect);
  from = wocky_stanza_get_from (msg);

  if (sid == NULL || from == NULL)
    return FALSE;

  sess = ensure_session (self, sid, from, action, dialect, &new_session,
      &error);

  if (sess == NULL)
    goto REQUEST_ERROR;

  /* now act on the message */
  if (!gabble_jingle_session_parse (sess, action, msg, &error))
    goto REQUEST_ERROR;

  /* This has to be after the call to parse(), not inside create_session():
   * until the session has parsed the session-initiate stanza, it does not know
   * about its own contents, and we don't even know if the content types are
   * something we understand. So it's essentially half-alive and useless to
   * signal listeners.
   */
  if (new_session)
    g_signal_emit (self, signals[NEW_SESSION], 0, sess, FALSE);

  /* all went well, we can acknowledge the IQ */
  wocky_porter_acknowledge_iq (porter, msg, NULL);

  return TRUE;

REQUEST_ERROR:
  g_assert (error != NULL);
  DEBUG ("NAKing with error: %s", error->message);
  wocky_porter_send_iq_gerror (porter, msg, error);
  g_error_free (error);

  if (sess != NULL && new_session)
    gabble_jingle_session_terminate (sess, JINGLE_REASON_UNKNOWN, NULL, NULL);

  return TRUE;
}

static gboolean
session_query_cap_cb (
    GabbleJingleSession *session,
    WockyContact *contact,
    const gchar *cap_or_quirk,
    gpointer user_data)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (user_data);
  gboolean ret;

  /* Propagate the query out to the application. We can't depend on the
   * application connecting to ::query-cap on the session because caps queries
   * may happen while parsing the session-initiate stanza, which must happen
   * before the session is announced to the application.
   */
  g_signal_emit (self, signals[QUERY_CAP], 0, contact, cap_or_quirk, &ret);
  return ret;
}

/*
 * If sid is set to NULL a unique sid is generated and
 * the "local-initiator" property of the newly created
 * GabbleJingleSession is set to true.
 */
static GabbleJingleSession *
create_session (GabbleJingleFactory *fac,
    const gchar *sid,
    const gchar *jid,
    JingleDialect dialect,
    gboolean local_hold)
{
  GabbleJingleFactoryPrivate *priv = fac->priv;
  GabbleJingleSession *sess;
  gboolean local_initiator;
  gchar *sid_, *key;
  gpointer contact;
  WockyContactFactory *factory;

  factory = wocky_session_get_contact_factory (priv->session);
  g_assert (jid != NULL);

  if (strchr (jid, '/') != NULL)
    contact = wocky_contact_factory_ensure_resource_contact (factory, jid);
  else
    contact = wocky_contact_factory_ensure_bare_contact (factory, jid);

  g_return_val_if_fail (contact != NULL, NULL);
  g_return_val_if_fail (WOCKY_IS_CONTACT (contact), NULL);

  if (sid != NULL)
    {
      key = make_session_map_key (jid, sid);
      sid_ = g_strdup (sid);

      local_initiator = FALSE;
    }
  else
    {
      sid_ = get_unique_sid_for (fac, jid, &key);

      local_initiator = TRUE;
    }

  /* Either we should have found the existing session when the IQ arrived, or
   * get_unique_sid_for should have ensured the key is fresh. */
  g_assert (NULL == g_hash_table_lookup (priv->sessions, key));

  sess = gabble_jingle_session_new (
      fac,
      priv->porter,
      sid_, local_initiator, contact, dialect, local_hold);
  g_signal_connect (sess, "terminated",
    (GCallback) session_terminated_cb, fac);

  /* Takes ownership of key */
  g_hash_table_insert (priv->sessions, key, sess);

  DEBUG ("new session (%s, %s) @ %p", jid, sid_, sess);

  g_free (sid_);
  g_object_unref (contact);

  g_signal_connect (sess, "query-cap",
      (GCallback) session_query_cap_cb, (GObject *) fac);

  return sess;
}

GabbleJingleSession *
gabble_jingle_factory_create_session (GabbleJingleFactory *fac,
    const gchar *jid,
    JingleDialect dialect,
    gboolean local_hold)
{
  GabbleJingleSession *session = create_session (fac, NULL, jid, dialect, local_hold);

  g_signal_emit (fac, signals[NEW_SESSION], 0, session, TRUE);
  return session;
}

void
gabble_jingle_factory_register_transport (GabbleJingleFactory *self,
                                          gchar *xmlns,
                                          GType transport_type)
{
  g_return_if_fail (g_type_is_a (transport_type,
        GABBLE_TYPE_JINGLE_TRANSPORT_IFACE));

  g_hash_table_insert (self->priv->transports, xmlns,
      GSIZE_TO_POINTER (transport_type));
}

GType
gabble_jingle_factory_lookup_transport (GabbleJingleFactory *self,
                                        const gchar *xmlns)
{
  return GPOINTER_TO_SIZE (g_hash_table_lookup (self->priv->transports,
        xmlns));
}

void
gabble_jingle_factory_register_content_type (GabbleJingleFactory *self,
                                             gchar *xmlns,
                                             GType content_type)
{
  g_return_if_fail (g_type_is_a (content_type, GABBLE_TYPE_JINGLE_CONTENT));

  g_hash_table_insert (self->priv->content_types, xmlns,
      GSIZE_TO_POINTER (content_type));
}

GType
gabble_jingle_factory_lookup_content_type (GabbleJingleFactory *self,
                                           const gchar *xmlns)
{
  return GPOINTER_TO_SIZE (g_hash_table_lookup (self->priv->content_types,
        xmlns));
}

static void
session_terminated_cb (GabbleJingleSession *session,
                       gboolean local_terminator G_GNUC_UNUSED,
                       JingleReason reason G_GNUC_UNUSED,
                       const gchar *text G_GNUC_UNUSED,
                       GabbleJingleFactory *factory)
{
  gchar *key = make_session_map_key (
      gabble_jingle_session_get_peer_jid (session),
      gabble_jingle_session_get_sid (session));

  DEBUG ("removing terminated session with key %s", key);

  g_signal_handlers_disconnect_by_func (session, session_query_cap_cb, factory);
  g_warn_if_fail (g_hash_table_remove (factory->priv->sessions, key));

  g_free (key);
}

GabbleJingleInfo *
gabble_jingle_factory_get_jingle_info (
    GabbleJingleFactory *self)
{
  return self->priv->jingle_info;
}
