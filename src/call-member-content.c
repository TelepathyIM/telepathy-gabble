/*
 * call-member-content.c - Source for GabbleCallMemberContent
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "call-member.h"
#include "call-member-content.h"
#include "jingle-media-rtp.h"
#include "util.h"
#include "namespaces.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

G_DEFINE_TYPE(GabbleCallMemberContent,
  gabble_call_member_content, G_TYPE_OBJECT)


/* properties */
enum {
  PROP_JINGLE_CONTENT = 1,
  PROP_CONTENT_NAME,
  PROP_MEDIA_TYPE,
  PROP_MEMBER
};

/* signal enum */
enum
{
    CODECS_CHANGED,
    GOT_JINGLE_CONTENT,
    REMOVED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
struct _GabbleCallMemberContentPrivate
{
  gboolean dispose_has_run;

  GabbleCallMember *member;

  GabbleJingleContent *jingle_content;
  gchar *name;
  JingleMediaType media_type;

  GList *remote_codecs;
  gboolean removed;
};

#define GABBLE_CALL_MEMBER_CONTENT_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CALL_MEMBER_CONTENT, \
    GabbleCallMemberContentPrivate))

static void
gabble_call_member_content_init (GabbleCallMemberContent *self)
{
  GabbleCallMemberContentPrivate *priv =
    GABBLE_CALL_MEMBER_CONTENT_GET_PRIVATE (self);

  self->priv = priv;
}

static void
gabble_call_member_content_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallMemberContent *self = GABBLE_CALL_MEMBER_CONTENT (object);
  GabbleCallMemberContentPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_MEMBER:
        g_value_set_object (value, priv->member);
        break;
      case PROP_JINGLE_CONTENT:
        g_value_set_object (value, priv->jingle_content);
        break;
      case PROP_CONTENT_NAME:
        g_value_set_string (value, priv->name);
        break;
      case PROP_MEDIA_TYPE:
        g_value_set_uint (value, priv->media_type);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_member_content_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallMemberContent *self = GABBLE_CALL_MEMBER_CONTENT (object);
  GabbleCallMemberContentPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_JINGLE_CONTENT:
        gabble_call_member_content_set_jingle_content (self,
          g_value_get_object (value));
        break;
      case PROP_MEMBER:
        priv->member = g_value_get_object (value);
        break;
      case PROP_CONTENT_NAME:
        priv->name = g_value_dup_string (value);
        g_assert (priv->name != NULL);
        break;
      case PROP_MEDIA_TYPE:
        priv->media_type = g_value_get_uint (value);
        g_assert (priv->media_type != JINGLE_MEDIA_TYPE_NONE);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void gabble_call_member_content_dispose (GObject *object);
static void gabble_call_member_content_finalize (GObject *object);

void
gabble_call_member_content_add_to_session (GabbleCallMemberContent *self)
{
  GabbleCallMemberContentPrivate *priv = self->priv;
  const gchar *content_ns;
  GabbleJingleSession *session;
  GabbleJingleContent *content;
  const gchar *peer_resource;
  const gchar *transport_ns;


  if (priv->jingle_content != NULL)
    return;

  DEBUG ("Session set for: %s (current jingle %p)",
    priv->name, priv->jingle_content);

  session = gabble_call_member_get_session (priv->member);
  transport_ns = gabble_call_member_get_transport_ns (priv->member);
  content_ns = NS_JINGLE_RTP;

  g_assert (session != NULL);

  peer_resource = gabble_jingle_session_get_peer_resource (session);

  if (peer_resource != NULL)
    DEBUG ("existing call, using peer resource %s", peer_resource);
  else
    DEBUG ("existing call, using bare JID");

  DEBUG ("Creating new jingle content with ns %s : %s",
    content_ns, transport_ns);

  content = gabble_jingle_session_add_content (session,
      priv->media_type, JINGLE_CONTENT_SENDERS_BOTH,
      priv->name, content_ns, transport_ns);

  gabble_call_member_content_set_jingle_content (self, content);
}

static void
member_got_session_cb (GabbleCallMember *member,
  GParamSpec *param, gpointer user_data)
{
  gabble_call_member_content_add_to_session (
      GABBLE_CALL_MEMBER_CONTENT (user_data));
}

static void
gabble_call_member_content_constructed (GObject *obj)
{
  GabbleCallMemberContent *self = GABBLE_CALL_MEMBER_CONTENT (obj);
  GabbleCallMemberContentPrivate *priv = self->priv;

  gabble_signal_connect_weak (priv->member, "notify::session",
    G_CALLBACK (member_got_session_cb), G_OBJECT (self));

  if (G_OBJECT_CLASS (gabble_call_member_content_parent_class)->constructed
      != NULL)
    G_OBJECT_CLASS (
      gabble_call_member_content_parent_class)->constructed (obj);
}

static void
gabble_call_member_content_class_init (
  GabbleCallMemberContentClass *gabble_call_member_content_class)
{
  GObjectClass *object_class =
      G_OBJECT_CLASS (gabble_call_member_content_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_call_member_content_class,
    sizeof (GabbleCallMemberContentPrivate));

  object_class->dispose = gabble_call_member_content_dispose;
  object_class->finalize = gabble_call_member_content_finalize;

  object_class->get_property = gabble_call_member_content_get_property;
  object_class->set_property = gabble_call_member_content_set_property;
  object_class->constructed = gabble_call_member_content_constructed;

  param_spec = g_param_spec_string ("name", "Name",
      "The name of this jingle content",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTENT_NAME,
    param_spec);

  param_spec = g_param_spec_uint ("media-type", "MediaType",
      "The media type of this jingle content",
      JINGLE_MEDIA_TYPE_NONE,
      JINGLE_MEDIA_TYPE_VIDEO,
      JINGLE_MEDIA_TYPE_NONE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE,
    param_spec);

  param_spec = g_param_spec_object ("jingle-content", "JingleContent",
      "The jingle content corresponding to this members content",
      GABBLE_TYPE_JINGLE_CONTENT,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JINGLE_CONTENT,
    param_spec);

  param_spec = g_param_spec_object ("member", "CallMember",
      "The call member that has this as a content",
      GABBLE_TYPE_CALL_MEMBER,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEMBER,
    param_spec);

  signals[CODECS_CHANGED] = g_signal_new ("codecs-changed",
      G_OBJECT_CLASS_TYPE (gabble_call_member_content_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[GOT_JINGLE_CONTENT] = g_signal_new ("got-jingle-content",
      G_OBJECT_CLASS_TYPE (gabble_call_member_content_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[REMOVED] = g_signal_new ("removed",
      G_OBJECT_CLASS_TYPE (gabble_call_member_content_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

void
gabble_call_member_content_dispose (GObject *object)
{
  GabbleCallMemberContent *self = GABBLE_CALL_MEMBER_CONTENT (object);
  GabbleCallMemberContentPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->jingle_content);

  if (G_OBJECT_CLASS (gabble_call_member_content_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_member_content_parent_class)->dispose (object);
}

void
gabble_call_member_content_finalize (GObject *object)
{
  GabbleCallMemberContent *self = GABBLE_CALL_MEMBER_CONTENT (object);
  GabbleCallMemberContentPrivate *priv = self->priv;

  g_free (priv->name);

  G_OBJECT_CLASS (gabble_call_member_content_parent_class)->finalize (object);
}

JingleMediaType
gabble_call_member_content_get_media_type (GabbleCallMemberContent *self)
{
  return self->priv->media_type;
}

const gchar *
gabble_call_member_content_get_name (GabbleCallMemberContent *self)
{
  return self->priv->name;
}

GabbleJingleContent *
gabble_call_member_content_get_jingle_content (
    GabbleCallMemberContent *self)
{
  return self->priv->jingle_content;
}

GabbleCallMemberContent *
gabble_call_member_content_new (const gchar *name,
    JingleMediaType type,
    GabbleCallMember *member)
{
  return GABBLE_CALL_MEMBER_CONTENT (g_object_new (
    GABBLE_TYPE_CALL_MEMBER_CONTENT,
    "name", name,
    "media-type", type,
    "member", member,
    NULL));
}

static void
call_member_content_jingle_removed_cb (GabbleJingleContent *jingle_content,
    GabbleCallMemberContent *content)
{
  if (!content->priv->removed)
    {
      content->priv->removed = TRUE;
      g_signal_emit (content, signals[REMOVED], 0);
    }
}

static void
call_member_content_jingle_media_description_cb (GabbleJingleMediaRtp *media,
    JingleMediaDescription *md,
    gpointer user_data)
{
  GabbleCallMemberContent *self = GABBLE_CALL_MEMBER_CONTENT (user_data);

  DEBUG ("New codecs from jingle");

  g_signal_emit (self, signals[CODECS_CHANGED], 0);
}

GabbleCallMemberContent *
gabble_call_member_content_from_jingle_content (
  GabbleJingleContent *jingle_content,
  GabbleCallMember *member)
{
  GabbleCallMemberContent *content;
  gchar *name;
  JingleMediaType mtype;

  g_object_get (jingle_content,
    "name", &name,
    "media-type", &mtype,
    NULL);

  content = gabble_call_member_content_new (name, mtype, member);

  gabble_call_member_content_set_jingle_content (content, jingle_content);

  g_free (name);

  return content;
}

gboolean
gabble_call_member_content_has_jingle_content (
    GabbleCallMemberContent *self)
{
  return self->priv->jingle_content != NULL;
}

GList *
gabble_call_member_content_get_remote_codecs (GabbleCallMemberContent *self)
{
  GList *jcodecs = NULL;

  if (self->priv->jingle_content != NULL)
    {
      JingleMediaDescription *md;
      md = gabble_jingle_media_rtp_get_remote_media_description (
          GABBLE_JINGLE_MEDIA_RTP (self->priv->jingle_content));
      if (md != NULL)
        jcodecs = md->codecs;
    }

  return jcodecs != NULL ? jcodecs : self->priv->remote_codecs;
}

void
gabble_call_member_content_set_remote_codecs (GabbleCallMemberContent *self,
    GList *codecs)
{
  GabbleCallMemberContentPrivate *priv = self->priv;

  DEBUG ("New codecs set directly on the member");

  if (priv->remote_codecs != NULL)
    {
      GList *changed = NULL;

      if (!jingle_media_rtp_compare_codecs (priv->remote_codecs, codecs,
            &changed, NULL) || changed == NULL)
        return;

      g_list_free (changed);
    }

  jingle_media_rtp_free_codecs (priv->remote_codecs);
  priv->remote_codecs = codecs;


  g_signal_emit (self, signals[CODECS_CHANGED], 0);
}

GabbleCallMember *
gabble_call_member_content_get_member (GabbleCallMemberContent *self)
{
  return self->priv->member;
}

void
gabble_call_member_content_set_jingle_content (GabbleCallMemberContent *self,
    GabbleJingleContent *content)
{
  g_assert (self->priv->jingle_content == NULL);

  if (content == NULL)
    return;

  self->priv->jingle_content = g_object_ref (content);

  gabble_signal_connect_weak (content, "removed",
      G_CALLBACK (call_member_content_jingle_removed_cb), G_OBJECT (self));
  gabble_signal_connect_weak (content, "remote-media-description",
    G_CALLBACK (call_member_content_jingle_media_description_cb),
      G_OBJECT (self));

  g_signal_emit (self, signals[GOT_JINGLE_CONTENT], 0);
}

void
gabble_call_member_content_remove (GabbleCallMemberContent *self)
{
  GabbleCallMemberContentPrivate *priv = self->priv;

  if (priv->removed)
    return;

  priv->removed = TRUE;

  g_object_ref (self);
  /* Remove ourselves from the sesison */
  if (priv->jingle_content != NULL)
      gabble_jingle_session_remove_content (priv->jingle_content->session,
          priv->jingle_content);

  g_signal_emit (self, signals[REMOVED], 0);
  g_object_unref (self);
}
