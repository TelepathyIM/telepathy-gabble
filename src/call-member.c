/*
 * call-member.c - Source for CallMember
 * Copyright (C) 2010 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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


#include <stdio.h>
#include <stdlib.h>

#include "call-member.h"
#include "base-call-channel.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

G_DEFINE_TYPE(GabbleCallMember, gabble_call_member, G_TYPE_OBJECT)

/* signal enum */
enum
{
    FLAGS_CHANGED,
    CONTENT_ADDED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* properties */
enum
{
  PROP_CALL = 1,
  PROP_TARGET,
  PROP_SESSION
};

/* private structure */
struct _GabbleCallMemberPrivate
{
  TpHandle target;

  GabbleBaseCallChannel *call;
  GabbleCallMemberFlags flags;
  GabbleJingleSession *session;

  GabbleConnection *connection;

  GList *contents;
  gchar *transport_ns;

  gboolean dispose_has_run;
};

#define GABBLE_CALL_MEMBER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CALL_MEMBER, \
    GabbleCallMemberPrivate))

static void
gabble_call_member_init (GabbleCallMember *self)
{
  GabbleCallMemberPrivate *priv =
    GABBLE_CALL_MEMBER_GET_PRIVATE (self);

  self->priv = priv;
}

static void
gabble_call_member_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallMember *self = GABBLE_CALL_MEMBER (object);
  GabbleCallMemberPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CALL:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_SESSION:
        g_value_set_object (value, priv->session);
        break;
      case PROP_TARGET:
        g_value_set_uint (value, priv->target);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_member_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallMember *self = GABBLE_CALL_MEMBER (object);
  GabbleCallMemberPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CALL:
        priv->call = g_value_get_object (value);
        g_assert (priv->call != NULL);
        break;
      case PROP_SESSION:
        gabble_call_member_set_session (self,
          GABBLE_JINGLE_SESSION (g_value_get_object (value)));
        break;
      case PROP_TARGET:
        priv->target = g_value_get_uint (value);
        g_assert (priv->target != 0);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void gabble_call_member_dispose (GObject *object);
static void gabble_call_member_finalize (GObject *object);

static void
gabble_call_member_class_init (
    GabbleCallMemberClass *gabble_call_member_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_member_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_call_member_class,
      sizeof (GabbleCallMemberPrivate));

  object_class->dispose = gabble_call_member_dispose;
  object_class->finalize = gabble_call_member_finalize;

  object_class->get_property = gabble_call_member_get_property;
  object_class->set_property = gabble_call_member_set_property;

  param_spec = g_param_spec_object ("call", "Call",
      "The base call object that contains this member",
      GABBLE_TYPE_BASE_CALL_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CALL, param_spec);

  param_spec = g_param_spec_uint ("target", "Target",
      "the target handle of member",
      0,
      G_MAXUINT,
      0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET, param_spec);

  signals[FLAGS_CHANGED] =
    g_signal_new ("flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_call_member_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[CONTENT_ADDED] =
    g_signal_new ("content-added",
                  G_OBJECT_CLASS_TYPE (gabble_call_member_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

void
gabble_call_member_dispose (GObject *object)
{
  GabbleCallMember *self = GABBLE_CALL_MEMBER (object);
  GabbleCallMemberPrivate *priv = self->priv;
  GList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->session != NULL)
    g_object_unref (priv->session);
  priv->session = NULL;

  for (l = priv->contents ; l != NULL; l = g_list_next (l))
    g_object_unref (l->data);

  g_list_free (priv->contents);
  priv->contents = NULL;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_call_member_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_member_parent_class)->dispose (object);
}

void
gabble_call_member_finalize (GObject *object)
{
  GabbleCallMember *self = GABBLE_CALL_MEMBER (object);
  GabbleCallMemberPrivate *priv = self->priv;

  g_free (priv->transport_ns);
  priv->transport_ns = NULL;

  G_OBJECT_CLASS (gabble_call_member_parent_class)->finalize (object);
}

static void
remote_state_changed_cb (GabbleJingleSession *session, gpointer user_data)
{
  GabbleCallMember *self = GABBLE_CALL_MEMBER (user_data);
  GabbleCallMemberPrivate *priv = self->priv;
  GabbleCallMemberFlags newflags = 0;

  if (gabble_jingle_session_get_remote_ringing (session))
    newflags |= GABBLE_CALL_MEMBER_FLAG_RINGING;

  if (gabble_jingle_session_get_remote_hold (session))
    newflags |= GABBLE_CALL_MEMBER_FLAG_HELD;

  if (priv->flags == newflags)
    return;

  priv->flags = newflags;

  DEBUG ("Call members flags changed to: %d", priv->flags);

  g_signal_emit (self, signals[FLAGS_CHANGED], 0, priv->flags);
}

void
gabble_call_member_set_session (GabbleCallMember *self,
    GabbleJingleSession *session)
{
  GabbleCallMemberPrivate *priv = self->priv;
  GList *c;

  g_assert (priv->session == NULL);
  priv->session = g_object_ref (session);

  for (c = gabble_jingle_session_get_contents (session) ;
      c != NULL; c = g_list_next (c))
    {
      GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (c->data);
      GabbleCallMemberContent *mcontent;

      mcontent = gabble_call_member_content_from_jingle_content (content,
        self);

      priv->contents = g_list_append (priv->contents, mcontent);
      g_signal_emit (self, signals[CONTENT_ADDED], 0, mcontent);


      if (priv->transport_ns == NULL)
        {
          g_object_get (content, "transport-ns",
            &priv->transport_ns,
            NULL);
        }
    }

  gabble_signal_connect_weak (priv->session, "remote-state-changed",
    G_CALLBACK (remote_state_changed_cb), G_OBJECT (self));
}

GabbleJingleSession *
gabble_call_member_get_session (GabbleCallMember *self)
{
  return self->priv->session;
}

GabbleCallMemberFlags
gabble_call_member_get_flags (GabbleCallMember *self)
{
  return self->priv->flags;
}

TpHandle gabble_call_member_get_handle (
    GabbleCallMember *self)
{
  return self->priv->target;
}

GList *
gabble_call_member_get_contents (GabbleCallMember *self)
{
  GabbleCallMemberPrivate *priv = self->priv;

  return priv->contents;
}

GabbleCallMemberContent *
gabble_call_member_ensure_content (GabbleCallMember *self,
  const gchar *name,
  JingleMediaType mtype)
{
  GabbleCallMemberPrivate *priv = self->priv;
  GList *l;
  GabbleCallMemberContent *content = NULL;

  for (l = priv->contents ; l != NULL; l = g_list_next (l))
    {
      content = GABBLE_CALL_MEMBER_CONTENT (l->data);

      if (gabble_call_member_content_get_media_type (content) == mtype &&
          !tp_strdiff (gabble_call_member_content_get_name (content), name))
        break;
    }

  if (content == NULL)
    {
      content = gabble_call_member_content_new (name, mtype, self);
      priv->contents = g_list_prepend (priv->contents, content);
      g_signal_emit (self, signals[CONTENT_ADDED], 0, content);
    }

  return content;
}

GabbleCallMemberContent *
gabble_call_member_create_content (GabbleCallMember *self,
  const gchar *name,
  JingleMediaType mtype,
  GError **error)
{
  GabbleCallMemberPrivate *priv = self->priv;
  const gchar *content_ns;
  GabbleJingleContent *c;
  GabbleCallMemberContent *content;
  const gchar *peer_resource;

  g_assert (priv->session != NULL);

  peer_resource = gabble_jingle_session_get_peer_resource (priv->session);

  if (peer_resource != NULL)
    DEBUG ("existing call, using peer resource %s", peer_resource);
  else
    DEBUG ("existing call, using bare JID");

  content_ns = jingle_pick_best_content_type (priv->call->conn,
    priv->target,
    peer_resource, mtype);

  if (content_ns == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "Content type %d not available for this resource", mtype);
      return NULL;
    }

  DEBUG ("Creating new jingle content with ns %s : %s",
    content_ns, priv->transport_ns);

  c = gabble_jingle_session_add_content (priv->session,
      mtype, name, content_ns, priv->transport_ns);

  g_assert (c != NULL);

  content = gabble_call_member_content_from_jingle_content (c, self);

  priv->contents = g_list_append (priv->contents, content);
  g_signal_emit (self, signals[CONTENT_ADDED], 0, content);

  return content;
}

gboolean
gabble_call_member_start_session (GabbleCallMember *self,
    const gchar *audio_name,
    const gchar *video_name,
    GError **error)
{
  GabbleCallMemberPrivate *priv = self->priv;
  const gchar *resource;
  JingleDialect dialect;
  const gchar *transport;
  GabbleJingleSession *session;

  /* FIXME might need to wait on capabilities, also don't need transport
   * and dialect already */
  if (!jingle_pick_best_resource (priv->call->conn,
      priv->call->target, audio_name != NULL, video_name != NULL,
      &transport, &dialect, &resource))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_CAPABLE,
        "member does not have the desired audio/video capabilities");
      return FALSE;
    }

  session = gabble_jingle_factory_create_session (
        priv->call->conn->jingle_factory,
        priv->target, resource, FALSE);

  gabble_call_member_set_session (self, session);
  g_object_set (session, "dialect", dialect, NULL);

  priv->transport_ns = g_strdup (transport);

  if (audio_name != NULL)
    gabble_call_member_create_content (self, audio_name,
      JINGLE_MEDIA_TYPE_AUDIO, NULL);

  if (video_name != NULL)
    gabble_call_member_create_content (self, video_name,
      JINGLE_MEDIA_TYPE_VIDEO, NULL);

  return TRUE;
}
