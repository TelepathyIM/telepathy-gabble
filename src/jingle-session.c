/*
 * gabble-jingle-session.c - Source for GabbleJingleSession
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

#include "jingle-session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <wocky/wocky.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "gabble/capabilities.h"
#include "connection.h"
#include "conn-presence.h"
#include "debug.h"
#include "gabble-signals-marshal.h"
#include "gabble-enumtypes.h"
#include "jingle-content.h"
#include "jingle-factory.h"
/* FIXME: the RTP-specific bits of this file should be separated from the
 *        generic Jingle code.
 */
#include "jingle-media-rtp.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "util.h"

G_DEFINE_TYPE(GabbleJingleSession, gabble_jingle_session, G_TYPE_OBJECT);

/* signal enum */
enum
{
  NEW_CONTENT,
  REMOTE_STATE_CHANGED,
  TERMINATED,
  CONTENT_REJECTED,
  QUERY_CAP,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_JINGLE_FACTORY,
  PROP_PORTER,
  PROP_SESSION_ID,
  PROP_PEER_CONTACT,
  PROP_LOCAL_INITIATOR,
  PROP_STATE,
  PROP_DIALECT,
  PROP_LOCAL_HOLD,
  PROP_REMOTE_HOLD,
  PROP_REMOTE_RINGING,
  LAST_PROPERTY
};

struct _GabbleJingleSessionPrivate
{
  GabbleConnection *conn;
  /* Borrowed; the factory owns us. */
  GabbleJingleFactory *jingle_factory;
  WockyPorter *porter;

  WockyContact *peer_contact;
  /* Borrowed from peer_contact if it's a WockyResourceContact. */
  const gchar *peer_resource;
  gchar *peer_jid;
  /* Either borrowed from 'porter' or equal to peer_jid. */
  const gchar *initiator;
  gboolean local_initiator;

  /* GabbleJingleContent objects keyed by content name.
   * Table owns references to these objects. */
  GHashTable *initiator_contents;
  GHashTable *responder_contents;

  JingleDialect dialect;
  JingleState state;
  gchar *sid;

  gboolean locally_accepted;
  gboolean locally_terminated;

  gboolean local_hold;

  gboolean remote_hold;
  gboolean remote_ringing;

  gboolean dispose_has_run;
};

typedef struct {
  JingleState state;
  JingleAction *actions;
} JingleStateActions;

/* gcc should be able to figure this out from the table below, but.. */
#define MAX_ACTIONS_PER_STATE 12

/* NB: JINGLE_ACTION_UNKNOWN is used as a terminator here. */
static JingleAction allowed_actions[MAX_JINGLE_STATES][MAX_ACTIONS_PER_STATE] = {
  /* JINGLE_STATE_PENDING_CREATED */
  { JINGLE_ACTION_SESSION_INITIATE, JINGLE_ACTION_UNKNOWN },
  /* JINGLE_STATE_PENDING_INITIATE_SENT */
  { JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_SESSION_ACCEPT,
    JINGLE_ACTION_TRANSPORT_ACCEPT, /* required for GTalk4 */
    JINGLE_ACTION_DESCRIPTION_INFO, JINGLE_ACTION_SESSION_INFO,
    JINGLE_ACTION_TRANSPORT_INFO, JINGLE_ACTION_INFO,
    JINGLE_ACTION_UNKNOWN },
  /* JINGLE_STATE_PENDING_INITIATED */
  { JINGLE_ACTION_SESSION_ACCEPT, JINGLE_ACTION_SESSION_TERMINATE,
    JINGLE_ACTION_TRANSPORT_INFO, JINGLE_ACTION_CONTENT_REJECT,
    JINGLE_ACTION_CONTENT_MODIFY, JINGLE_ACTION_CONTENT_ACCEPT,
    JINGLE_ACTION_CONTENT_REMOVE,  JINGLE_ACTION_DESCRIPTION_INFO,
    JINGLE_ACTION_TRANSPORT_ACCEPT, JINGLE_ACTION_SESSION_INFO,
    JINGLE_ACTION_INFO,
    JINGLE_ACTION_UNKNOWN },
  /* JINGLE_STATE_PENDING_ACCEPT_SENT */
  { JINGLE_ACTION_TRANSPORT_INFO, JINGLE_ACTION_DESCRIPTION_INFO,
    JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_SESSION_INFO,
    JINGLE_ACTION_INFO,
    JINGLE_ACTION_UNKNOWN },
  /* JINGLE_STATE_ACTIVE */
  { JINGLE_ACTION_CONTENT_MODIFY, JINGLE_ACTION_CONTENT_ADD,
    JINGLE_ACTION_CONTENT_REMOVE, JINGLE_ACTION_CONTENT_REPLACE,
    JINGLE_ACTION_CONTENT_ACCEPT, JINGLE_ACTION_CONTENT_REJECT,
    JINGLE_ACTION_SESSION_INFO, JINGLE_ACTION_TRANSPORT_INFO,
    JINGLE_ACTION_DESCRIPTION_INFO, JINGLE_ACTION_INFO,
    JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_UNKNOWN },
  /* JINGLE_STATE_ENDED */
  { JINGLE_ACTION_UNKNOWN }
};

gboolean
gabble_jingle_session_defines_action (GabbleJingleSession *sess,
    JingleAction a)
{
  JingleDialect d = sess->priv->dialect;

  if (a == JINGLE_ACTION_UNKNOWN)
    return FALSE;

  switch (d)
    {
      case JINGLE_DIALECT_V032:
        return TRUE;
      case JINGLE_DIALECT_V015:
        return (a != JINGLE_ACTION_DESCRIPTION_INFO &&
            a != JINGLE_ACTION_SESSION_INFO);
      case JINGLE_DIALECT_GTALK4:
        if (a == JINGLE_ACTION_TRANSPORT_ACCEPT ||
            a == JINGLE_ACTION_INFO )
          return TRUE;
      case JINGLE_DIALECT_GTALK3:
        return (a == JINGLE_ACTION_SESSION_ACCEPT ||
            a == JINGLE_ACTION_SESSION_INITIATE ||
            a == JINGLE_ACTION_SESSION_TERMINATE ||
            a == JINGLE_ACTION_TRANSPORT_INFO ||
            a == JINGLE_ACTION_INFO);
      default:
        return FALSE;
    }
}

static void gabble_jingle_session_send_held (GabbleJingleSession *sess);
static void content_ready_cb (GabbleJingleContent *c, gpointer user_data);
static void content_removed_cb (GabbleJingleContent *c, gpointer user_data);

static void
gabble_jingle_session_init (GabbleJingleSession *obj)
{
  GabbleJingleSessionPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_SESSION,
         GabbleJingleSessionPrivate);
  obj->priv = priv;

  DEBUG ("Initializing the jingle session %p", obj);

  priv->initiator_contents = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
  priv->responder_contents = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  priv->state = JINGLE_STATE_PENDING_CREATED;
  priv->locally_accepted = FALSE;
  priv->locally_terminated = FALSE;
  priv->dispose_has_run = FALSE;
}

static void
dispose_content_hash (
    GabbleJingleSession *sess,
    GHashTable **contents)
{
  GHashTableIter iter;
  gpointer content;

  g_hash_table_iter_init (&iter, *contents);
  while (g_hash_table_iter_next (&iter, NULL, &content))
    {
      g_signal_handlers_disconnect_by_func (content, content_ready_cb, sess);
      g_signal_handlers_disconnect_by_func (content, content_removed_cb, sess);
      g_hash_table_iter_remove (&iter);
    }

  tp_clear_pointer (contents, g_hash_table_unref);
}

static void
gabble_jingle_session_dispose (GObject *object)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (object);
  GabbleJingleSessionPrivate *priv = sess->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("called");
  priv->dispose_has_run = TRUE;

  g_assert ((priv->state == JINGLE_STATE_PENDING_CREATED) ||
      (priv->state == JINGLE_STATE_ENDED));

  dispose_content_hash (sess, &priv->initiator_contents);
  dispose_content_hash (sess, &priv->responder_contents);

  tp_clear_object (&priv->peer_contact);
  tp_clear_object (&priv->porter);

  g_free (priv->sid);
  priv->sid = NULL;

  g_free (priv->peer_jid);
  priv->peer_jid = NULL;

  if (G_OBJECT_CLASS (gabble_jingle_session_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_session_parent_class)->dispose (object);
}

static void
gabble_jingle_session_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (object);
  GabbleJingleSessionPrivate *priv = sess->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_JINGLE_FACTORY:
      g_value_set_object (value, priv->jingle_factory);
      break;
    case PROP_PORTER:
      g_value_set_object (value, priv->porter);
      break;
    case PROP_SESSION_ID:
      g_value_set_string (value, priv->sid);
      break;
    case PROP_LOCAL_INITIATOR:
      g_value_set_boolean (value, priv->local_initiator);
      break;
    case PROP_PEER_CONTACT:
      g_value_set_object (value, priv->peer_contact);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    case PROP_DIALECT:
      g_value_set_uint (value, priv->dialect);
      break;
    case PROP_LOCAL_HOLD:
      g_value_set_boolean (value, priv->local_hold);
      break;
    case PROP_REMOTE_HOLD:
      g_value_set_boolean (value, priv->remote_hold);
      break;
    case PROP_REMOTE_RINGING:
      g_value_set_boolean (value, priv->remote_ringing);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_session_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (object);
  GabbleJingleSessionPrivate *priv = sess->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      g_assert (priv->conn != NULL);
      break;
    case PROP_JINGLE_FACTORY:
      priv->jingle_factory = g_value_get_object (value);
      g_assert (priv->jingle_factory != NULL);
      break;
    case PROP_PORTER:
      priv->porter = g_value_dup_object (value);
      g_assert (priv->porter != NULL);
      break;
    case PROP_SESSION_ID:
      g_free (priv->sid);
      priv->sid = g_value_dup_string (value);
      break;
    case PROP_LOCAL_INITIATOR:
      priv->local_initiator = g_value_get_boolean (value);
      break;
    case PROP_DIALECT:
      priv->dialect = g_value_get_uint (value);
      break;
    case PROP_PEER_CONTACT:
      priv->peer_contact = g_value_dup_object (value);
      break;
    case PROP_LOCAL_HOLD:
      {
        gboolean local_hold = g_value_get_boolean (value);

        if (priv->local_hold != local_hold)
          {
            priv->local_hold = local_hold;

            if (priv->state >= JINGLE_STATE_PENDING_INITIATED &&
                priv->state < JINGLE_STATE_ENDED)
              gabble_jingle_session_send_held (sess);

            /* else, we'll send this in set_state when we move to PENDING_INITIATED or
             * better.
             */
          }
        break;
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      g_assert_not_reached ();
      break;
  }
}

static void
gabble_jingle_session_constructed (GObject *object)
{
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (gabble_jingle_session_parent_class)->constructed;
  GabbleJingleSession *self = GABBLE_JINGLE_SESSION (object);
  GabbleJingleSessionPrivate *priv = self->priv;

  if (chain_up != NULL)
    chain_up (object);

  g_assert (priv->conn != NULL);
  g_assert (priv->jingle_factory != NULL);
  g_assert (priv->porter != NULL);
  g_assert (priv->peer_contact != NULL);
  g_assert (priv->sid != NULL);

  priv->peer_jid = wocky_contact_dup_jid (priv->peer_contact);

  if (priv->local_initiator)
    priv->initiator = wocky_porter_get_full_jid (priv->porter);
  else
    priv->initiator = priv->peer_jid;

  if (WOCKY_IS_RESOURCE_CONTACT (priv->peer_contact))
    priv->peer_resource = wocky_resource_contact_get_resource (
        WOCKY_RESOURCE_CONTACT (priv->peer_contact));
}

GabbleJingleSession *
gabble_jingle_session_new (GabbleConnection *connection,
                           GabbleJingleFactory *factory,
                           WockyPorter *porter,
                           const gchar *session_id,
                           gboolean local_initiator,
                           WockyContact *peer,
                           gboolean local_hold)
{
  return g_object_new (GABBLE_TYPE_JINGLE_SESSION,
      "session-id", session_id,
      "connection", connection,
      "jingle-factory", factory,
      "porter", porter,
      "local-initiator", local_initiator,
      "peer-contact", peer,
      "local-hold", local_hold,
      NULL);
}

static void
gabble_jingle_session_class_init (GabbleJingleSessionClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleSessionPrivate));

  object_class->constructed = gabble_jingle_session_constructed;
  object_class->get_property = gabble_jingle_session_get_property;
  object_class->set_property = gabble_jingle_session_set_property;
  object_class->dispose = gabble_jingle_session_dispose;

  /* property definitions */
  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object used for exchanging messages.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("jingle-factory",
      "GabbleJingleFactory object",
      "The Jingle factory which created this session",
      GABBLE_TYPE_JINGLE_FACTORY,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JINGLE_FACTORY, param_spec);

  param_spec = g_param_spec_object ("porter", "WockyPorter",
      "The WockyPorter for the current connection",
      WOCKY_TYPE_PORTER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PORTER, param_spec);

  param_spec = g_param_spec_string ("session-id", "Session ID",
      "A unique session identifier used throughout all communication.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SESSION_ID, param_spec);

  param_spec = g_param_spec_boolean ("local-initiator", "Session initiator",
      "Specifies if local end initiated the session.",
      TRUE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_INITIATOR,
      param_spec);

  /**
   * GabbleJingleSession:peer-contact:
   *
   * The #WockyContact representing the other party in the session. Note that
   * if this is a #WockyBareContact (as opposed to a #WockyResourceContact) the
   * session is with the contact's bare JID.
   */
  param_spec = g_param_spec_object ("peer-contact", "Session peer",
      "The WockyContact representing the other party in the session.",
      WOCKY_TYPE_CONTACT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PEER_CONTACT, param_spec);

  param_spec = g_param_spec_uint ("state", "Session state",
      "The current state that the session is in.",
      0, G_MAXUINT32, JINGLE_STATE_PENDING_CREATED,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_uint ("dialect", "Jingle dialect",
      "Jingle dialect used for this session.",
      0, G_MAXUINT32, JINGLE_DIALECT_ERROR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DIALECT, param_spec);

  param_spec = g_param_spec_boolean ("local-hold", "Local hold",
      "TRUE if we've placed the peer on hold", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_HOLD, param_spec);

  param_spec = g_param_spec_boolean ("remote-hold", "Remote hold",
      "TRUE if the peer has placed us on hold", FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_HOLD, param_spec);

  param_spec = g_param_spec_boolean ("remote-ringing", "Remote ringing",
      "TRUE if the peer's client is ringing", FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_RINGING,
      param_spec);

  /* signal definitions */

  signals[NEW_CONTENT] = g_signal_new ("new-content",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[TERMINATED] = g_signal_new ("terminated",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, gabble_marshal_VOID__BOOLEAN_UINT_STRING,
        G_TYPE_NONE, 3, G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_STRING);

  signals[REMOTE_STATE_CHANGED] = g_signal_new ("remote-state-changed",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);
  signals[CONTENT_REJECTED] = g_signal_new ("content-rejected",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, gabble_marshal_VOID__OBJECT_UINT_STRING,
        G_TYPE_NONE, 3, G_TYPE_OBJECT, G_TYPE_UINT, G_TYPE_STRING);

  /*
   * @contact: this call's peer (the artist commonly known as
   *  gabble_jingle_session_get_peer_contact())
   * @cap: the XEP-0115 feature string the session is interested in.
   *
   * Emitted when the session wants to check whether the peer has a particular
   * capability. The handler should return %TRUE if @contact has @cap.
   */
  signals[QUERY_CAP] = g_signal_new ("query-cap",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, g_signal_accumulator_first_wins, NULL,
        gabble_marshal_BOOLEAN__OBJECT_STRING,
        G_TYPE_BOOLEAN, 2, WOCKY_TYPE_CONTACT, G_TYPE_STRING);
}

typedef void (*HandlerFunc)(GabbleJingleSession *sess,
    WockyNode *node, GError **error);
typedef void (*ContentHandlerFunc)(GabbleJingleSession *sess,
    GabbleJingleContent *c, WockyNode *content_node, gpointer user_data,
    GError **error);

static gboolean
extract_reason (WockyNode *node, JingleReason *reason, gchar **message)
{
  JingleReason _reason = JINGLE_REASON_UNKNOWN;
  WockyNode *child;
  WockyNodeIter iter;

  g_return_val_if_fail (node != NULL, FALSE);

  if (message != NULL)
    *message = g_strdup (wocky_node_get_content_from_child (node, "text"));

  wocky_node_iter_init (&iter, node, NULL, NULL);

  while (wocky_node_iter_next (&iter, &child))
    {
      if (wocky_enum_from_nick (
              jingle_reason_get_type (), child->name, (gint *) &_reason))
        {
          if (reason != NULL)
            *reason = _reason;
          return TRUE;
        }
    }

  return FALSE;
}

static JingleAction
parse_action (const gchar *txt)
{
  if (txt == NULL)
      return JINGLE_ACTION_UNKNOWN;

  /* synonyms, best deal with them right now */
  if (!tp_strdiff (txt, "initiate") ||
      !tp_strdiff (txt, "session-initiate"))
        return JINGLE_ACTION_SESSION_INITIATE;
  else if (!tp_strdiff (txt, "terminate") ||
      !tp_strdiff (txt, "session-terminate") ||
      !tp_strdiff (txt, "reject"))
        return JINGLE_ACTION_SESSION_TERMINATE;
  else if (!tp_strdiff (txt, "accept") ||
      !tp_strdiff (txt, "session-accept"))
        return JINGLE_ACTION_SESSION_ACCEPT;
  else if (!tp_strdiff (txt, "candidates") ||
      !tp_strdiff (txt, "transport-info"))
        return JINGLE_ACTION_TRANSPORT_INFO;
  else if (!tp_strdiff (txt, "content-accept"))
      return JINGLE_ACTION_CONTENT_ACCEPT;
  else if (!tp_strdiff (txt, "content-add"))
      return JINGLE_ACTION_CONTENT_ADD;
  else if (!tp_strdiff (txt, "content-modify"))
      return JINGLE_ACTION_CONTENT_MODIFY;
  else if (!tp_strdiff (txt, "content-replace"))
      return JINGLE_ACTION_CONTENT_REPLACE;
  else if (!tp_strdiff (txt, "content-reject"))
      return JINGLE_ACTION_CONTENT_REJECT;
  else if (!tp_strdiff (txt, "content-remove"))
      return JINGLE_ACTION_CONTENT_REMOVE;
  else if (!tp_strdiff (txt, "session-info"))
      return JINGLE_ACTION_SESSION_INFO;
  else if (!tp_strdiff (txt, "transport-accept"))
      return JINGLE_ACTION_TRANSPORT_ACCEPT;
  else if (!tp_strdiff (txt, "description-info"))
      return JINGLE_ACTION_DESCRIPTION_INFO;
  else if (!tp_strdiff (txt, "info"))
      return JINGLE_ACTION_INFO;

  return JINGLE_ACTION_UNKNOWN;
}

static const gchar *
produce_action (JingleAction action, JingleDialect dialect)
{
  gboolean gmode = (dialect == JINGLE_DIALECT_GTALK3) ||
      (dialect == JINGLE_DIALECT_GTALK4);

  g_return_val_if_fail (action != JINGLE_ACTION_UNKNOWN, NULL);

  switch (action) {
    case JINGLE_ACTION_SESSION_INITIATE:
      return (gmode) ? "initiate" : "session-initiate";
    case JINGLE_ACTION_SESSION_TERMINATE:
      return (gmode) ? "terminate" : "session-terminate";
    case JINGLE_ACTION_SESSION_ACCEPT:
      return (gmode) ? "accept" : "session-accept";
    case JINGLE_ACTION_TRANSPORT_INFO:
      return (dialect == JINGLE_DIALECT_GTALK3) ?
        "candidates" : "transport-info";
    case JINGLE_ACTION_CONTENT_ACCEPT:
      return "content-accept";
    case JINGLE_ACTION_CONTENT_ADD:
      return "content-add";
    case JINGLE_ACTION_CONTENT_MODIFY:
      return "content-modify";
    case JINGLE_ACTION_CONTENT_REMOVE:
      return "content-remove";
    case JINGLE_ACTION_CONTENT_REPLACE:
      return "content-replace";
    case JINGLE_ACTION_CONTENT_REJECT:
      return "content-reject";
    case JINGLE_ACTION_SESSION_INFO:
      return "session-info";
    case JINGLE_ACTION_TRANSPORT_ACCEPT:
      return "transport-accept";
    case JINGLE_ACTION_DESCRIPTION_INFO:
      return "description-info";
    case JINGLE_ACTION_INFO:
      return "info";
    default:
      /* only reached if g_return_val_if_fail is disabled */
      DEBUG ("unknown action %u", action);
      return NULL;
    }
}

static gboolean
action_is_allowed (JingleAction action, JingleState state)
{
  guint i;

  for (i = 0; allowed_actions[state][i] != JINGLE_ACTION_UNKNOWN; i++)
    {
      if (allowed_actions[state][i] == action)
        return TRUE;
    }

  return FALSE;
}

static void gabble_jingle_session_send_rtp_info (GabbleJingleSession *sess,
    const gchar *name);
static void set_state (GabbleJingleSession *sess,
    JingleState state, JingleReason termination_reason, const gchar *text);
static GabbleJingleContent *_get_any_content (GabbleJingleSession *session);

gboolean
gabble_jingle_session_peer_has_cap (
    GabbleJingleSession *self,
    const gchar *cap_or_quirk)
{
  GabbleJingleSessionPrivate *priv = self->priv;
  gboolean ret;

  g_signal_emit (self, signals[QUERY_CAP], 0,
      priv->peer_contact, cap_or_quirk,
      &ret);
  return ret;
}

static gboolean
lookup_content (GabbleJingleSession *sess,
    const gchar *name,
    const gchar *creator,
    gboolean fail_if_missing,
    GabbleJingleContent **c,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  if (name == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "'name' attribute unset");
      return FALSE;
    }

  if (JINGLE_IS_GOOGLE_DIALECT (priv->dialect))
    {
      /* Only the initiator can create contents on GTalk. */
      *c = g_hash_table_lookup (priv->initiator_contents, name);
    }
  else
    {
      /* Versions of Gabble between 0.7.16 and 0.7.28 (inclusive) omitted the
       * 'creator' attribute from transport-info (and possibly other) stanzas.
       * We try to detect contacts using such a version of Gabble from their
       * caps; if 'creator' is missing and the peer has that caps flag, we look
       * up the content in both hashes.
       *
       * While this doesn't deal with the case where the content is found in
       * both hashes, this isn't a problem in practice: the versions of Gabble
       * we're working around didn't allow this to happen (they'd either reject
       * the second stream, or let it replace the first, depending on the phase
       * of the moon, and get kind of confused in the process), and we try to
       * pick globally-unique content names.
       */
      if (creator == NULL &&
          gabble_jingle_session_peer_has_cap (sess,
              QUIRK_OMITS_CONTENT_CREATORS))
        {
          DEBUG ("working around missing 'creator' attribute");

          *c = g_hash_table_lookup (priv->initiator_contents, name);

          if (*c == NULL)
            *c = g_hash_table_lookup (priv->responder_contents, name);
        }
      else if (!tp_strdiff (creator, "initiator"))
        {
          *c = g_hash_table_lookup (priv->initiator_contents, name);
        }
      else if (!tp_strdiff (creator, "responder"))
        {
          *c = g_hash_table_lookup (priv->responder_contents, name);
        }
      else
        {
          g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
              "'creator' attribute %s",
              (creator == NULL ? "missing" : "invalid"));
          return FALSE;
        }
    }

  if (fail_if_missing && *c == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "Content '%s' (created by %s) does not exist", name, creator);
      return FALSE;
    }

  return TRUE;
}

static void
_foreach_content (GabbleJingleSession *sess,
    WockyNode *node,
    gboolean fail_if_missing,
    ContentHandlerFunc func,
    gpointer user_data,
    GError **error)
{
  GabbleJingleContent *c;
  WockyNode *content_node;
  WockyNodeIter iter;

  wocky_node_iter_init (&iter, node, "content", NULL);
  while (wocky_node_iter_next (&iter, &content_node))
    {
      if (!lookup_content (sess,
              wocky_node_get_attribute (content_node, "name"),
              wocky_node_get_attribute (content_node, "creator"),
              fail_if_missing, &c, error))
        return;

      func (sess, c, content_node, user_data, error);
      if (*error != NULL)
        return;
    }
}

struct idle_content_reject_ctx {
    GabbleJingleSession *session;
    gchar *creator;
    gchar *name;
};

static gboolean
idle_content_reject (gpointer data)
{
  WockyStanza *msg;
  WockyNode *sess_node, *node;
  struct idle_content_reject_ctx *ctx = data;

  msg = gabble_jingle_session_new_message (ctx->session,
      JINGLE_ACTION_CONTENT_REJECT, &sess_node);

  g_debug ("name = %s, intiiator = %s", ctx->name, ctx->creator);

  node = wocky_node_add_child_with_content (sess_node, "content", NULL);
  wocky_node_set_attributes (node,
      "name", ctx->name, "creator", ctx->creator, NULL);

  gabble_jingle_session_send (ctx->session, msg);

  g_object_unref (ctx->session);
  g_free (ctx->name);
  g_free (ctx->creator);
  g_free (ctx);

  return FALSE;
}

static void
fire_idle_content_reject (GabbleJingleSession *sess, const gchar *name,
    const gchar *creator)
{
  struct idle_content_reject_ctx *ctx = g_new0 (struct idle_content_reject_ctx, 1);

  if (creator == NULL)
      creator = "";

  ctx->session = g_object_ref (sess);
  ctx->name = g_strdup (name);
  ctx->creator = g_strdup (creator);

  /* FIXME: add API for ordering IQs rather than using g_idle_add. */
  g_idle_add (idle_content_reject, ctx);
}

static GabbleJingleContent *
create_content (GabbleJingleSession *sess, GType content_type,
  JingleMediaType type, JingleContentSenders senders,
  const gchar *content_ns, const gchar *transport_ns,
  const gchar *name, WockyNode *content_node, GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c;
  GHashTable *contents;

  DEBUG ("session creating new content name %s, type %d", name, type);

  /* FIXME: media-type is introduced by GabbleJingleMediaRTP, not by the
   * superclass, so this call is unsafe in the general case */
  c = g_object_new (content_type,
                    "session", sess,
                    "content-ns", content_ns,
                    "transport-ns", transport_ns,
                    "media-type", type,
                    "name", name,
                    "disposition", "session",
                    "senders", senders,
                    NULL);

  g_signal_connect (c, "ready",
      (GCallback) content_ready_cb, sess);
  g_signal_connect (c, "removed",
      (GCallback) content_removed_cb, sess);

  /* if we are called by parser, parse content add */
  if (content_node != NULL)
    {
      gabble_jingle_content_parse_add (c, content_node,
          JINGLE_IS_GOOGLE_DIALECT (priv->dialect), error);

      if (*error != NULL)
        {
          g_object_unref (c);
          return NULL;
        }

      /* gtalk streams don't have name, so use whatever Content came up with */
      if (name == NULL)
        name = gabble_jingle_content_get_name (c);
    }

  if (priv->local_initiator == gabble_jingle_content_is_created_by_us (c))
    {
      DEBUG ("inserting content %s into initiator_contents", name);
      contents = priv->initiator_contents;
    }
  else
    {
      DEBUG ("inserting content %s into responder_contents", name);
      contents = priv->responder_contents;
    }

  /* If the content already existed, either we shouldn't have picked the name
   * we did (if we're creating it) or _each_content_add should have already
   * said no.
   */
  g_assert (g_hash_table_lookup (contents, name) == NULL);
  g_hash_table_insert (contents, g_strdup (name), c);
  g_signal_emit (sess, signals[NEW_CONTENT], 0, c);
  return c;
}


static void
_each_content_add (GabbleJingleSession *sess, GabbleJingleContent *c,
    WockyNode *content_node, gpointer user_data, GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  const gchar *name = wocky_node_get_attribute (content_node, "name");
  WockyNode *desc_node = wocky_node_get_child (content_node,
      "description");
  GType content_type = 0;
  const gchar *content_ns = NULL;

  if (desc_node != NULL)
    {
      content_ns = wocky_node_get_ns (desc_node);
      DEBUG ("namespace: %s", content_ns);
      content_type = gabble_jingle_factory_lookup_content_type (
          gabble_jingle_session_get_factory (sess),
          content_ns);
    }

  if (content_type == 0)
    {
      /* if this is session-initiate, we should return error, otherwise,
       * we should respond with content-reject */
      if (priv->state < JINGLE_STATE_PENDING_INITIATED)
        g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
            "unsupported content type with ns %s", content_ns);
      else
        fire_idle_content_reject (sess, name,
            wocky_node_get_attribute (content_node, "creator"));

      return;
    }

  if (c != NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "content '%s' already exists", name);
      return;
    }

  create_content (sess, content_type, JINGLE_MEDIA_TYPE_NONE,
      JINGLE_CONTENT_SENDERS_BOTH, content_ns, NULL, NULL, content_node,
      error);
}

static void
_each_content_remove (GabbleJingleSession *sess, GabbleJingleContent *c,
    WockyNode *content_node, gpointer user_data, GError **error)
{
  g_assert (c != NULL);

  gabble_jingle_content_remove (c, FALSE);
}

static void
_each_content_rejected (GabbleJingleSession *sess, GabbleJingleContent *c,
    WockyNode *content_node, gpointer user_data, GError **error)
{
  JingleReason reason = GPOINTER_TO_UINT (user_data);
  g_assert (c != NULL);

  g_signal_emit (sess, signals[CONTENT_REJECTED], 0, c, reason, "");

  gabble_jingle_content_remove (c, FALSE);
}

static void
_each_content_modify (GabbleJingleSession *sess, GabbleJingleContent *c,
    WockyNode *content_node, gpointer user_data, GError **error)
{
  g_assert (c != NULL);

  gabble_jingle_content_update_senders (c, content_node, error);

  if (*error != NULL)
    return;
}

static void
_each_content_replace (GabbleJingleSession *sess, GabbleJingleContent *c,
    WockyNode *content_node, gpointer user_data, GError **error)
{
  _each_content_remove (sess, c, content_node, NULL, error);

  if (*error != NULL)
    return;

  _each_content_add (sess, c, content_node, NULL, error);
}

static void
_each_content_accept (GabbleJingleSession *sess, GabbleJingleContent *c,
    WockyNode *content_node, gpointer user_data, GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  JingleContentState state;

  g_assert (c != NULL);

  g_object_get (c, "state", &state, NULL);
  if (state != JINGLE_CONTENT_STATE_SENT)
    {
#ifdef ENABLE_DEBUG
      const gchar *name = wocky_node_get_attribute (content_node, "name");
      DEBUG ("ignoring content \"%s\"s acceptance for content not in SENT state", name);
#endif
      return;
    }

  gabble_jingle_content_parse_accept (c, content_node,
      JINGLE_IS_GOOGLE_DIALECT (priv->dialect), error);
}

static void
_each_description_info (GabbleJingleSession *sess, GabbleJingleContent *c,
    WockyNode *content_node, gpointer user_data, GError **error)
{
  gabble_jingle_content_parse_description_info (c, content_node, error);
}

static void
on_session_initiate (GabbleJingleSession *sess, WockyNode *node,
  GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  /* we can't call ourselves at the moment */
  if (priv->local_initiator)
    {
      /* We ignore initiate from us, and terminate the session immediately
       * afterwards */
      gabble_jingle_session_terminate (sess, JINGLE_REASON_BUSY, NULL, NULL);
      return;
    }

  if ((priv->dialect == JINGLE_DIALECT_GTALK3))
    {
      const gchar *content_ns = NULL;
      WockyNode *desc_node =
        wocky_node_get_child (node, "description");
      content_ns = wocky_node_get_ns (desc_node);

      if (!tp_strdiff (content_ns, NS_GOOGLE_SESSION_VIDEO))
        {
          GabbleJingleFactory *factory =
              gabble_jingle_session_get_factory (sess);
          GType content_type = 0;

          DEBUG ("GTalk v3 session with audio and video");

          /* audio and video content */
          content_type = gabble_jingle_factory_lookup_content_type (
            factory, content_ns);
          create_content (sess, content_type, JINGLE_MEDIA_TYPE_VIDEO,
            JINGLE_CONTENT_SENDERS_BOTH, NS_GOOGLE_SESSION_VIDEO, NULL,
              "video", node, error);

          content_type = gabble_jingle_factory_lookup_content_type (
            factory, NS_GOOGLE_SESSION_PHONE);
          create_content (sess, content_type, JINGLE_MEDIA_TYPE_AUDIO,
            JINGLE_CONTENT_SENDERS_BOTH, NS_GOOGLE_SESSION_PHONE, NULL,
              "audio", node, error);
        }
      else
        {
          _each_content_add (sess, NULL, node, NULL, error);
        }
    }
  else if (priv->dialect == JINGLE_DIALECT_GTALK4)
    {
      /* in this case we implicitly have just one content */
      _each_content_add (sess, NULL, node, NULL, error);
    }
  else
    {
      _foreach_content (sess, node, FALSE, _each_content_add, NULL, error);
    }

  if (*error == NULL)
    {
      /* FIXME: contents defined here should always have "session" content
       * disposition; resolve this as soon as the proper procedure is defined
       * in XEP-0166. */

      set_state (sess, JINGLE_STATE_PENDING_INITIATED, JINGLE_REASON_UNKNOWN,
          NULL);

      gabble_jingle_session_send_rtp_info (sess, "ringing");
    }
}

static void
on_content_add (GabbleJingleSession *sess, WockyNode *node,
  GError **error)
{
  _foreach_content (sess, node, FALSE, _each_content_add, NULL, error);
}

static void
on_content_modify (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  _foreach_content (sess, node, TRUE, _each_content_modify, NULL, error);
}

static void
on_content_remove (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  _foreach_content (sess, node, TRUE, _each_content_remove, NULL, error);
}

static void
on_content_replace (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  _foreach_content (sess, node, TRUE, _each_content_replace, NULL, error);
}

static void
on_content_reject (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  WockyNode *n = wocky_node_get_child (node, "reason");
  JingleReason reason = JINGLE_REASON_UNKNOWN;

  DEBUG (" ");

  if (n != NULL)
    extract_reason (n, &reason, NULL);

  if (reason == JINGLE_REASON_UNKNOWN)
    reason = JINGLE_REASON_GENERAL_ERROR;

  _foreach_content (sess, node, TRUE, _each_content_rejected,
      GUINT_TO_POINTER (reason), error);
}

static void
on_content_accept (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  _foreach_content (sess, node, TRUE, _each_content_accept, NULL, error);
}

static void
on_session_accept (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  DEBUG ("called");

  if ((priv->dialect == JINGLE_DIALECT_GTALK3) ||
      (priv->dialect == JINGLE_DIALECT_GTALK4))
    {
      /* Google Talk calls don't have contents per se; they just have
       * <payload-type>s in different namespaces for audio and video, in the
       * same <description> stanza. So we need to feed the whole stanza to each
       * content in turn.
       */
      GList *cs = gabble_jingle_session_get_contents (sess);
      GList *l;

      for (l = cs; l != NULL; l = l->next)
        _each_content_accept (sess, l->data, node, NULL, error);

      g_list_free (cs);
    }
  else
    {
      _foreach_content (sess, node, TRUE, _each_content_accept, NULL, error);
    }

  if (*error != NULL)
      return;

  set_state (sess, JINGLE_STATE_ACTIVE, JINGLE_REASON_UNKNOWN, NULL);

  /* Make sure each content knows the session is active */
  g_list_foreach (gabble_jingle_session_get_contents (sess),
      (GFunc) g_object_notify, "state");


  if (priv->dialect != JINGLE_DIALECT_V032)
    {
      /* If this is a dialect that doesn't support <active/>, we treat
       * session-accept as the cue to remove the ringing flag.
       */
      priv->remote_ringing = FALSE;
      g_signal_emit (sess, signals[REMOTE_STATE_CHANGED], 0);
    }
}

static void
mute_all_foreach (gpointer key,
    gpointer value,
    gpointer mute)
{
  if (G_OBJECT_TYPE (value) == GABBLE_TYPE_JINGLE_MEDIA_RTP)
    g_object_set (value, "remote-mute", GPOINTER_TO_INT (mute), NULL);
}

static void
mute_all (GabbleJingleSession *sess,
    gboolean mute)
{
  g_hash_table_foreach (sess->priv->initiator_contents, mute_all_foreach,
      GINT_TO_POINTER (mute));
  g_hash_table_foreach (sess->priv->responder_contents, mute_all_foreach,
      GINT_TO_POINTER (mute));
}

static gboolean
set_mute (GabbleJingleSession *sess,
    const gchar *name,
    const gchar *creator,
    gboolean mute,
    GError **error)
{
  GabbleJingleContent *c;

  if (name == NULL)
    {
      mute_all (sess, mute);
      return TRUE;
    }

  if (!lookup_content (sess, name, creator, TRUE /* fail if missing */, &c,
      error))
    return FALSE;

  if (G_OBJECT_TYPE (c) != GABBLE_TYPE_JINGLE_MEDIA_RTP)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "content '%s' isn't an RTP session", name);
      return FALSE;
    }

  g_object_set (c, "remote-mute", mute, NULL);
  return TRUE;
}

static void
set_hold (GabbleJingleSession *sess,
    gboolean hold)
{
  sess->priv->remote_hold = hold;
}

static void
set_ringing (GabbleJingleSession *sess,
    gboolean ringing)
{
  sess->priv->remote_ringing = ringing;
}

static gboolean
handle_payload (GabbleJingleSession *sess,
    WockyNode *payload,
    gboolean *handled,
    GError **error)
{
  const gchar *ns = wocky_node_get_ns (payload);
  const gchar *elt = payload->name;
  const gchar *name = wocky_node_get_attribute (payload, "name");
  const gchar *creator = wocky_node_get_attribute (payload, "creator");

  if (tp_strdiff (ns, NS_JINGLE_RTP_INFO))
    {
      *handled = FALSE;
      return TRUE;
    }

  *handled = TRUE;

  if (!tp_strdiff (elt, "active"))
    {
      /* Clear all states, we're active */
      mute_all (sess, FALSE);
      set_ringing (sess, FALSE);
      set_hold (sess, FALSE);
    }
  else if (!tp_strdiff (elt, "ringing"))
    {
      set_ringing (sess, TRUE);
    }
  else if (!tp_strdiff (elt, "hold"))
    {
      set_hold (sess, TRUE);
    }
  else if (!tp_strdiff (elt, "unhold"))
    {
      set_hold (sess, FALSE);
    }
  /* XEP-0178 says that only <mute/> and <unmute/> can have a name=''
   * attribute.
   */
  else if (!tp_strdiff (elt, "mute"))
    {
      return set_mute (sess, name, creator, TRUE, error);
    }
  else if (!tp_strdiff (elt, "unmute"))
    {
      return set_mute (sess, name, creator, FALSE, error);
    }
  else
    {
      g_set_error (error, WOCKY_JINGLE_ERROR,
          WOCKY_JINGLE_ERROR_UNSUPPORTED_INFO,
          "<%s> is not known in namespace %s", elt, ns);
      return FALSE;
    }

  return TRUE;
}

static void
on_session_info (GabbleJingleSession *sess,
    WockyNode *node,
    GError **error)
{
  gboolean understood_a_payload = FALSE;
  gboolean hit_an_error = FALSE;
  WockyNodeIter i;
  WockyNode *n;

  /* if this is a ping, just ack it. */
  if (wocky_node_get_first_child (node) == NULL)
    return;

  wocky_node_iter_init (&i, node, NULL, NULL);
  while (wocky_node_iter_next (&i, &n))
    {
      gboolean handled;
      GError *e = NULL;

      if (handle_payload (sess, n, &handled, &e))
        {
          understood_a_payload = understood_a_payload || handled;
        }
      else if (hit_an_error)
        {
          DEBUG ("already got another error; ignoring %s", e->message);
          g_error_free (e);
        }
      else
        {
          DEBUG ("hit an error: %s", e->message);
          hit_an_error = TRUE;
          g_propagate_error (error, e);
        }
    }

  /* If we understood something, the remote state (may have) changed. Else,
   * return an error to the peer.
   */
  if (understood_a_payload)
    g_signal_emit (sess, signals[REMOTE_STATE_CHANGED], 0);
  else if (!hit_an_error)
    g_set_error (error, WOCKY_JINGLE_ERROR, WOCKY_JINGLE_ERROR_UNSUPPORTED_INFO,
        "no recognized session-info payloads");
}

static void
on_session_terminate (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  gchar *text = NULL;
  WockyNode *n = wocky_node_get_child (node, "reason");
  JingleReason jingle_reason = JINGLE_REASON_UNKNOWN;

  if (n != NULL)
    extract_reason (n, &jingle_reason, &text);

  DEBUG ("remote end terminated the session with reason %s and text '%s'",
      gabble_jingle_session_get_reason_name (jingle_reason),
      (text != NULL ? text : "(none)"));

  set_state (sess, JINGLE_STATE_ENDED, jingle_reason, text);

  g_free (text);
}

static void
on_transport_info (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c = NULL;

  if (JINGLE_IS_GOOGLE_DIALECT (priv->dialect))
    {
      GHashTableIter iter;
      gpointer value;

      if (priv->dialect == JINGLE_DIALECT_GTALK4)
        {
          if (!tp_strdiff (wocky_node_get_attribute (node, "type"),
                "candidates"))
            {
              GList *contents = gabble_jingle_session_get_contents (sess);
              GList *l;

              DEBUG ("switching to gtalk3 dialect and retransmiting our candidates");
              priv->dialect = JINGLE_DIALECT_GTALK3;

              for (l = contents; l != NULL; l = l->next)
                gabble_jingle_content_retransmit_candidates (l->data, TRUE);

              g_list_free (contents);
            }
          else
            {
              node = wocky_node_get_child (node, "transport");

              if (node == NULL)
                {
                  g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
                      "transport-info stanza without a <transport/>");
                  return;
                }
            }
        }

        g_hash_table_iter_init (&iter, priv->initiator_contents);
        while (g_hash_table_iter_next (&iter, NULL, &value))
          {
            c = value;
            gabble_jingle_content_parse_transport_info (c, node, error);
            if (error != NULL && *error != NULL)
              break;
          }
    }
  else
    {
      WockyNodeIter i;
      WockyNode *content_node;
      GError *e = NULL;

      wocky_node_iter_init (&i, node, "content", NULL);

      while (wocky_node_iter_next (&i, &content_node))
        {
          WockyNode *transport_node;

          if (lookup_content (sess,
                wocky_node_get_attribute (content_node, "name"),
                wocky_node_get_attribute (content_node, "creator"),
                TRUE /* fail_if_missing */, &c, &e))
            {
              /* we need transport child of content node */
              transport_node = wocky_node_get_child (
                content_node, "transport");
              gabble_jingle_content_parse_transport_info (c,
                transport_node, &e);
            }

          /* Save the first error we encounter, but go through all remaining
           * contents anyway to try and recover as much info as we can */
          if (e != NULL && error != NULL && *error == NULL)
            {
              *error = e;
              e = NULL;
            }
          g_clear_error (&e);
        }
    }

}

static void
on_transport_accept (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  DEBUG ("Ignoring 'transport-accept' action from peer");
}

static void
on_description_info (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  _foreach_content (sess, node, TRUE, _each_description_info, NULL, error);
}

static void
on_info (GabbleJingleSession *sess, WockyNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c = NULL;

  DEBUG ("received info ");
  if (JINGLE_IS_GOOGLE_DIALECT (priv->dialect))
    {
      GHashTableIter iter;
      g_hash_table_iter_init (&iter, priv->initiator_contents);
      while (g_hash_table_iter_next (&iter, NULL, (gpointer) &c))
        {
          gabble_jingle_content_parse_info (c, node, error);
          if (error != NULL && *error != NULL)
            break;
        }
    }
}

static HandlerFunc handlers[] = {
  NULL, /* for unknown action */
  on_content_accept,
  on_content_add,
  on_content_modify,
  on_content_remove,
  on_content_replace,
  on_content_reject,
  on_session_accept, /* jingle_on_session_accept */
  on_session_info,
  on_session_initiate,
  on_session_terminate, /* jingle_on_session_terminate */
  on_transport_info, /* jingle_on_transport_info */
  on_transport_accept,
  on_description_info,
  on_info
};

static void
jingle_state_machine_dance (GabbleJingleSession *sess,
    JingleAction action,
    WockyNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  /* parser should've checked this already */
  g_assert (action_is_allowed (action, priv->state));
  g_assert (handlers[action] != NULL);

  handlers[action] (sess, node, error);
}

static JingleDialect
detect_google_dialect (WockyNode *session_node)
{
  /* The GTALK3 dialect is the only one that supports video at this time */
  if (wocky_node_get_child_ns (session_node,
      "description", NS_GOOGLE_SESSION_VIDEO) != NULL)
    return JINGLE_DIALECT_GTALK3;

  /* GTalk4 has a transport item, GTalk3 doesn't */
  if (wocky_node_get_child_ns (session_node,
      "transport", NS_GOOGLE_TRANSPORT_P2P) == NULL)
    return JINGLE_DIALECT_GTALK3;

  return JINGLE_DIALECT_GTALK4;
}

const gchar *
gabble_jingle_session_detect (
    WockyStanza *stanza,
    JingleAction *action,
    JingleDialect *dialect)
{
  const gchar *actxt, *sid;
  WockyNode *iq_node, *session_node;
  WockyStanzaSubType sub_type;
  gboolean google_mode = FALSE;

  /* all jingle actions are sets */
  wocky_stanza_get_type_info (stanza, NULL, &sub_type);
  if (sub_type != WOCKY_STANZA_SUB_TYPE_SET)
    return NULL;

  iq_node = wocky_stanza_get_top_node (stanza);

  if ((NULL == wocky_stanza_get_from (stanza)) ||
      (NULL == wocky_stanza_get_to (stanza)))
        return NULL;

  /* first, we try standard jingle */
  session_node = wocky_node_get_child_ns (iq_node, "jingle", NS_JINGLE032);

  if (session_node != NULL)
    {
      *dialect = JINGLE_DIALECT_V032;
    }
  else
    {
      /* then, we try a bit older jingle version */
      session_node = wocky_node_get_child_ns (iq_node, "jingle", NS_JINGLE015);

      if (session_node != NULL)
        {
          *dialect = JINGLE_DIALECT_V015;
        }
      else
        {
          /* next, we try googletalk */
          session_node = wocky_node_get_child_ns (iq_node,
              "session", NS_GOOGLE_SESSION);

          if (session_node != NULL)
            {
              *dialect = detect_google_dialect (session_node);
              google_mode = TRUE;
            }
          else
            {
              return NULL;
            }
        }
    }

  if (google_mode)
    {
      actxt = wocky_node_get_attribute (session_node, "type");
      sid = wocky_node_get_attribute (session_node, "id");
    }
  else
    {
      actxt = wocky_node_get_attribute (session_node, "action");
      sid = wocky_node_get_attribute (session_node, "sid");
    }

  *action = parse_action (actxt);

  return sid;
}

gboolean
gabble_jingle_session_parse (
    GabbleJingleSession *sess,
    JingleAction action,
    WockyStanza *stanza,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  WockyNode *iq_node, *session_node;
  const gchar *from, *action_name;

  /* IQ from/to can come in handy */
  from = wocky_stanza_get_from (stanza);
  iq_node = wocky_stanza_get_top_node (stanza);

  if (action == JINGLE_ACTION_UNKNOWN)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "unknown session action");
      return FALSE;
    }

  action_name = produce_action (action, priv->dialect);

  DEBUG ("jingle action '%s' from '%s' in session '%s' dialect %u state %u",
      action_name, from, priv->sid, priv->dialect, priv->state);

  switch (priv->dialect) {
    case JINGLE_DIALECT_V032:
      session_node = wocky_node_get_child_ns (iq_node,
          "jingle", NS_JINGLE032);
      break;
    case JINGLE_DIALECT_V015:
      session_node = wocky_node_get_child_ns (iq_node,
          "jingle", NS_JINGLE015);
      break;
    case JINGLE_DIALECT_GTALK3:
    case JINGLE_DIALECT_GTALK4:
      session_node = wocky_node_get_child_ns (iq_node,
          "session", NS_GOOGLE_SESSION);
      break;
    default:
      /* just to make gcc happy about dealing with default case */
      session_node = NULL;
  }

  if (session_node == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "malformed jingle stanza");
      return FALSE;
    }

  if (!gabble_jingle_session_defines_action (sess, action))
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "action '%s' unknown (using dialect %u)", action_name, priv->dialect);
      return FALSE;
    }

  if (!action_is_allowed (action, priv->state))
    {
      g_set_error (error, WOCKY_JINGLE_ERROR, WOCKY_JINGLE_ERROR_OUT_OF_ORDER,
          "action '%s' not allowed in current state", action_name);
      return FALSE;
    }

  jingle_state_machine_dance (sess, action, session_node, error);

  if (*error != NULL)
    return FALSE;

  return TRUE;
}

WockyStanza *
gabble_jingle_session_new_message (GabbleJingleSession *sess,
    JingleAction action, WockyNode **sess_node)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  WockyStanza *stanza;
  WockyNode *session_node;
  gchar *el = NULL, *ns = NULL;
  gboolean gtalk_mode = FALSE;

  g_return_val_if_fail (action != JINGLE_ACTION_UNKNOWN, NULL);

  g_assert ((action == JINGLE_ACTION_SESSION_INITIATE) ||
            (priv->state > JINGLE_STATE_PENDING_CREATED));
  g_assert (GABBLE_IS_JINGLE_SESSION (sess));

  switch (priv->dialect)
    {
      case JINGLE_DIALECT_V032:
        el = "jingle";
        ns = NS_JINGLE032;
        break;
      case JINGLE_DIALECT_V015:
        el = "jingle";
        ns = NS_JINGLE015;
        break;
      case JINGLE_DIALECT_GTALK3:
      case JINGLE_DIALECT_GTALK4:
        el = "session";
        ns = NS_GOOGLE_SESSION;
        gtalk_mode = TRUE;
        break;
      case JINGLE_DIALECT_ERROR:
        g_assert_not_reached ();
    }

  stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      NULL, priv->peer_jid,
      '(', el, ':', ns,
        '*', &session_node,
      ')', NULL);

  wocky_node_set_attributes (session_node,
      "initiator", priv->initiator,
      (gtalk_mode) ? "id" : "sid", priv->sid,
      (gtalk_mode) ? "type" : "action",
        produce_action (action, priv->dialect),
      NULL);

  if (sess_node != NULL)
      *sess_node = session_node;

  return stanza;
}

typedef void (*ContentMapperFunc) (GabbleJingleSession *sess,
    GabbleJingleContent *c, gpointer user_data);

static void
_map_initial_contents (GabbleJingleSession *sess, ContentMapperFunc mapper,
    gpointer user_data)
{
  GList *li;
  GList *contents = gabble_jingle_session_get_contents (sess);

  for (li = contents; li; li = li->next)
    {
      GabbleJingleContent *c = GABBLE_JINGLE_CONTENT (li->data);
      const gchar *disposition = gabble_jingle_content_get_disposition (c);

      if (!tp_strdiff (disposition, "session"))
        mapper (sess, c, user_data);
    }

  g_list_free (contents);
}

static void
_check_content_ready (GabbleJingleSession *sess,
    GabbleJingleContent *c, gpointer user_data)
{
  gboolean *ready = (gboolean *) user_data;

  if (!gabble_jingle_content_is_ready (c))
    {
      *ready = FALSE;
    }
}

static void
_transmit_candidates (GabbleJingleSession *sess,
    GabbleJingleContent *c,
    gpointer user_data)
{
  gabble_jingle_content_retransmit_candidates (c, FALSE);
}

static void
_fill_content (GabbleJingleSession *sess,
    GabbleJingleContent *c, gpointer user_data)
{
  WockyNode *sess_node = user_data;
  WockyNode *transport_node;
  JingleContentState state;

  gabble_jingle_content_produce_node (c, sess_node, TRUE, TRUE,
      &transport_node);
  gabble_jingle_content_inject_candidates (c, transport_node);

  g_object_get (c, "state", &state, NULL);

  if (state == JINGLE_CONTENT_STATE_EMPTY)
    {
      g_object_set (c, "state", JINGLE_CONTENT_STATE_SENT, NULL);
    }
  else if (state == JINGLE_CONTENT_STATE_NEW)
    {
      g_object_set (c, "state", JINGLE_CONTENT_STATE_ACKNOWLEDGED, NULL);
    }
  else
    {
      DEBUG ("content %p is in state %u", c, state);
      g_assert_not_reached ();
    }
}

/**
 * gabble_jingle_session_send:
 * @sess: a session
 * @stanza: (transfer full): a stanza, of which this function will take ownership
 *
 * A shorthand for sending a Jingle IQ without waiting for the reply.
 */
void
gabble_jingle_session_send (GabbleJingleSession *sess,
    WockyStanza *stanza)
{
  wocky_porter_send_iq_async (sess->priv->porter,
      stanza, NULL, NULL, NULL);
  g_object_unref (stanza);
}

static void
_on_initiate_reply (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (user_data);
  GabbleJingleSessionPrivate *priv = sess->priv;
  WockyStanza *reply;

  if (priv->state != JINGLE_STATE_PENDING_INITIATE_SENT)
    {
      DEBUG ("Ignoring session-initiate reply; session %p is in state %u.",
          sess, priv->state);
      g_object_unref (sess);
      return;
    }

  reply = wocky_porter_send_iq_finish (porter, result, NULL);
  if (reply != NULL &&
      !wocky_stanza_extract_errors (reply, NULL, NULL, NULL, NULL))
    {
      set_state (sess, JINGLE_STATE_PENDING_INITIATED, 0, NULL);

      if (priv->dialect != JINGLE_DIALECT_V032)
        {
          /* If this is a dialect that doesn't support <ringing/>, we treat the
           * session-initiate being acked as the cue to say we're ringing.
           */
          priv->remote_ringing = TRUE;
          g_signal_emit (sess, signals[REMOTE_STATE_CHANGED], 0);
        }
    }
  else
    {
      set_state (sess, JINGLE_STATE_ENDED, JINGLE_REASON_UNKNOWN,
          NULL);
    }

  g_clear_object (&reply);
}

static void
_on_accept_reply (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (user_data);
  GabbleJingleSessionPrivate *priv = sess->priv;
  WockyStanza *reply;

  if (priv->state != JINGLE_STATE_PENDING_ACCEPT_SENT)
    {
      DEBUG ("Ignoring session-accept reply; session %p is in state %u.",
          sess, priv->state);
      g_object_unref (sess);
      return;
    }

  reply = wocky_porter_send_iq_finish (porter, result, NULL);
  if (reply != NULL &&
      !wocky_stanza_extract_errors (reply, NULL, NULL, NULL, NULL))
    {
      set_state (sess, JINGLE_STATE_ACTIVE, 0, NULL);
      gabble_jingle_session_send_rtp_info (sess, "active");
    }
  else
    {
      set_state (sess, JINGLE_STATE_ENDED, JINGLE_REASON_UNKNOWN,
          NULL);
    }

  g_clear_object (&reply);
}

static void
try_session_initiate_or_accept (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  WockyStanza *msg;
  WockyNode *sess_node;
  gboolean contents_ready = TRUE;
  JingleAction action;
  JingleState new_state;
  GAsyncReadyCallback handler;

  DEBUG ("Trying initiate or accept");

  /* If there are no contents yet, we shouldn't have been called at all. */
  g_assert (g_hash_table_size (priv->initiator_contents) +
      g_hash_table_size (priv->responder_contents) > 0);

  if (priv->local_initiator)
    {
      if (priv->state != JINGLE_STATE_PENDING_CREATED)
        {
          DEBUG ("session is in state %u, won't try to initiate", priv->state);
          return;
        }

      if (!priv->locally_accepted)
        {
          DEBUG ("session not locally accepted yet, not initiating");
          return;
        }

      /* send directed presence (including our own caps, avatar etc.) to
       * the peer, if we aren't already visible to them */
      {
        TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
            (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
        TpHandle peer = tp_handle_ensure (contact_repo, priv->peer_jid,
            NULL, NULL);

        if (!conn_presence_visible_to (priv->conn, peer))
          conn_presence_signal_own_presence (priv->conn,
              priv->peer_jid, NULL);
      }

      action = JINGLE_ACTION_SESSION_INITIATE;
      new_state = JINGLE_STATE_PENDING_INITIATE_SENT;
      handler = _on_initiate_reply;
    }
  else
    {
      if (priv->state != JINGLE_STATE_PENDING_INITIATED)
        {
          DEBUG ("session is in state %u, won't try to accept", priv->state);
          return;
        }

      if (!priv->locally_accepted)
        {
          DEBUG ("session not locally accepted yet, not accepting");
          return;
        }

      action = JINGLE_ACTION_SESSION_ACCEPT;
      new_state = JINGLE_STATE_PENDING_ACCEPT_SENT;
      handler = _on_accept_reply;
    }

  _map_initial_contents (sess, _check_content_ready, &contents_ready);

  DEBUG ("Contents are ready: %s", contents_ready ? "yes" : "no");

  if (!contents_ready)
    {
      DEBUG ("Contents not yet ready, not initiating/accepting now..");
      return;
    }

  msg = gabble_jingle_session_new_message (sess, action, &sess_node);

  if (priv->dialect == JINGLE_DIALECT_GTALK3)
    {
      gboolean has_video = FALSE;
      gboolean has_audio = FALSE;
      GHashTableIter iter;
      gpointer value;

      g_hash_table_iter_init (&iter, priv->initiator_contents);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          JingleMediaType type;

          g_object_get (value, "media-type", &type, NULL);

          if (type == JINGLE_MEDIA_TYPE_VIDEO)
            {
              has_video = TRUE;
            }
          else if (type == JINGLE_MEDIA_TYPE_AUDIO)
            {
              has_audio = TRUE;
            }
        }

      if (has_video || has_audio)
        {
          sess_node = wocky_node_add_child_with_content (sess_node, "description",
              NULL);

          sess_node->ns = g_quark_from_static_string (
              has_video ? NS_GOOGLE_SESSION_VIDEO : NS_GOOGLE_SESSION_PHONE);
        }
    }


  _map_initial_contents (sess, _fill_content, sess_node);
  wocky_porter_send_iq_async (priv->porter,
      msg, NULL, handler, g_object_ref (sess));
  g_object_unref (msg);
  set_state (sess, new_state, 0, NULL);

  /* now all initial contents can transmit their candidates */
  _map_initial_contents (sess, _transmit_candidates, NULL);
}

/**
 * set_state:
 * @sess: a jingle session
 * @state: the new state for the session
 * @termination_reason: if @state is JINGLE_STATE_ENDED, the reason the session
 *                      ended. Otherwise, must be JINGLE_REASON_UNKNOWN.
 * @text: if @state is JINGLE_STATE_ENDED, the human-readable reason the session
 *        ended.
 */
static void
set_state (GabbleJingleSession *sess,
           JingleState state,
           JingleReason termination_reason,
           const gchar *text)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  if (state <= priv->state)
    {
      DEBUG ("ignoring request to set state from %u back to %u", priv->state, state);
      return;
    }

  if (state != JINGLE_STATE_ENDED)
    g_assert (termination_reason == JINGLE_REASON_UNKNOWN);

  DEBUG ("Setting state of JingleSession: %p (priv = %p) from %u to %u", sess, priv, priv->state, state);

  priv->state = state;
  g_object_notify (G_OBJECT (sess), "state");

  /* If we have an outstanding "you're on hold notification", send it */
  if (priv->local_hold &&
      state >= JINGLE_STATE_PENDING_INITIATED &&
      state < JINGLE_STATE_ENDED)
    gabble_jingle_session_send_held (sess);

  if (state == JINGLE_STATE_ENDED)
    g_signal_emit (sess, signals[TERMINATED], 0, priv->locally_terminated,
        termination_reason, text);
}

void
gabble_jingle_session_accept (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  priv->locally_accepted = TRUE;

  try_session_initiate_or_accept (sess);
}

const gchar *
gabble_jingle_session_get_reason_name (JingleReason reason)
{
  GEnumClass *klass = g_type_class_ref (jingle_reason_get_type ());
  GEnumValue *enum_value = g_enum_get_value (klass, (gint) reason);

  g_return_val_if_fail (enum_value != NULL, NULL);

  return enum_value->value_nick;
}

gboolean
gabble_jingle_session_terminate (GabbleJingleSession *sess,
                                 JingleReason reason,
                                 const gchar *text,
                                 GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  const gchar *reason_elt;

  if (priv->state == JINGLE_STATE_ENDED)
    {
      DEBUG ("session already terminated, ignoring terminate request");
      return TRUE;
    }

  if (reason == JINGLE_REASON_UNKNOWN)
    reason = (priv->state == JINGLE_STATE_ACTIVE) ?
      JINGLE_REASON_SUCCESS : JINGLE_REASON_CANCEL;

  reason_elt = gabble_jingle_session_get_reason_name (reason);

  if (priv->state != JINGLE_STATE_PENDING_CREATED)
    {
      WockyNode *session_node;
      WockyStanza *msg = gabble_jingle_session_new_message (sess,
          JINGLE_ACTION_SESSION_TERMINATE, &session_node);

      if (priv->dialect == JINGLE_DIALECT_V032 && reason_elt != NULL)
        {
          WockyNode *r = wocky_node_add_child_with_content (session_node, "reason",
              NULL);

          wocky_node_add_child_with_content (r, reason_elt, NULL);

          if (!tp_str_empty (text))
            wocky_node_add_child_with_content (r, "text", text);
        }

      gabble_jingle_session_send (sess, msg);
    }

  /* NOTE: on "terminated", jingle factory and media channel will unref
   * it, bringing refcount to 0, so dispose will be called, and it
   * takes care of cleanup */

  DEBUG ("we are terminating this session");
  priv->locally_terminated = TRUE;
  set_state (sess, JINGLE_STATE_ENDED, reason, text);

  return TRUE;
}

static void
_foreach_count_active_contents (gpointer key, gpointer value, gpointer user_data)
{
  GabbleJingleContent *c = value;
  guint *n_contents = user_data;
  JingleContentState state;

  g_object_get (c, "state", &state, NULL);
  if ((state >= JINGLE_CONTENT_STATE_NEW) &&
      (state < JINGLE_CONTENT_STATE_REMOVING))
    {
      *n_contents = *n_contents + 1;
    }
}

static gboolean
count_active_contents (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  guint n_contents = 0;

  g_hash_table_foreach (priv->initiator_contents, _foreach_count_active_contents,
      &n_contents);
  g_hash_table_foreach (priv->responder_contents, _foreach_count_active_contents,
      &n_contents);

  return n_contents;
}

static void
content_removed_cb (GabbleJingleContent *c, gpointer user_data)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (user_data);
  GabbleJingleSessionPrivate *priv = sess->priv;
  const gchar *name = gabble_jingle_content_get_name (c);

  if (gabble_jingle_content_creator_is_initiator (c))
    g_hash_table_remove (priv->initiator_contents, name);
  else
    g_hash_table_remove (priv->responder_contents, name);

  if (priv->state == JINGLE_STATE_ENDED)
    return;

  if (count_active_contents (sess) == 0)
    {
      gabble_jingle_session_terminate (sess,
          JINGLE_REASON_UNKNOWN, NULL, NULL);
    }
  else
    {
      /* It's possible the content now removed was
       * blocking us from creating or accepting the
       * session, so we might as well try now. */
      try_session_initiate_or_accept (sess);
    }
}


void
gabble_jingle_session_remove_content (GabbleJingleSession *sess,
    GabbleJingleContent *c)
{
  if (count_active_contents (sess) > 1)
    {
      gabble_jingle_content_remove (c, TRUE);
    }
  else
    {
      /* session will be terminated when the content gets marked as removed */
      DEBUG ("called for last active content, doing session-terminate instead");
      gabble_jingle_content_remove (c, FALSE);
    }
}

GabbleJingleContent *
gabble_jingle_session_add_content (GabbleJingleSession *sess,
    JingleMediaType mtype,
    JingleContentSenders senders,
    const gchar *name,
    const gchar *content_ns,
    const gchar *transport_ns)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c;
  GType content_type;
  GHashTable *contents = priv->local_initiator ? priv->initiator_contents
      : priv->responder_contents;
  guint id = g_hash_table_size (contents) + 1;
  gchar *cname = NULL;

  if (name == NULL || *name == '\0')
    name = (mtype == JINGLE_MEDIA_TYPE_AUDIO ?  "Audio" : "Video");

  cname = g_strdup (name);

  while (g_hash_table_lookup (priv->initiator_contents, cname) != NULL
      || g_hash_table_lookup (priv->responder_contents, cname) != NULL)
    {
      g_free (cname);
      cname = g_strdup_printf ("%s_%d", name, id++);
    }

  content_type = gabble_jingle_factory_lookup_content_type (
      gabble_jingle_session_get_factory (sess),
      content_ns);

  g_assert (content_type != 0);

  c = create_content (sess, content_type, mtype, senders,
      content_ns, transport_ns, cname, NULL, NULL);

  /* The new content better have ended up in the set we thought it would... */
  g_assert (g_hash_table_lookup (contents, cname) != NULL);

  g_free (cname);

  return c;
}

/* Get any content. Either we're in google mode (so we only have one content
 * anyways), or we just need any content type to figure out what use case
 * we're in (media, ft, etc). */
static GabbleJingleContent *
_get_any_content (GabbleJingleSession *session)
{
  GabbleJingleContent *c;

  GList *li = gabble_jingle_session_get_contents (session);

  if (li == NULL)
      return NULL;

  c = li->data;
  g_list_free (li);

  return c;
}

/* Note: if there are multiple content types, not guaranteed which one will
 * be returned. Typically, the same GType will know how to handle related
 * contents found in a session (e.g. media-rtp for audio/video), so that
 * should not be a problem. Returns 0 if there are no contents yet. */
GType
gabble_jingle_session_get_content_type (GabbleJingleSession *sess)
{
  GabbleJingleContent *c = _get_any_content (sess);

  if (c == NULL)
      return 0;

  return G_OBJECT_TYPE (c);
}

/* FIXME: probably should make this into a property */
GList *
gabble_jingle_session_get_contents (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  return g_list_concat (g_hash_table_get_values (priv->initiator_contents),
      g_hash_table_get_values (priv->responder_contents));
}

const gchar *
gabble_jingle_session_get_peer_resource (GabbleJingleSession *sess)
{
  return sess->priv->peer_resource;
}

const gchar *
gabble_jingle_session_get_initiator (GabbleJingleSession *sess)
{
  return sess->priv->initiator;
}

const gchar *
gabble_jingle_session_get_sid (GabbleJingleSession *sess)
{
  return sess->priv->sid;
}

static void
content_ready_cb (GabbleJingleContent *c, gpointer user_data)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (user_data);
  const gchar *disposition;

  DEBUG ("called");

  disposition = gabble_jingle_content_get_disposition (c);
  /* This assertion is actually safe, because 'ready' is only emitted by
   * contents with disposition "session". But this is crazy.
   */
  g_assert (!tp_strdiff (disposition, "session"));

  try_session_initiate_or_accept (sess);
}

static void
gabble_jingle_session_send_rtp_info (GabbleJingleSession *sess,
    const gchar *name)
{
  WockyStanza *message;
  WockyNode *jingle, *notification;

  if (!gabble_jingle_session_defines_action (sess, JINGLE_ACTION_SESSION_INFO))
    {
      DEBUG ("Not sending <%s/>; not using modern Jingle", name);
      return;
    }

  message = gabble_jingle_session_new_message (sess,
      JINGLE_ACTION_SESSION_INFO, &jingle);

  notification = wocky_node_add_child_with_content (jingle, name, NULL);
  notification->ns = g_quark_from_static_string (NS_JINGLE_RTP_INFO);

  /* This is just informational, so ignoring the reply. */
  gabble_jingle_session_send (sess, message);
}

static void
gabble_jingle_session_send_held (GabbleJingleSession *sess)
{
  const gchar *s = (sess->priv->local_hold ? "hold" : "unhold");

  gabble_jingle_session_send_rtp_info (sess, s);
}

void
gabble_jingle_session_set_local_hold (GabbleJingleSession *sess,
    gboolean held)
{
  g_object_set (sess, "local-hold", held, NULL);
}

gboolean
gabble_jingle_session_get_remote_hold (GabbleJingleSession *sess)
{
  g_assert (GABBLE_IS_JINGLE_SESSION (sess));

  return sess->priv->remote_hold;
}

gboolean
gabble_jingle_session_get_remote_ringing (GabbleJingleSession *sess)
{
  g_assert (GABBLE_IS_JINGLE_SESSION (sess));

  return sess->priv->remote_ringing;
}

gboolean
gabble_jingle_session_can_modify_contents (GabbleJingleSession *sess)
{
  return !JINGLE_IS_GOOGLE_DIALECT (sess->priv->dialect) &&
      !gabble_jingle_session_peer_has_cap (sess, QUIRK_GOOGLE_WEBMAIL_CLIENT);
}

JingleDialect
gabble_jingle_session_get_dialect (GabbleJingleSession *sess)
{
  return sess->priv->dialect;
}

WockyContact *
gabble_jingle_session_get_peer_contact (GabbleJingleSession *self)
{
  return self->priv->peer_contact;
}

/*
 * gabble_jingle_session_get_peer_jid:
 * @sess: a jingle session
 *
 * Returns: the full JID of the remote contact.
 */
const gchar *
gabble_jingle_session_get_peer_jid (GabbleJingleSession *sess)
{
  return sess->priv->peer_jid;
}

GabbleJingleFactory *
gabble_jingle_session_get_factory (GabbleJingleSession *self)
{
  return self->priv->jingle_factory;
}

WockyPorter *
gabble_jingle_session_get_porter (GabbleJingleSession *self)
{
  return self->priv->porter;
}
