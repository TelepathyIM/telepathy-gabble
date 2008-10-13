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

#include "debug.h"
#include "connection.h"
#include "util.h"
#include "namespaces.h"
#include "jingle-factory.h"
#include "jingle-content.h"

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

typedef struct _GabbleJingleSessionPrivate GabbleJingleSessionPrivate;
struct _GabbleJingleSessionPrivate
{
  GabbleConnection *conn;

  gchar *peer_resource;
  gchar *peer_jid;
  gchar *initiator;
  gboolean local_initiator;
  GHashTable *contents;
  GHashTable *initial_contents;
  JingleDialect dialect;
  JingleState state;
  gchar *sid;

  gboolean locally_accepted;

  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_SESSION_GET_PRIVATE(o)\
  ((GabbleJingleSessionPrivate *) ((o)->priv))

/* lookup tables */

static const gchar *action_table[] = {
  "content-accept",
  "content-add",
  "content-modify",
  "content-remove",
  "content-replace",
  "session-accept",
  "session-info",
  "session-initiate",
  "session-terminate",
  "transport-info",
  "transport-accept",
  NULL
};

typedef struct {
  JingleState state;
  JingleAction *actions;
} JingleStateActions;

static JingleAction allowed_actions[6][8] = {
  /* JS_STATE_PENDING_CREATED */
  { JINGLE_ACTION_SESSION_INITIATE, JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_PENDING_INITIATE_SENT */
  { JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_SESSION_ACCEPT,
    JINGLE_ACTION_TRANSPORT_INFO, JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_PENDING_INITIATED */
  { JINGLE_ACTION_SESSION_ACCEPT, JINGLE_ACTION_SESSION_TERMINATE,
    JINGLE_ACTION_TRANSPORT_INFO,
    JINGLE_ACTION_CONTENT_MODIFY, JINGLE_ACTION_CONTENT_ACCEPT,
    JINGLE_ACTION_CONTENT_REMOVE, JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_PENDING_ACCEPT_SENT */
  { JINGLE_ACTION_SESSION_TERMINATE, JINGLE_ACTION_UNKNOWN },
  /* JS_STATE_ACTIVE */
  { JINGLE_ACTION_CONTENT_MODIFY, JINGLE_ACTION_CONTENT_ADD,
    JINGLE_ACTION_CONTENT_REMOVE, JINGLE_ACTION_CONTENT_REPLACE,
    JINGLE_ACTION_SESSION_INFO, JINGLE_ACTION_TRANSPORT_INFO,
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
  priv->initial_contents = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, NULL);

  priv->state = JS_STATE_PENDING_CREATED;
  priv->locally_accepted = FALSE;
  priv->dispose_has_run = FALSE;
}

static void
gabble_jingle_session_dispose (GObject *object)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (object);
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  g_hash_table_destroy (priv->contents);
  priv->contents = NULL;

  g_hash_table_destroy (priv->initial_contents);
  priv->initial_contents = NULL;

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

  DEBUG ("got here in dispose");
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
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

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
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

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
      break;
    case PROP_PEER_RESOURCE:
      g_free (priv->peer_resource);
      priv->peer_resource = g_value_dup_string (value);
      DEBUG ("setting peer resource to %s", priv->peer_resource);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      g_assert_not_reached ();
      break;
  }
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
        G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[TERMINATED] = g_signal_new ("terminated",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
        G_TYPE_NONE, 1, G_TYPE_POINTER);
}

typedef void (*HandlerFunc)(GabbleJingleSession *sess,
  LmMessageNode *node, GError **error);
typedef void (*ContentHandlerFunc)(GabbleJingleSession *sess,
  GabbleJingleContent *c, LmMessageNode *content_node, GError **error);

const gchar *
_enum_to_string (const gchar **table, gint val)
{
  return table[val];
}

gint
_string_to_enum (const gchar **table, const gchar *val)
{
  gint i;

  if (val == NULL)
    return -1;

  for (i = 0; table[i] != NULL; i++)
    {
      if (!tp_strdiff (val, table[i]))
        return i;
    }

  return -1;
}

static JingleAction
parse_action (const gchar *txt)
{
  /* synonyms, best deal with them right now */
  if (!tp_strdiff (txt, "initiate"))
    txt = "session-initiate";
  else if (!tp_strdiff (txt, "terminate"))
    txt = "session-terminate";
  else if (!tp_strdiff (txt, "accept"))
    txt = "session-accept";
  else if (!tp_strdiff (txt, "reject"))
    txt = "session-terminate";
  else if (!tp_strdiff (txt, "candidates"))
    txt = "transport-info";

  return (JingleAction) _string_to_enum (action_table, txt);
}

static const gchar *
produce_action (JingleAction act, JingleDialect dialect)
{
  g_assert (act != JINGLE_ACTION_UNKNOWN);

  if ((dialect == JINGLE_DIALECT_GTALK3) || (dialect == JINGLE_DIALECT_GTALK4))
    {
      switch (act) {
        case JINGLE_ACTION_SESSION_INITIATE:
          return "initiate";
        case JINGLE_ACTION_SESSION_TERMINATE:
          return "terminate";
        case JINGLE_ACTION_SESSION_ACCEPT:
          return "accept";
        case JINGLE_ACTION_TRANSPORT_INFO:
          if (dialect == JINGLE_DIALECT_GTALK3)
              return "candidates";
          else
              break;
        default:
          return _enum_to_string (action_table, act);
      }
    }

  return _enum_to_string (action_table, act);
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

static void set_state (GabbleJingleSession *sess, JingleState state);
static GabbleJingleContent *_get_any_content (GabbleJingleSession *session);

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

static void
_foreach_content (GabbleJingleSession *sess, LmMessageNode *node,
  ContentHandlerFunc func, GError **error)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  GabbleJingleContent *c;
  LmMessageNode *content_node;
  const gchar *name;

  for (content_node = node->children;
       NULL != content_node;
       content_node = content_node->next)
    {
      if (tp_strdiff (content_node->name, "content"))
        continue;

      name = lm_message_node_get_attribute (content_node, "name");
      c = g_hash_table_lookup (priv->contents, name);

      func (sess, c, content_node, error);
      if (*error)
        return;
    }
}

static void content_ready_cb (GabbleJingleContent *c,
    GParamSpec *arg, GabbleJingleSession *sess);

static void
_each_content_add (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  const gchar *name = lm_message_node_get_attribute (content_node, "name");
  LmMessageNode *desc_node = lm_message_node_get_child (content_node, "description");
  GType content_type = 0;
  const gchar *content_ns = NULL;

  if (desc_node)
    {
      content_ns = lm_message_node_get_attribute (desc_node, "xmlns");
      content_type =
          GPOINTER_TO_INT (g_hash_table_lookup (priv->conn->jingle_factory->content_types,
          content_ns));
    }

  if (content_type == 0)
    {
      SET_BAD_REQ ("unsupported content type");
      DEBUG ("unsupported content type with ns %s", content_ns);
      return;
    }

  DEBUG ("remote end adds new content named '%s' of type %s", name, g_type_name (content_type));

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

  DEBUG ("session creating new content type, conn == %p, jf == %p", priv->conn, priv->conn->jingle_factory);
  c = g_object_new (content_type,
                    "connection", priv->conn,
                    "session", sess,
                    "content-ns", content_ns,
                    NULL);

  g_signal_connect (c, "notify::ready",
      (GCallback) content_ready_cb, sess);

  gabble_jingle_content_parse_add (c, content_node,
    ((priv->dialect == JINGLE_DIALECT_GTALK3) ||
    (priv->dialect == JINGLE_DIALECT_GTALK4)), error);

  if (*error)
    {
      g_object_unref (c);
      return;
    }

  /* gtalk streams don't have name, so use whatever Content came up with */
  if (name == NULL)
    {
      g_object_get (c, "name", &name, NULL);
    }

  /* This will override existing content if it exists. */
  g_hash_table_replace (priv->contents, g_strdup (name), c);

  g_signal_emit (sess, signals[NEW_CONTENT], 0, c);
}

static void
_each_content_remove (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  const gchar *name = lm_message_node_get_attribute (content_node, "name");

  if (c == NULL)
    {
      SET_BAD_REQ ("content called \"%s\" doesn't exist", name);
      return;
    }

  if (g_hash_table_size (priv->contents) == 1)
    {
      SET_BAD_REQ ("unable to remove the last content in a session");
      return;
    }

  /* This should have the effect of shutting the content down.
   * FIXME: do we need to have REMOVING state at all? */
  g_hash_table_remove (priv->contents, name);
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

  if (*error)
    return;
}

static void
_each_content_replace (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node, GError **error)
{
  _each_content_remove (sess, c, content_node, error);

  if (*error)
    return;

  _each_content_add (sess, c, content_node, error);
}

static void
_each_content_accept (GabbleJingleSession *sess, GabbleJingleContent *c,
    LmMessageNode *content_node ,GError **error)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

  if (c == NULL)
    {
      const gchar *name = lm_message_node_get_attribute (content_node, "name");
      SET_BAD_REQ ("content called \"%s\" doesn't exist", name);
      return;
    }

  gabble_jingle_content_parse_accept (c, content_node,
      (priv->dialect <= JINGLE_DIALECT_GTALK4), error);
}

static void
_mark_each_initial_content (gpointer key, gpointer data, gpointer user_data)
{
  GabbleJingleSession *sess = GABBLE_JINGLE_SESSION (user_data);
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

  g_hash_table_insert (priv->initial_contents, key, GUINT_TO_POINTER (TRUE));
}

static void
on_session_initiate (GabbleJingleSession *sess, LmMessageNode *node,
  GError **error)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

  /* we can't call ourselves at the moment */
  if (priv->local_initiator)
    {
      // FIXME: terminate session here, plzkthxbai
      // jingle_session_terminate (sess, FALSE, JINGLE_REASON_BUSY);
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
      /* mark all newly defined contents as initial contents */
      g_hash_table_foreach (priv->initial_contents,
          _mark_each_initial_content, sess);

      set_state (sess, JS_STATE_PENDING_INITIATED);
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
on_content_accept (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  // FIXME
  // _foreach_content (sess, node, _each_content_replace, error);
}

static void
on_session_accept (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

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

  if (*error)
      return;

  set_state (sess, JS_STATE_ACTIVE);
}

static void
on_session_terminate (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  DEBUG ("remote end terminates the session");
  set_state (sess, JS_STATE_ENDED);
  g_signal_emit (sess, signals[TERMINATED], 0, FALSE);
}

static void
on_transport_info (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  GabbleJingleContent *c = NULL;

  /* FIXME: we need to do dialect detection here!!! */

  if (priv->dialect <= JINGLE_DIALECT_GTALK4)
    {
      /* GTalk has only one content anyways */
      GList *cs = g_hash_table_get_values (priv->contents);

      /* Check that's the case */
      /* FIXME: report error to peer instead of assert */
      g_assert (g_list_length (cs) == 1);

      c = cs->data;

      g_list_free (cs);

      /* FIXME: here we need to take care if we receive <candidates>
       * and we're in gtalk4 mode, to switch to gtalk3? */
      /* GTalk4 includes "transport" subnode */
      if (priv->dialect == JINGLE_DIALECT_GTALK4)
        node = lm_message_node_get_child (node, "transport");
    }
  else
    {
      const gchar *name;

      node = lm_message_node_get_child (node, "content");

      name = lm_message_node_get_attribute (node, "name");

      c = g_hash_table_lookup (priv->contents, name);

      /* FIXME: report error to peer instead of assert */
      g_assert (c != NULL);

      /* we need transport child of content node */
      node = lm_message_node_get_child (node, "transport");
    }

  gabble_jingle_content_parse_transport_info (c, node, error);
}

static void
on_transport_accept (GabbleJingleSession *sess, LmMessageNode *node,
    GError **error)
{
  DEBUG ("Ignoring 'transport-accept' action from peer");
}


static HandlerFunc handlers[] = {
  on_content_accept,
  on_content_add,
  on_content_modify,
  on_content_remove,
  on_content_replace,
  on_session_accept, /* jingle_on_session_accept */
  NULL, /* jingle_on_session_info */
  on_session_initiate,
  on_session_terminate, /* jingle_on_session_terminate */
  on_transport_info, /* jingle_on_transport_info */
  on_transport_accept
};

static void
jingle_state_machine_dance (GabbleJingleSession *sess, JingleAction action,
    LmMessageNode *node, GError **error)
  {
    GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

    /* parser should've checked this already */
    g_assert (action_is_allowed (action, priv->state));
    g_assert (handlers[action] != NULL);

  handlers[action] (sess, node, error);
}


const gchar *
gabble_jingle_session_parse (GabbleJingleSession *sess, LmMessage *message, GError **error)
{
  TpHandleRepoIface *contact_repo;
  GabbleJingleSessionPrivate *priv;
  LmMessageNode *iq_node, *session_node;
  const gchar *actxt, *sid, *from, *to, *resource;
  JingleDialect dialect;
  JingleAction action;
  const gchar *initiator, *responder;
  gboolean google_mode = FALSE;

  /* all jingle actions are sets */
  if (LM_MESSAGE_SUB_TYPE_SET != lm_message_get_sub_type (message))
    return FALSE;

  iq_node = lm_message_get_node (message);

  /* IQ from/to can come in handy */
  from = lm_message_node_get_attribute (iq_node, "from");
  if (from == NULL)
    return NULL;

  to = lm_message_node_get_attribute (iq_node, "to");
  if (to == FALSE)
    return NULL;

  /* first, we try standard jingle */
  session_node = lm_message_node_get_child_with_namespace (iq_node,
      "jingle", NS_JINGLE026);

  if (session_node != NULL)
    {
      dialect = JINGLE_DIALECT_V026;
    }
  else
    {
      /* then, we try a bit older jingle version */
      session_node = lm_message_node_get_child_with_namespace (iq_node,
          "jingle", NS_JINGLE015);

      if (session_node != NULL)
        {
          dialect = JINGLE_DIALECT_V015;
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
              dialect = JINGLE_DIALECT_GTALK4;
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

  if (actxt == NULL)
    {
      SET_BAD_REQ ("session action not found");
      return NULL;
    }

  if (sid == NULL)
    {
      SET_BAD_REQ ("session id not found");
      return NULL;
    }

  action = parse_action (actxt);
  if (action == JINGLE_ACTION_UNKNOWN)
    {
      SET_BAD_REQ ("unknown session action");
      return NULL;
    }

  initiator = lm_message_node_get_attribute (session_node, "initiator");
  if (initiator == NULL)
    {
      SET_BAD_REQ ("session initiator not found");
      return NULL;
    }

  resource = strchr (from, '/');
  if (resource == NULL || *resource == '\0')
    {
      SET_BAD_REQ ("sender with no resource");
      return NULL;
    }

  /* this one is not required, so it can be NULL */
  responder = lm_message_node_get_attribute (session_node, "responder");

  /* if we were just validating, return successfully now */
  if (sess == NULL)
    return sid;

  priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

  DEBUG("jingle action '%s' from '%s' in session '%s' dialect %u", actxt, from,
      sid, dialect);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (!action_is_allowed (action, priv->state))
    {
      SET_OUT_ORDER ("action \"%s\" not allowed in current state", actxt);
      return NULL;
    }

  sess->peer = tp_handle_ensure (contact_repo, from, NULL, NULL);
  if (sess->peer == 0)
    {
      SET_BAD_REQ ("unable to get sender handle");
      return NULL;
    }

  /* if we just created the session, fill in the data */
  if (priv->state == JS_STATE_PENDING_CREATED)
    {
      priv->sid = g_strdup (sid);
      priv->peer_resource = g_strdup (resource);
      priv->dialect = dialect;
      priv->peer_jid = g_strdup (from);
      priv->initiator = g_strdup (initiator);
    }

  jingle_state_machine_dance (sess, action, session_node, error);

  if (*error)
    return NULL;

  return sid;
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

  priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (session);
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
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  LmMessage *msg;
  LmMessageNode *iq_node, *session_node;
  gchar *el = NULL, *ns = NULL;
  gboolean gtalk_mode = FALSE;

  DEBUG ("creating new message to peer: %s", priv->peer_jid);

  g_assert ((action == JINGLE_ACTION_SESSION_INITIATE) || (priv->state > JS_STATE_PENDING_CREATED));

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

  switch (priv->dialect) {
    case JINGLE_DIALECT_V026:
      el = "jingle";
      ns = NS_JINGLE026;
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

  session_node = lm_message_node_add_child (iq_node, el, NULL);
  lm_message_node_set_attributes (session_node,
      "xmlns", ns,
      "initiator", priv->initiator,
      (gtalk_mode) ? "id" : "sid", priv->sid,
      (gtalk_mode) ? "type" : "action",
        produce_action (action, priv->dialect),
      NULL);

  *sess_node = session_node;

  return msg;
}

static void
_check_content_for_acceptance (gpointer key, gpointer data, gpointer user_data)
{
  GabbleJingleContent *c = GABBLE_JINGLE_CONTENT (data);
  gboolean *is_ready = (gboolean *) user_data;

  *is_ready = gabble_jingle_content_is_ready (c, TRUE);
}

static void
_check_content_for_initiation (gpointer key, gpointer data, gpointer user_data)
{
  GabbleJingleContent *c = GABBLE_JINGLE_CONTENT (data);
  gboolean *is_ready = (gboolean *) user_data;

  *is_ready = gabble_jingle_content_is_ready (c, FALSE);
}

static void
_try_session_accept_fill (gpointer key, gpointer data, gpointer user_data)
{
  GabbleJingleContent *c = GABBLE_JINGLE_CONTENT (data);
  LmMessageNode *sess_node = user_data;
  JingleContentState state;

  g_object_get (c, "state", &state, NULL);

  DEBUG ("considering whether to add content node, state = %u", state);

  /* we only want to acknowledge newly added contents */
  if (state == JINGLE_CONTENT_STATE_EMPTY)
    gabble_jingle_content_produce_node (c, sess_node, TRUE);
}

static void
try_session_accept (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  LmMessage *msg;
  LmMessageNode *sess_node;
  gboolean content_ready = TRUE;

  g_assert (g_hash_table_size (priv->contents) > 0);

  if (priv->state != JS_STATE_PENDING_INITIATED)
    {
      DEBUG ("session is in state %u, won't try to accept", priv->state);
      return;
    }

  if (!priv->locally_accepted)
      return;

  g_hash_table_foreach (priv->initial_contents, _check_content_for_acceptance, &content_ready);

  if (!content_ready)
      return;

  msg = gabble_jingle_session_new_message (sess, JINGLE_ACTION_SESSION_ACCEPT,
      &sess_node);

  g_hash_table_foreach (priv->initial_contents, _try_session_accept_fill, sess_node);

  _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);

  set_state (sess, JS_STATE_ACTIVE);
}

static void
try_session_initiate (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  LmMessage *msg;
  LmMessageNode *sess_node;
  gboolean content_ready = TRUE;

  g_assert (g_hash_table_size (priv->contents) > 0);

  if (priv->state != JS_STATE_PENDING_CREATED)
    {
      DEBUG ("session is in state %u, won't try to initiate", priv->state);
      return;
    }

  g_hash_table_foreach (priv->contents, _check_content_for_initiation, &content_ready);

  if (!content_ready)
      return;

  msg = gabble_jingle_session_new_message (sess, JINGLE_ACTION_SESSION_INITIATE,
      &sess_node);

  g_hash_table_foreach (priv->contents, _try_session_accept_fill, sess_node);

  _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);

  set_state (sess, JS_STATE_PENDING_INITIATE_SENT);
}

static void
set_state (GabbleJingleSession *sess, JingleState state)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

  priv->state = state;
  DEBUG ("Setting state of JingleSession: %p (priv = %p), state = %u", sess, priv, priv->state);
  g_object_notify (G_OBJECT (sess), "state");
}

void
gabble_jingle_session_accept (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);

  priv->locally_accepted = TRUE;

  try_session_accept (sess);
}

void
gabble_jingle_session_terminate (GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  LmMessage *msg;
  LmMessageNode *sess_node;

  msg = gabble_jingle_session_new_message (sess, JINGLE_ACTION_SESSION_TERMINATE,
      &sess_node);

  _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);

  /* NOTE: on "terminated", jingle factory and media channel will unref
   * it, bringing refcount to 0, so dispose will be called, and it
   * takes care of cleanup */

  DEBUG ("we are terminating this session");
  set_state (sess, JS_STATE_ENDED);
  g_signal_emit (sess, signals[TERMINATED], 0, FALSE);
}

void
gabble_jingle_session_remove_content (GabbleJingleSession *sess,
    const gchar *content_name)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  GabbleJingleContent *c = g_hash_table_lookup (priv->contents, content_name);
  LmMessage *msg;
  LmMessageNode *sess_node;

  g_return_if_fail (c != NULL);

  msg = gabble_jingle_session_new_message (sess, JINGLE_ACTION_CONTENT_REMOVE,
      &sess_node);

  g_assert (msg != NULL);

  gabble_jingle_content_produce_node (c, sess_node, FALSE);
  /* FIXME: actually send it, mark it as in removal, etc */

  /* FIXME: emit a 'content-removed' signal */
  /* This should g_object_unref the content */
  g_hash_table_remove (priv->contents, content_name);
}

GabbleJingleContent *
gabble_jingle_session_add_content (GabbleJingleSession *sess, JingleMediaType mtype,
    const gchar *content_ns, const gchar *transport_ns)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
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

  content_type =
      GPOINTER_TO_INT (g_hash_table_lookup (priv->conn->jingle_factory->content_types,
      content_ns));

  g_assert (content_type != 0);

  c = g_object_new (content_type,
                    "connection", priv->conn,
                    "session", sess,
                    "media-type", mtype,
                    "content-ns", content_ns,
                    "transport-ns", transport_ns,
                    "name", name,
                    "senders", JINGLE_CONTENT_SENDERS_BOTH,
                    NULL);

  g_signal_connect (c, "notify::ready",
      (GCallback) content_ready_cb, sess);

  g_hash_table_insert (priv->contents, g_strdup (name), c);

  g_signal_emit (sess, signals[NEW_CONTENT], 0, c);

  /* FIXME BUG: for session acceptance, we should only consider
   * contents with disposition != "session". we'll burn that bridge
   * when we come to it. Here we assume all the contents added before
   * an initiation message has been sent are initial contents. */
  if (priv->state == JS_STATE_PENDING_CREATED)
      g_hash_table_insert (priv->initial_contents, name, GUINT_TO_POINTER (TRUE));

  /* Try to signal content-add if needed. */
  if ((priv->state == JS_STATE_ACTIVE) ||
      ((priv->state >= JS_STATE_PENDING_INITIATE_SENT) &&
          (priv->dialect >= JINGLE_DIALECT_V026)))
    {
      LmMessage *msg;
      LmMessageNode *sess_node;

      msg = gabble_jingle_session_new_message (sess, JINGLE_ACTION_CONTENT_ADD,
          &sess_node);
      gabble_jingle_content_produce_node (c, sess_node, TRUE);
      _gabble_connection_send (priv->conn, msg, NULL);
      lm_message_unref (msg);

      g_object_set (c, "state", JINGLE_CONTENT_STATE_SENT, NULL);
    }

  return c;
}

void
gabble_jingle_session_change_direction (GabbleJingleSession *sess,
    const gchar *content_name, JingleContentSenders senders)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  GabbleJingleContent *c = g_hash_table_lookup (priv->contents, content_name);
  LmMessage *msg;
  LmMessageNode *sess_node;

  g_return_if_fail (c != NULL);

  g_object_set (c, "senders", senders, NULL);

  msg = gabble_jingle_session_new_message (sess, JINGLE_ACTION_CONTENT_MODIFY,
      &sess_node);

  g_assert (msg != NULL);

  gabble_jingle_content_produce_node (c, sess_node, FALSE);

  /* FIXME: send the message, mark the nodes as pending change, etc */
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
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  return g_hash_table_get_values (priv->contents);
}

static void
content_ready_cb (GabbleJingleContent *c,
    GParamSpec *arg, GabbleJingleSession *sess)
{
  GabbleJingleSessionPrivate *priv = GABBLE_JINGLE_SESSION_GET_PRIVATE (sess);
  const gchar *name;

  DEBUG ("called");

  g_object_get (c, "name", &name, NULL);

  if (g_hash_table_lookup (priv->initial_contents, name))
    {
      /* it's one of the contents from session-initiate */
      if (priv->local_initiator)
        {
          try_session_initiate (sess);
        }
      else
        {
          try_session_accept (sess);
        }
    }
  else
    {
      /* FIXME: should do analogous thing; if we are the content
       * initiator (hm, maybe we should really remember that),
       * and content is ready for acceptance, then accept it
       * ... */
    }
}


