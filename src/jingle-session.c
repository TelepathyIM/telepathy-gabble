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

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "gabble-signals-marshal.h"
#include "jingle-content.h"
#include "jingle-factory.h"
#include "namespaces.h"
#include "util.h"

G_DEFINE_TYPE(GabbleJingleSession, gabble_jingle_session, G_TYPE_OBJECT);

/* signal enum */
enum
{
  NEW_CONTENT,
  TERMINATED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_SESSION_ID,
  PROP_PEER,
  PROP_PEER_RESOURCE,
  PROP_LOCAL_INITIATOR,
  PROP_STATE,
  PROP_DIALECT,
  LAST_PROPERTY
};

struct _GabbleJingleSessionPrivate
{
  GabbleConnection *conn;

  gchar *peer_resource;
  gchar *peer_jid;
  gchar *initiator;
  gboolean local_initiator;

  /* GabbleJingleContent objects keyed by content name.
   * Table owns references to these objects. */
  GHashTable *contents;

  JingleDialect dialect;
  JingleState state;
  gchar *sid;

  gboolean locally_accepted;
  gboolean locally_terminated;

  guint timer_id;

  gboolean dispose_has_run;
};

static guint session_timeout_time = 60000;

typedef struct {
  JingleState state;
  JingleAction *actions;
} JingleStateActions;

/* gcc should be able to figure this out from the table below, but.. */
#define MAX_ACTIONS_PER_STATE 11

/* NB: JINGLE_ACTION_UNKNOWN is used as a terminator here. */
static JingleAction allowed_actions[MAX_JINGLE_STATES][MAX_ACTIONS_PER_STATE] = {
  /* JS_STATE_PENDING_CREATED */
  { JINGLE_ACTION_SESSION_INITIATE, JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_PENDING_INITIATE_SENT */
  { JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_SESSION_ACCEPT,
    JINGLE_ACTION_TRANSPORT_ACCEPT, /* required for GTalk4 */
    JINGLE_ACTION_DESCRIPTION_INFO, JINGLE_ACTION_SESSION_INFO,
    JINGLE_ACTION_TRANSPORT_INFO, JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_PENDING_INITIATED */
  { JINGLE_ACTION_SESSION_ACCEPT, JINGLE_ACTION_SESSION_TERMINATE,
    JINGLE_ACTION_TRANSPORT_INFO, JINGLE_ACTION_CONTENT_REJECT,
    JINGLE_ACTION_CONTENT_MODIFY, JINGLE_ACTION_CONTENT_ACCEPT,
    JINGLE_ACTION_CONTENT_REMOVE,  JINGLE_ACTION_DESCRIPTION_INFO,
    JINGLE_ACTION_TRANSPORT_ACCEPT, JINGLE_ACTION_SESSION_INFO,
    JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_PENDING_ACCEPT_SENT */
  { JINGLE_ACTION_TRANSPORT_INFO, JINGLE_ACTION_DESCRIPTION_INFO,
    JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_SESSION_INFO,
    JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_ACTIVE */
  { JINGLE_ACTION_CONTENT_MODIFY, JINGLE_ACTION_CONTENT_ADD,
    JINGLE_ACTION_CONTENT_REMOVE, JINGLE_ACTION_CONTENT_REPLACE,
    JINGLE_ACTION_CONTENT_ACCEPT, JINGLE_ACTION_CONTENT_REJECT,
    JINGLE_ACTION_SESSION_INFO, JINGLE_ACTION_TRANSPORT_INFO,
    JINGLE_ACTION_DESCRIPTION_INFO,
    JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_ENDED */
  { JINGLE_ACTION_UNKNOWN }
};

static void
gabble_jingle_session_init (GabbleJingleSession *obj)
{
  GabbleJingleSessionPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_SESSION,
         GabbleJingleSessionPrivate);
  obj->priv = priv;

  priv->contents = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  priv->state = JS_STATE_PENDING_CREATED;
  priv->locally_accepted = FALSE;
  priv->locally_terminated = FALSE;
  priv->timer_id = 0;
  priv->dispose_has_run = FALSE;
}

static void
gabble_jingle_session_dispose (GObject *object)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (object);
  GabbleJingleSessionPrivate *priv = sess->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  DEBUG ("called");
  priv->dispose_has_run = TRUE;

  g_assert ((priv->state == JS_STATE_PENDING_CREATED) ||
      (priv->state == JS_STATE_ENDED));
  g_assert (priv->timer_id == 0);

  g_hash_table_destroy (priv->contents);
  priv->contents = NULL;

  if (sess->peer)
    {
      tp_handle_unref (contact_repo, sess->peer);
      sess->peer = 0;
    }

  g_free (priv->sid);
  priv->sid = NULL;

  g_free (priv->peer_resource);
  priv->peer_resource = NULL;

  g_free (priv->peer_jid);
  priv->peer_jid = NULL;

  g_free (priv->initiator);
  priv->initiator = NULL;

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
    case PROP_SESSION_ID:
      g_value_set_string (value, priv->sid);
      break;
    case PROP_LOCAL_INITIATOR:
      g_value_set_boolean (value, priv->local_initiator);
      break;
    case PROP_PEER:
      g_value_set_uint (value, sess->peer);
      break;
    case PROP_PEER_RESOURCE:
      g_value_set_string (value, priv->peer_resource);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    case PROP_DIALECT:
      g_value_set_uint (value, priv->dialect);
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
    case PROP_PEER:
      sess->peer = g_value_get_uint (value);
      if (sess->peer != 0)
        {
          TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

          tp_handle_ref (contact_repo, sess->peer);
        }
      break;
    case PROP_PEER_RESOURCE:
      g_free (priv->peer_resource);
      priv->peer_resource = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      g_assert_not_reached ();
      break;
  }
}

GabbleJingleSession *
gabble_jingle_session_new (GabbleConnection *connection,
                           const gchar *session_id,
                           gboolean local_initiator,
                           TpHandle peer,
                           const gchar *peer_resource)
{
  return g_object_new (GABBLE_TYPE_JINGLE_SESSION,
      "session-id", session_id,
      "connection", connection,
      "local-initiator", local_initiator,
      "peer", peer,
      "peer-resource", peer_resource,
      NULL);
}

static void
gabble_jingle_session_class_init (GabbleJingleSessionClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleSessionPrivate));

  object_class->get_property = gabble_jingle_session_get_property;
  object_class->set_property = gabble_jingle_session_set_property;
  object_class->dispose = gabble_jingle_session_dispose;

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

  param_spec = g_param_spec_string ("session-id", "Session ID",
                                    "A unique session identifier used "
                                    "throughout all communication.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION_ID, param_spec);

  param_spec = g_param_spec_boolean ("local-initiator", "Session initiator",
                                     "Specifies if local end initiated the session.",
                                     TRUE,
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_LOCAL_INITIATOR, param_spec);

  param_spec = g_param_spec_uint ("peer", "Session peer",
                                  "The TpHandle representing the contact "
                                  "with whom this session communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_string ("peer-resource",
                                    "Session peer's resource",
                                    "The resource of the contact "
                                    "with whom this session communicates, "
                                    "if applicable",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
                                   param_spec);

  param_spec = g_param_spec_uint ("state", "Session state",
                                  "The current state that the session is in.",
                                  0, G_MAXUINT32, JS_STATE_PENDING_CREATED,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_uint ("dialect", "Jingle dialect",
                                  "Jingle dialect used for this session.",
                                  0, G_MAXUINT32, JINGLE_DIALECT_ERROR,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DIALECT, param_spec);


  /* signal definitions */

  signals[NEW_CONTENT] = g_signal_new ("new-content",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[TERMINATED] = g_signal_new ("terminated",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, gabble_marshal_VOID__BOOLEAN_UINT,
        G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_UINT);
}

typedef void (*HandlerFunc)(GabbleJingleSession *sess,
  LmMessageNode *node, GError **error);
typedef void (*ContentHandlerFunc)(GabbleJingleSession *sess,
  GabbleJingleContent *c, LmMessageNode *content_node, GError **error);

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

  return JINGLE_ACTION_UNKNOWN;
}

static const gchar *
produce_action (JingleAction action, JingleDialect dialect)
{
  gboolean gmode = (dialect == JINGLE_DIALECT_GTALK3) ||
      (dialect == JINGLE_DIALECT_GTALK4);

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
    default:
      DEBUG ("unknown action %u", action);
      g_assert_not_reached ();
  }

  /* to make gcc not complain */
  return NULL;
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

static void set_state (GabbleJingleSession *sess, JingleState state,
    TpChannelGroupChangeReason termination_reason);
static GabbleJingleContent *_get_any_content (GabbleJingleSession *session);

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

static void
_foreach_content (GabbleJingleSession *sess, LmMessageNode *node,
  ContentHandlerFunc func, GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c;
  LmMessageNode *content_node;
  const gchar *name;

  for (content_node = node->children;
       NULL != content_node;
       content_node = content_node->next)
    {
      if (tp_strdiff (lm_message_node_get_name (content_node), "content"))
        continue;

      name = lm_message_node_get_attribute (content_node, "name");
      c = g_hash_table_lookup (priv->contents, name);

      func (sess, c, content_node, error);
      if (*error != NULL)
        return;
    }
}

static void content_ready_cb (GabbleJingleContent *c, gpointer user_data);
static void content_removed_cb (GabbleJingleContent *c, gpointer user_data);

struct idle_content_reject_ctx {
    GabbleJingleSession *session;
    gchar *creator;
    gchar *name;
};

static gboolean
idle_content_reject (gpointer data)
{
  LmMessage *msg;
  LmMessageNode *sess_node, *node;
  struct idle_content_reject_ctx *ctx = data;

  msg = gabble_jingle_session_new_message (ctx->session,
      JINGLE_ACTION_CONTENT_REJECT, &sess_node);

  g_debug ("name = %s, intiiator = %s", ctx->name, ctx->creator);

  node = lm_message_node_add_child (sess_node, "content", NULL);
  lm_message_node_set_attributes (node,
      "name", ctx->name, "creator", ctx->creator, NULL);

  gabble_jingle_session_send (ctx->session, msg, NULL, NULL);

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
  JingleMediaType type, const gchar *content_ns, const gchar *transport_ns,
  const gchar *name, LmMessageNode *content_node, GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c;

  DEBUG ("session creating new content type, conn == %p, jf == %p", priv->conn, priv->conn->jingle_factory);

  /* FIXME: media-type is introduced by GabbleJingleMediaRTP, not by the
   * superclass, so this call is unsafe in the general case */
  c = g_object_new (content_type,
                    "connection", priv->conn,
                    "session", sess,
                    "content-ns", content_ns,
                    "transport-ns", transport_ns,
                    "media-type", type,
                    "name", name,
                    "disposition", "session",
                    "senders", JINGLE_CONTENT_SENDERS_BOTH,
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
          g_object_get (c, "name", &name, NULL);
    }

  /* This will override existing content if it exists. */
  g_hash_table_replace (priv->contents, g_strdup (name), c);
  g_signal_emit (sess, signals[NEW_CONTENT], 0, c);
  return c;
}


static void
_each_content_add (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  const gchar *name = lm_message_node_get_attribute (content_node, "name");
  LmMessageNode *desc_node = lm_message_node_get_child_any_ns (content_node,
      "description");
  GType content_type = 0;
  const gchar *content_ns = NULL;

  if (desc_node != NULL)
    {
      content_ns = lm_message_node_get_namespace (desc_node);
      DEBUG ("namespace: %s", content_ns);
      content_type = gabble_jingle_factory_lookup_content_type (
          priv->conn->jingle_factory, content_ns);
    }

  if (content_type == 0)
    {
      DEBUG ("unsupported content type with ns %s", content_ns);

      /* if this is session-initiate, we should return error, otherwise,
       * we should respond with content-reject */
      if (priv->state < JS_STATE_PENDING_INITIATED)
        {
          SET_BAD_REQ ("unsupported content type");
        }
      else
        {
          fire_idle_content_reject (sess, name,
              lm_message_node_get_attribute (content_node, "creator"));
        }
      return;
    }

  if (c != NULL)
    {
      JingleContentState state;

      /* contents added by the session initiator may replace similarly-named
       * contents which we are trying to add (but haven't had acknowledged) */

      g_object_get (c, "state", &state, NULL);
      if (state < JINGLE_CONTENT_STATE_ACKNOWLEDGED)
        {
          if (priv->local_initiator)
            {
              SET_CONFLICT ("session initiator is creating a content "
                  "named \"%s\" already", name);
              return;
            }
        }
      else
        {
          SET_CONFLICT ("content called \"%s\" already exists, rejecting", name);
          return;
        }
    }

  create_content (sess, content_type, JINGLE_MEDIA_TYPE_NONE,
      content_ns, NULL, NULL, content_node, error);
}

static void
_each_content_remove (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  const gchar *name = lm_message_node_get_attribute (content_node, "name");

  if (c == NULL)
    {
      SET_BAD_REQ ("content called \"%s\" doesn't exist", name);
      return;
    }
  gabble_jingle_content_remove (c, FALSE);
}

static void
_each_content_modify (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  if (c == NULL)
    {
      const gchar *name = lm_message_node_get_attribute (content_node, "name");
      SET_BAD_REQ ("content called \"%s\" doesn't exist", name);
      return;
    }

  gabble_jingle_content_update_senders (c, content_node, error);

  if (*error != NULL)
    return;
}

static void
_each_content_replace (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  _each_content_remove (sess, c, content_node, error);

  if (*error != NULL)
    return;

  _each_content_add (sess, c, content_node, error);
}

static void
_each_content_accept (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node ,GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  JingleContentState state;

  if (c == NULL)
    {
      const gchar *name = lm_message_node_get_attribute (content_node, "name");
      SET_BAD_REQ ("content called \"%s\" doesn't exist", name);
      return;
    }

  g_object_get (c, "state", &state, NULL);
  if (state != JINGLE_CONTENT_STATE_SENT)
    {
#ifdef ENABLE_DEBUG
      const gchar *name = lm_message_node_get_attribute (content_node, "name");
      DEBUG ("ignoring content \"%s\"s acceptance for content not in SENT state", name);
#endif
      return;
    }

  gabble_jingle_content_parse_accept (c, content_node,
      JINGLE_IS_GOOGLE_DIALECT (priv->dialect), error);
}

static void
_each_description_info (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  if (c == NULL)
    {
      const gchar *name = lm_message_node_get_attribute (content_node, "name");
      SET_BAD_REQ ("content called \"%s\" doesn't exist", name);
      return;
    }

  gabble_jingle_content_parse_description_info (c, content_node, error);
}

static void
on_session_initiate (GabbleJingleSession *sess, LmMessageNode *node,
  GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  /* we can't call ourselves at the moment */
  if (priv->local_initiator)
    {
      /* We ignore initiate from us, and terminate the session immediately
       * afterwards */
      gabble_jingle_session_terminate (sess,
          TP_CHANNEL_GROUP_CHANGE_REASON_BUSY, NULL);
      return;
    }

  if ((priv->dialect == JINGLE_DIALECT_GTALK3) ||
      (priv->dialect == JINGLE_DIALECT_GTALK4))
    {
      /* in this case we implicitly have just one content */
      _each_content_add (sess, NULL, node, error);
    }
  else
    {
      _foreach_content (sess, node, _each_content_add, error);
    }

  if (*error == NULL)
    {
      /* FIXME: contents defined here should always have "session" content
       * disposition; resolve this as soon as the proper procedure is defined
       * in XEP-0166. */

      set_state (sess, JS_STATE_PENDING_INITIATED, 0);
    }
}

static void
on_content_add (GabbleJingleSession *sess, LmMessageNode *node,
  GError **error)
{
  _foreach_content (sess, node, _each_content_add, error);
}

static void
on_content_modify (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  _foreach_content (sess, node, _each_content_modify, error);
}

static void
on_content_remove (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  _foreach_content (sess, node, _each_content_remove, error);
}

static void
on_content_replace (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  _foreach_content (sess, node, _each_content_replace, error);
}

static void
on_content_reject (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  /* FIXME: reject is different from remove - remove is for
   * acknowledged contents, reject is for pending; but the result
   * is the same. */
  _foreach_content (sess, node, _each_content_remove, error);
}

static void
on_content_accept (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  _foreach_content (sess, node, _each_content_accept, error);
}

static void
on_session_accept (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  DEBUG ("called");

  /* handle single-content special case */
  if ((priv->dialect == JINGLE_DIALECT_GTALK3) ||
      (priv->dialect == JINGLE_DIALECT_GTALK4))
    {
      GabbleJingleContent *c = _get_any_content (sess);
      _each_content_accept (sess, c, node, error);
    }
  else
    {
      _foreach_content (sess, node, _each_content_accept, error);
    }

  if (*error != NULL)
      return;

  set_state (sess, JS_STATE_ACTIVE, 0);
}

static void
on_session_info (GabbleJingleSession *sess,
    LmMessageNode *node,
    GError **error)
{
  GList *contents, *l;
  gboolean understood_a_payload = FALSE;
  gboolean hit_an_error = FALSE;
  LmMessageNode *n = node->children;

  /* if this is a ping, just ack it. */
  if (n == NULL)
    return;

  contents = g_hash_table_get_values (sess->priv->contents);

  for (n = node->children; n != NULL; n = n->next)
    {
      for (l = contents; l != NULL; l = g_list_next (l))
        {
          gboolean handled;
          GError *e = NULL;

          if (gabble_jingle_content_handle_info (l->data, n, &handled, &e))
            {
              understood_a_payload = understood_a_payload || handled;
            }
          else
            {
              if (hit_an_error)
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
        }
    }

  /* If we didn't understand any of the payloads, tell the other end.
   */
  if (!understood_a_payload)
    g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_UNSUPPORTED_INFO,
        "no recognized session-info payloads");
}

typedef struct {
    const gchar *element;
    TpChannelGroupChangeReason reason;
} ReasonMapping;

/* Taken from the schema in XEP 0166 */
ReasonMapping reasons[] = {
    { "alternative-session", TP_CHANNEL_GROUP_CHANGE_REASON_NONE },
    { "busy", TP_CHANNEL_GROUP_CHANGE_REASON_BUSY },
    { "cancel", TP_CHANNEL_GROUP_CHANGE_REASON_NONE },
    { "connectivity-error", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "decline", TP_CHANNEL_GROUP_CHANGE_REASON_NONE },
    { "expired", TP_CHANNEL_GROUP_CHANGE_REASON_NONE },
    { "failed-application", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "failed-transport", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "general-error", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "gone", TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE },
    { "incompatible-parameters", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "media-error", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "security-error", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "success", TP_CHANNEL_GROUP_CHANGE_REASON_NONE },
    { "timeout", TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER },
    { "unsupported-applications", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { "unsupported-transports", TP_CHANNEL_GROUP_CHANGE_REASON_ERROR },
    { NULL, }
};

static void
on_session_terminate (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  TpChannelGroupChangeReason reason = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;
  LmMessageNode *n = lm_message_node_get_child (node, "reason");
  ReasonMapping *m = NULL;

  /* If the session-terminate stanza has a <reason> child, then iterate across
   * its children, looking for a child whose name we recognise as a
   * machine-readable reason for the call ending (looked up from the table
   * above). (The XEP also defines <text> for a human-readable message. TODO:
   * expose and send that as the group change message.) Once we find a reason
   * we recognise, break out of both loops.
   */
  if (n != NULL)
    for (n = n->children; n != NULL; n = n->next)
      for (m = reasons; m->element != NULL; m++)
        if (!tp_strdiff (m->element, lm_message_node_get_name (n)))
          {
            reason = m->reason;
            goto pub;
          }

pub:
  DEBUG ("remote end terminated the session with reason %s (%u)",
      (m != NULL && m->element != NULL ? m->element : "(none)"), reason);
  set_state (sess, JS_STATE_ENDED, reason);
}

static void
on_transport_info (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c = NULL;

  if (JINGLE_IS_GOOGLE_DIALECT (priv->dialect))
    {
      /* We are certain that GTalk has only one content. It's not possible
       * for session to have more than one content if in gtalk mode (if
       * it happens, it's a bug in our code). */
      GList *cs = g_hash_table_get_values (priv->contents);
      g_assert (g_list_length (cs) == 1);

      c = cs->data;

      g_list_free (cs);

      if (priv->dialect == JINGLE_DIALECT_GTALK4)
        {
          if (!tp_strdiff (lm_message_node_get_attribute (node, "type"),
                "candidates"))
            {
              DEBUG ("switching to gtalk3 dialect and retransmiting our candidates");
              priv->dialect = JINGLE_DIALECT_GTALK3;
              gabble_jingle_content_retransmit_candidates (c);
            }
          else
            {
              node = lm_message_node_get_child_any_ns (node, "transport");

              if (node == NULL)
                {
                  SET_BAD_REQ ("illegal transport-info stanza");
                  return;
                }
            }
        }
    }
  else
    {
      const gchar *name;

      node = lm_message_node_get_child_any_ns (node, "content");
      name = lm_message_node_get_attribute (node, "name");
      c = g_hash_table_lookup (priv->contents, name);

      if (c == NULL)
        {
          SET_BAD_REQ ("content doesn't exist");
          return;
        }

      /* we need transport child of content node */
      node = lm_message_node_get_child_any_ns (node, "transport");
    }

  gabble_jingle_content_parse_transport_info (c, node, error);
}

static void
on_transport_accept (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  DEBUG ("Ignoring 'transport-accept' action from peer");
}

static void
on_description_info (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  _foreach_content (sess, node, _each_description_info, error);
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
  on_description_info
};

static void
jingle_state_machine_dance (GabbleJingleSession *sess,
    JingleAction action,
    LmMessageNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  /* parser should've checked this already */
  g_assert (action_is_allowed (action, priv->state));
  g_assert (handlers[action] != NULL);

  handlers[action] (sess, node, error);
}

const gchar *
gabble_jingle_session_detect (LmMessage *message, JingleAction *action, JingleDialect *dialect)
{
  const gchar *actxt, *sid;
  LmMessageNode *iq_node, *session_node;
  gboolean google_mode = FALSE;

  /* all jingle actions are sets */
  if (LM_MESSAGE_SUB_TYPE_SET != lm_message_get_sub_type (message))
    return NULL;

  iq_node = lm_message_get_node (message);

  if ((NULL == lm_message_node_get_attribute (iq_node, "from")) ||
      (NULL == lm_message_node_get_attribute (iq_node, "to")))
        return NULL;

  /* first, we try standard jingle */
  session_node = lm_message_node_get_child_with_namespace (iq_node,
      "jingle", NS_JINGLE032);

  if (session_node != NULL)
    {
      *dialect = JINGLE_DIALECT_V032;
    }
  else
    {
      /* then, we try a bit older jingle version */
      session_node = lm_message_node_get_child_with_namespace (iq_node,
          "jingle", NS_JINGLE015);

      if (session_node != NULL)
        {
          *dialect = JINGLE_DIALECT_V015;
        }
      else
        {
          /* next, we try googletalk */
          session_node = lm_message_node_get_child_with_namespace (iq_node,
              "session", NS_GOOGLE_SESSION);

          /* we can't distinguish between libjingle 0.3 and libjingle0.4 at this
           * point, assume the better case */
          if (session_node != NULL)
            {
              *dialect = JINGLE_DIALECT_GTALK4;
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
      actxt = lm_message_node_get_attribute (session_node, "type");
      sid = lm_message_node_get_attribute (session_node, "id");
    }
  else
    {
      actxt = lm_message_node_get_attribute (session_node, "action");
      sid = lm_message_node_get_attribute (session_node, "sid");
    }

  *action = parse_action (actxt);

  return sid;
}

gboolean
gabble_jingle_session_parse (GabbleJingleSession *sess, JingleAction action, LmMessage *message, GError **error)
{
  TpHandleRepoIface *contact_repo;
  GabbleJingleSessionPrivate *priv = sess->priv;
  LmMessageNode *iq_node, *session_node;
  const gchar *from, *resource;
  const gchar *initiator;

  iq_node = lm_message_get_node (message);

  /* IQ from/to can come in handy */
  from = lm_message_node_get_attribute (iq_node, "from");

  DEBUG ("jingle action '%s' from '%s' in session '%s' dialect %u state %u",
      produce_action (action, priv->dialect), from, priv->sid,
      priv->dialect, priv->state);

  switch (priv->dialect) {
    case JINGLE_DIALECT_V032:
      session_node = lm_message_node_get_child_with_namespace (iq_node,
          "jingle", NS_JINGLE032);
      break;
    case JINGLE_DIALECT_V015:
      session_node = lm_message_node_get_child_with_namespace (iq_node,
          "jingle", NS_JINGLE015);
      break;
    case JINGLE_DIALECT_GTALK3:
    case JINGLE_DIALECT_GTALK4:
      session_node = lm_message_node_get_child_with_namespace (iq_node,
          "session", NS_GOOGLE_SESSION);
      break;
    default:
      /* just to make gcc happy about dealing with default case */
      session_node = NULL;
  }

  if (session_node == NULL)
    {
      SET_BAD_REQ ("malformed jingle stanza");
      return FALSE;
    }

  if (action == JINGLE_ACTION_UNKNOWN)
    {
      SET_BAD_REQ ("unknown session action");
      return FALSE;
    }

  initiator = lm_message_node_get_attribute (session_node, "initiator");
  if (initiator == NULL)
    {
      SET_BAD_REQ ("session initiator not found");
      return FALSE;
    }

  resource = strchr (from, '/');
  if (resource == NULL || *resource == '\0')
    {
      SET_BAD_REQ ("sender with no resource");
      return FALSE;
    }
  resource++;

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (!action_is_allowed (action, priv->state))
    {
      SET_OUT_ORDER ("action \"%s\" not allowed in current state",
          produce_action (action, priv->dialect));
      return FALSE;
    }

  /* if we just created the session, fill in the data */
  if (priv->state == JS_STATE_PENDING_CREATED)
    {
      sess->peer = tp_handle_ensure (contact_repo, from, NULL, NULL);

      if (sess->peer == 0)
        {
          SET_BAD_REQ ("unable to get sender handle");
          return FALSE;
        }

      priv->peer_resource = g_strdup (resource);
      priv->peer_jid = g_strdup (from);
      priv->initiator = g_strdup (initiator);
    }

  jingle_state_machine_dance (sess, action, session_node, error);

  if (*error != NULL)
    return FALSE;

  return TRUE;
}

static gchar *
get_jid_for_contact (GabbleJingleSession *session,
                     TpHandle handle)
{
  GabbleJingleSessionPrivate *priv;
  TpBaseConnection *conn;
  const gchar *base_jid;
  TpHandle self;
  TpHandleRepoIface *contact_handles;

  g_assert (GABBLE_IS_JINGLE_SESSION (session));

  priv = session->priv;
  conn = (TpBaseConnection *) priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  self = conn->self_handle;

  base_jid = tp_handle_inspect (contact_handles, handle);
  g_assert (base_jid != NULL);

  if (handle == self)
    {
      gchar *resource, *ret;
      g_object_get (priv->conn, "resource", &resource, NULL);
      g_assert (resource != NULL);
      ret = g_strdup_printf ("%s/%s", base_jid, resource);
      g_free (resource);
      return ret;
    }
  else
    {
      g_assert (priv->peer_resource != NULL);
      return g_strdup_printf ("%s/%s", base_jid, priv->peer_resource);
    }
}

LmMessage *
gabble_jingle_session_new_message (GabbleJingleSession *sess,
    JingleAction action, LmMessageNode **sess_node)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  LmMessage *msg;
  LmMessageNode *iq_node, *session_node;
  gchar *el = NULL, *ns = NULL;
  gboolean gtalk_mode = FALSE;

  g_assert ((action == JINGLE_ACTION_SESSION_INITIATE) ||
            (priv->state > JS_STATE_PENDING_CREATED));
  g_assert (GABBLE_IS_JINGLE_SESSION (sess));

  /* possibly this is the first message in an outgoing session,
   * meaning that we have to set up initiator */
  if (priv->initiator == NULL) {
      TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
      priv->initiator = get_jid_for_contact (sess, conn->self_handle);
  }
  /* likewise ^^ */
  if (priv->peer_jid == NULL) {
      priv->peer_jid = get_jid_for_contact (sess, sess->peer);
  }

  msg = lm_message_new_with_sub_type (
      priv->peer_jid,
      LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_SET);

  iq_node = lm_message_get_node (msg);

  switch (priv->dialect)
    {
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
      default:
        el = "jingle";
        ns = NS_JINGLE032;
        break;
    }

  session_node = lm_message_node_add_child (iq_node, el, NULL);
  lm_message_node_set_attributes (session_node,
      "xmlns", ns,
      "initiator", priv->initiator,
      (gtalk_mode) ? "id" : "sid", priv->sid,
      (gtalk_mode) ? "type" : "action",
        produce_action (action, priv->dialect),
      NULL);

  if (sess_node != NULL)
      *sess_node = session_node;

  return msg;
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
      gchar *disposition;
      GabbleJingleContent *c = GABBLE_JINGLE_CONTENT (li->data);

      g_object_get (c, "disposition", &disposition, NULL);

      if (!tp_strdiff (disposition, "session"))
        {
          mapper (sess, c, user_data);
        }

      g_free (disposition);
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
    GabbleJingleContent *c, gpointer user_data)
{
  gabble_jingle_content_retransmit_candidates (c);
}

static void
_fill_content (GabbleJingleSession *sess,
    GabbleJingleContent *c, gpointer user_data)
{
  LmMessageNode *sess_node = user_data;
  JingleContentState state;

  gabble_jingle_content_produce_node (c, sess_node, TRUE);

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

static LmHandlerResult
_process_reply (GabbleConnection *conn,
    LmMessage *sent,
    LmMessage *reply,
    GObject *obj,
    gpointer cb_)
{
  JingleReplyHandler cb = cb_;

  cb (obj, (lm_message_get_sub_type (reply) == LM_MESSAGE_SUB_TYPE_RESULT),
      reply);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * gabble_jingle_session_send:
 * @sess: a session
 * @msg: a stanza, of which this function will take ownership
 * @cb: callback for the IQ reply, or %NULL to ignore the reply
 * @weak_object: an object to pass to @cb, or %NULL
 *
 * Sends an IQ, optionally calling @cb for the reply. If @weak_object is not
 * NULL, @cb will only be called if @weak_object is still alive.
 */
void
gabble_jingle_session_send (GabbleJingleSession *sess,
    LmMessage *msg,
    JingleReplyHandler cb,
    GObject *weak_object)
{
  if (cb != NULL)
    _gabble_connection_send_with_reply (sess->priv->conn, msg,
        _process_reply, weak_object, cb, NULL);
  else
    _gabble_connection_send_with_reply (sess->priv->conn, msg,
        NULL, NULL, NULL, NULL);

  lm_message_unref (msg);
}

static void
_on_initiate_reply (GObject *sess_as_obj,
    gboolean success,
    LmMessage *reply)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (sess_as_obj);

  if (success)
    set_state (sess, JS_STATE_PENDING_INITIATED, 0);
  else
    set_state (sess, JS_STATE_ENDED, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
}

static void
_on_accept_reply (GObject *sess_as_obj,
    gboolean success,
    LmMessage *reply)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (sess_as_obj);

  if (success)
    set_state (sess, JS_STATE_ACTIVE, 0);
  else
    set_state (sess, JS_STATE_ENDED, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
}

static gboolean
timeout_session (gpointer data)
{
  GabbleJingleSession *session = data;

  DEBUG ("session timed out");
  session->priv->timer_id = 0;

  gabble_jingle_session_terminate (session,
      TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER, NULL);
  return FALSE;
}

static void
try_session_initiate_or_accept (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  LmMessage *msg;
  LmMessageNode *sess_node;
  gboolean contents_ready = TRUE;
  JingleAction action;
  JingleSessionState new_state;
  JingleReplyHandler handler;

  /* If there are no contents yet, we shouldn't have been called at all. */
  g_assert (g_hash_table_size (priv->contents) > 0);

  if (priv->local_initiator)
    {
      if (priv->state != JS_STATE_PENDING_CREATED)
        {
          DEBUG ("session is in state %u, won't try to initiate", priv->state);
          return;
        }

      action = JINGLE_ACTION_SESSION_INITIATE;
      new_state = JS_STATE_PENDING_INITIATE_SENT;
      handler = _on_initiate_reply;
    }
  else
    {
      if (priv->state != JS_STATE_PENDING_INITIATED)
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
      new_state = JS_STATE_PENDING_ACCEPT_SENT;
      handler = _on_accept_reply;
    }

  _map_initial_contents (sess, _check_content_ready, &contents_ready);

  if (!contents_ready)
      return;

  msg = gabble_jingle_session_new_message (sess, action, &sess_node);
  _map_initial_contents (sess, _fill_content, sess_node);
  gabble_jingle_session_send (sess, msg, handler, (GObject *) sess);
  set_state (sess, new_state, 0);

  /* now all initial contents can transmit their candidates */
  _map_initial_contents (sess, _transmit_candidates, NULL);
}

/**
 * set_state:
 * @sess: a jingle session
 * @state: the new state for the session
 * @termination_reason: if @state is JS_STATE_ENDED, the reason the session
 *                      ended. Otherwise, must be 0.
 */
static void
set_state (GabbleJingleSession *sess,
           JingleState state,
           TpChannelGroupChangeReason termination_reason)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  if (state <= priv->state)
    {
      DEBUG ("ignoring request to set state from %u back to %u", priv->state, state);
      return;
    }

  if (state != JS_STATE_ENDED)
    g_assert (termination_reason == 0);

  DEBUG ("Setting state of JingleSession: %p (priv = %p) from %u to %u", sess, priv, priv->state, state);

  priv->state = state;
  g_object_notify (G_OBJECT (sess), "state");

  /* if we or peer just initiated the session, set the session timer */
  if ((priv->local_initiator && (state == JS_STATE_PENDING_INITIATE_SENT)) ||
      (!priv->local_initiator && (state == JS_STATE_PENDING_INITIATED)))
    {
      g_assert (priv->timer_id == 0);
      priv->timer_id = g_timeout_add (session_timeout_time,
        timeout_session, sess);
    }
  /* if we're active or ended, we can clear the timer */
  else if ((state >= JS_STATE_ACTIVE) && (priv->timer_id != 0))
    {
      g_source_remove (priv->timer_id);
      priv->timer_id = 0;
    }

  if (state == JS_STATE_ENDED)
    g_signal_emit (sess, signals[TERMINATED], 0, priv->locally_terminated,
        termination_reason);
}

void
gabble_jingle_session_accept (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = sess->priv;

  priv->locally_accepted = TRUE;

  try_session_initiate_or_accept (sess);
}

static const gchar *
_get_jingle_reason (GabbleJingleSession *sess,
                    TpChannelGroupChangeReason reason)
{
  switch (reason)
    {
    case TP_CHANNEL_GROUP_CHANGE_REASON_NONE:
      if (sess->priv->state == JS_STATE_ACTIVE)
        return "success";
      else
        return "cancel";
    case TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE:
      return "gone";
    case TP_CHANNEL_GROUP_CHANGE_REASON_BUSY:
      return "busy";
    case TP_CHANNEL_GROUP_CHANGE_REASON_ERROR:
      return "general-error";
    case TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER:
      return "timeout";
    default:
      return NULL;
    }
}

gboolean
gabble_jingle_session_terminate (GabbleJingleSession *sess,
                                 TpChannelGroupChangeReason reason,
                                 GError **error)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  const gchar *reason_elt;

  if (priv->state == JS_STATE_ENDED)
    {
      DEBUG ("session already terminated, ignoring terminate request");
      return TRUE;
    }

  reason_elt = _get_jingle_reason (sess, reason);

  if (reason_elt == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "%u doesn't make sense as a reason to end a call", reason);
      return FALSE;
    }

  if (priv->state != JS_STATE_PENDING_CREATED)
    {
      LmMessageNode *session_node;
      LmMessage *msg = gabble_jingle_session_new_message (sess,
          JINGLE_ACTION_SESSION_TERMINATE, &session_node);

      if (priv->dialect == JINGLE_DIALECT_V032 && reason_elt != NULL)
        lm_message_node_add_child (
            lm_message_node_add_child (session_node, "reason", NULL),
            reason_elt, NULL);

      gabble_jingle_session_send (sess, msg, NULL, NULL);
    }

  /* NOTE: on "terminated", jingle factory and media channel will unref
   * it, bringing refcount to 0, so dispose will be called, and it
   * takes care of cleanup */

  DEBUG ("we are terminating this session");
  priv->locally_terminated = TRUE;
  set_state (sess, JS_STATE_ENDED, reason);

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

  g_hash_table_foreach (priv->contents, _foreach_count_active_contents, &n_contents);
  return n_contents;
}

static void
content_removed_cb (GabbleJingleContent *c, gpointer user_data)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (user_data);
  GabbleJingleSessionPrivate *priv = sess->priv;
  const gchar *name;

  g_object_get (c, "name", &name, NULL);
  g_hash_table_remove (priv->contents, name);

  if (priv->state == JS_STATE_ENDED)
      return;

  if (count_active_contents (sess) == 0)
    gabble_jingle_session_terminate (sess, TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL);
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
      /* session will be terminated when the content gets maked as removed */
      DEBUG ("called for last active content, doing session-terminate instead");
      gabble_jingle_content_remove (c, FALSE);
    }
}

GabbleJingleContent *
gabble_jingle_session_add_content (GabbleJingleSession *sess, JingleMediaType mtype,
    const gchar *content_ns, const gchar *transport_ns)
{
  GabbleJingleSessionPrivate *priv = sess->priv;
  GabbleJingleContent *c;
  GType content_type;
  gchar *name = NULL;
  gint id = g_hash_table_size (priv->contents) + 1;

  do
    {
      g_free (name);
      name = g_strdup_printf ("stream%d", id++);
    }
  while (g_hash_table_lookup (priv->contents, name) != NULL);

  content_type = gabble_jingle_factory_lookup_content_type (
      priv->conn->jingle_factory, content_ns);

  g_assert (content_type != 0);

  c = create_content (sess, content_type, mtype,
      content_ns, transport_ns, name, NULL, NULL);

  g_free (name);

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
  return g_hash_table_get_values (priv->contents);
}

static void
content_ready_cb (GabbleJingleContent *c, gpointer user_data)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (user_data);
  const gchar *disposition;

  DEBUG ("called");

  g_object_get (c, "disposition", &disposition, NULL);
  g_assert (!tp_strdiff (disposition, "session"));

  try_session_initiate_or_accept (sess);
}

/* Only to be used for the test suite */
void
gabble_set_jingle_session_timeout (guint ms)
{
  session_timeout_time = ms;
}

void
gabble_jingle_session_send_held (GabbleJingleSession *sess,
                                 gboolean held)
{
  LmMessage *message;
  LmMessageNode *jingle, *notification;

  if (sess->priv->dialect != JINGLE_DIALECT_V032)
    {
      DEBUG ("FIXME: fake hold for Ye Olde Jingle and GTalk.");
      return;
    }

  message = gabble_jingle_session_new_message (sess,
      JINGLE_ACTION_SESSION_INFO, &jingle);

  notification = lm_message_node_add_child (jingle,
      (held ? "hold" : "active"), NULL);
  lm_message_node_set_attributes (notification, "xmlns", NS_JINGLE_RTP_INFO,
      NULL);

  /* This is just informational, so ignoring the reply. */
  gabble_jingle_session_send (sess, message, NULL, NULL);
}
