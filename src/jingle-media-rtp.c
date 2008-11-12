/*
 * jingle-media-rtp.c - Source for GabbleJingleMediaRtp
 *
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

/* Media/RTP content type deals with audio/video content, ie. jingle calls. It
 * supports standard Jingle drafts (v0.15, v0.26) and Google's jingle variants
 * (libjingle 0.3/0.4). */

#include "jingle-media-rtp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-content.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "namespaces.h"
#include "util.h"

G_DEFINE_TYPE (GabbleJingleMediaRtp,
    gabble_jingle_media_rtp, GABBLE_TYPE_JINGLE_CONTENT);

/* signal enum */
enum
{
  REMOTE_CODECS,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MEDIA_TYPE = 1,
  LAST_PROPERTY
};

typedef enum {
  JINGLE_MEDIA_PROFILE_RTP_AVP,
} JingleMediaProfile;

struct _GabbleJingleMediaRtpPrivate
{
  GList *local_codecs;
  GList *remote_codecs;
  JingleMediaType media_type;
  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE(o) ((o)->priv)

static void
gabble_jingle_media_rtp_init (GabbleJingleMediaRtp *obj)
{
  GabbleJingleMediaRtpPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_MEDIA_RTP,
         GabbleJingleMediaRtpPrivate);
  obj->priv = priv;
  priv->dispose_has_run = FALSE;
}

JingleCodec *
jingle_media_rtp_codec_new (guint id, const gchar *name,
    guint clockrate, guint channels, GHashTable *params)
{
  JingleCodec *p = g_slice_new0 (JingleCodec);

  p->id = id;
  p->name = g_strdup (name);
  p->clockrate = clockrate;
  p->channels = channels;

  if (params != NULL)
      p->params = params;
  else
      p->params = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

  return p;
}

void
jingle_media_rtp_codec_free (JingleCodec *p)
{
  g_hash_table_destroy  (p->params);
  g_free (p->name);
  g_slice_free (JingleCodec, p);
}

void
jingle_media_rtp_free_codecs (GList *codecs)
{
  while (codecs != NULL)
    {
      JingleCodec *p = (JingleCodec *) codecs->data;
      jingle_media_rtp_codec_free (p);
      codecs = g_list_remove (codecs, p);
    }
}

static void
gabble_jingle_media_rtp_dispose (GObject *object)
{
  GabbleJingleMediaRtp *trans = GABBLE_JINGLE_MEDIA_RTP (object);
  GabbleJingleMediaRtpPrivate *priv = GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE (trans);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  jingle_media_rtp_free_codecs (priv->remote_codecs);
  priv->remote_codecs = NULL;

  jingle_media_rtp_free_codecs (priv->local_codecs);
  priv->local_codecs = NULL;

  if (G_OBJECT_CLASS (gabble_jingle_media_rtp_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_media_rtp_parent_class)->dispose (object);
}

static void
gabble_jingle_media_rtp_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleMediaRtp *trans = GABBLE_JINGLE_MEDIA_RTP (object);
  GabbleJingleMediaRtpPrivate *priv = GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE (trans);

  switch (property_id) {
    case PROP_MEDIA_TYPE:
      g_value_set_uint (value, priv->media_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_media_rtp_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
  GabbleJingleMediaRtp *trans = GABBLE_JINGLE_MEDIA_RTP (object);
  GabbleJingleMediaRtpPrivate *priv =
      GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE (trans);

  switch (property_id) {
    case PROP_MEDIA_TYPE:
      priv->media_type = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void parse_description (GabbleJingleContent *content,
    LmMessageNode *desc_node, GError **error);
static void produce_description (GabbleJingleContent *obj,
    LmMessageNode *content_node);

static void
gabble_jingle_media_rtp_class_init (GabbleJingleMediaRtpClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GabbleJingleContentClass *content_class = GABBLE_JINGLE_CONTENT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleMediaRtpPrivate));

  object_class->get_property = gabble_jingle_media_rtp_get_property;
  object_class->set_property = gabble_jingle_media_rtp_set_property;
  object_class->dispose = gabble_jingle_media_rtp_dispose;

  content_class->parse_description = parse_description;
  content_class->produce_description = produce_description;

  param_spec = g_param_spec_uint ("media-type", "RTP media type",
      "Media type.",
      JINGLE_MEDIA_TYPE_NONE, G_MAXUINT32, JINGLE_MEDIA_TYPE_NONE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE, param_spec);

  /* signal definitions */

  signals[REMOTE_CODECS] = g_signal_new ("remote-codecs",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1, G_TYPE_POINTER);
}

#define SET_BAD_REQ(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST, txt)
#define SET_OUT_ORDER(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_JINGLE_OUT_OF_ORDER, txt)
#define SET_CONFLICT(txt...) g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT, txt)

static const gchar *video_codec_params[] = {
  "x", "y", "width", "height", "layer", "transparent", NULL,
};

static void
parse_description (GabbleJingleContent *content,
    LmMessageNode *desc_node, GError **error)
{
  GabbleJingleMediaRtp *self = GABBLE_JINGLE_MEDIA_RTP (content);
  GabbleJingleMediaRtpPrivate *priv = GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE (self);
  JingleMediaType mtype = JINGLE_MEDIA_TYPE_NONE;
  gboolean google_mode = FALSE;
  GList *codecs = NULL;
  LmMessageNode *node;

  if (lm_message_node_has_namespace (desc_node, NS_JINGLE_RTP, NULL))
    {
      const gchar *type = lm_message_node_get_attribute (desc_node, "media");

      if (type == NULL)
        {
          SET_BAD_REQ("missing required media type attribute");
          return;
        }

      if (!tp_strdiff (type, "audio"))
          mtype = JINGLE_MEDIA_TYPE_AUDIO;
      else if (!tp_strdiff (type, "video"))
          mtype = JINGLE_MEDIA_TYPE_VIDEO;
      else
        {
          SET_BAD_REQ("unknown media type %s", type);
          return;
        }
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_AUDIO, NULL))
    {
      mtype = JINGLE_MEDIA_TYPE_AUDIO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_VIDEO, NULL))
    {
      mtype = JINGLE_MEDIA_TYPE_VIDEO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_GOOGLE_SESSION_PHONE, NULL))
    {
      mtype = JINGLE_MEDIA_TYPE_AUDIO;
      google_mode = TRUE;
    }
  else
    {
      /* If we get here, namespace in use is not one of
       * namespaces we signed up with, so obviously a bug
       * somewhere. */
      g_assert_not_reached ();
    }

  DEBUG ("detected media type %u", mtype);

  /* FIXME: we ignore "profile" attribute */

  for (node = desc_node->children; node; node = node->next)
    {
      JingleCodec *p;
      const char *txt;
      guchar id;
      const gchar *name;
      guint clockrate, channels;
      guint i;

      if (tp_strdiff (node->name, "payload-type"))
          continue;

      txt = lm_message_node_get_attribute (node, "id");
      if (txt == NULL)
          break;

      id = atoi (txt);

      name = lm_message_node_get_attribute (node, "name");
      if (name == NULL)
          name = "";

      /* xep-0167 v0.22, gtalk libjingle 0.3/0.4 use "clockrate" */
      txt = lm_message_node_get_attribute (node, "clockrate");
      /* older jingle rtp used "rate" ? */
      if (txt == NULL)
          txt = lm_message_node_get_attribute (node, "rate");

      if (txt != NULL)
        {
          clockrate = atoi (txt);
        }
      else
        {
          clockrate = 0;
        }

      txt = lm_message_node_get_attribute (node, "channels");
      if (txt != NULL)
        {
          channels = atoi (txt);
        }
      else
        {
          channels = 1;
        }

      p = jingle_media_rtp_codec_new (id, name, clockrate, channels, NULL);

      for (i = 0; video_codec_params[i] != NULL; i++)
        {
          txt = lm_message_node_get_attribute (node, video_codec_params[i]);
          if (txt != NULL)
              g_hash_table_insert (p->params, (gpointer) video_codec_params[i],
                  g_strdup (txt));
        }

      DEBUG ("new remote codec: id = %u, name = %s, clockrate = %u, channels = %u",
          p->id, p->name, p->clockrate, p->channels);

      codecs = g_list_append (codecs, p);
    }

  if (node != NULL)
    {
      /* rollback these */
      jingle_media_rtp_free_codecs (codecs);
      SET_BAD_REQ ("invalid payload");
      return;
    }

  priv->media_type = mtype;

  DEBUG ("emitting remote-codecs signal");
  g_signal_emit (self, signals[REMOTE_CODECS], 0, codecs);

  /* append them to the known remote codecs */
  priv->remote_codecs = g_list_concat (priv->remote_codecs, codecs);
}

static void
_produce_extra_param (gpointer key, gpointer value, gpointer user_data)
{
  lm_message_node_set_attribute ((LmMessageNode *) user_data,
      (gchar *) key, (gchar *) value);
}

static void
produce_description (GabbleJingleContent *obj, LmMessageNode *content_node)
{
  GabbleJingleMediaRtp *desc =
    GABBLE_JINGLE_MEDIA_RTP (obj);
  GabbleJingleSession *sess;
  GabbleJingleMediaRtpPrivate *priv =
    GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE (desc);
  LmMessageNode *desc_node;
  GList *li;
  JingleDialect dialect;
  const gchar *xmlns = NULL;

  g_object_get (obj, "session", &sess, NULL);
  g_object_get (sess, "dialect", &dialect, NULL);

  desc_node = lm_message_node_add_child (content_node, "description", NULL);

  switch (dialect)
    {
      case JINGLE_DIALECT_GTALK3:
      case JINGLE_DIALECT_GTALK4:
        g_assert (priv->media_type == JINGLE_MEDIA_TYPE_AUDIO);
        xmlns = NS_GOOGLE_SESSION_PHONE;
        break;
      case JINGLE_DIALECT_V015:
        if (priv->media_type == JINGLE_MEDIA_TYPE_AUDIO)
            xmlns = NS_JINGLE_DESCRIPTION_AUDIO;
        else if (priv->media_type == JINGLE_MEDIA_TYPE_VIDEO)
            xmlns = NS_JINGLE_DESCRIPTION_VIDEO;
        else
          {
            DEBUG ("unknown media type %u", priv->media_type);
            xmlns = "";
          }
        break;
      default:
        xmlns = NS_JINGLE_RTP;
        if (priv->media_type == JINGLE_MEDIA_TYPE_AUDIO)
            lm_message_node_set_attribute (desc_node, "media", "audio");
        else if (priv->media_type == JINGLE_MEDIA_TYPE_VIDEO)
            lm_message_node_set_attribute (desc_node, "media", "video");
        else
            g_assert_not_reached ();
        break;
    }

  lm_message_node_set_attribute (desc_node, "xmlns", xmlns);

  for (li = priv->local_codecs; li; li = li->next)
    {
      LmMessageNode *pt_node;
      gchar buf[16];
      JingleCodec *p = li->data;

      pt_node = lm_message_node_add_child (desc_node, "payload-type", NULL);

      /* id: required */
      sprintf (buf, "%d", p->id);
      lm_message_node_set_attribute (pt_node, "id", buf);

      /* name: optional */
      if (*p->name != '\0')
        {
          lm_message_node_set_attribute (pt_node, "name", p->name);
        }

      /* clock rate: optional */
      if (p->clockrate != 0)
        {
          const gchar *attname = "clockrate";

          if (dialect == JINGLE_DIALECT_V015)
              attname = "rate";

          sprintf (buf, "%u", p->clockrate);
          lm_message_node_set_attribute (pt_node, attname, buf);
        }

      if (p->channels != 0)
        {
          sprintf (buf, "%u", p->channels);
          lm_message_node_set_attribute (pt_node, "channels", buf);
        }

      if (p->params != NULL)
        {
          g_hash_table_foreach (p->params, _produce_extra_param, pt_node);
        }
    }
}

/* Takes in a list of slice-allocated JingleCodec structs */
void
jingle_media_rtp_set_local_codecs (GabbleJingleMediaRtp *self, GList *codecs)
{
  GabbleJingleMediaRtpPrivate *priv =
    GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE (self);

  DEBUG ("adding new local codecs");

  priv->local_codecs = g_list_concat (priv->local_codecs, codecs);

  _gabble_jingle_content_set_media_ready (GABBLE_JINGLE_CONTENT (self));
}

void
jingle_media_rtp_register (GabbleJingleFactory *factory)
{
  /* Current (v0.25) Jingle draft URI */
  gabble_jingle_factory_register_content_type (factory,
      NS_JINGLE_RTP, GABBLE_TYPE_JINGLE_MEDIA_RTP);

  /* Old Jingle audio/video namespaces */
  gabble_jingle_factory_register_content_type (factory,
      NS_JINGLE_DESCRIPTION_AUDIO,
      GABBLE_TYPE_JINGLE_MEDIA_RTP);

  gabble_jingle_factory_register_content_type (factory,
      NS_JINGLE_DESCRIPTION_VIDEO,
      GABBLE_TYPE_JINGLE_MEDIA_RTP);

  /* GTalk audio call namespace */
  gabble_jingle_factory_register_content_type (factory,
      NS_GOOGLE_SESSION_PHONE,
      GABBLE_TYPE_JINGLE_MEDIA_RTP);
}

/* We can't get remote codecs when they're signalled, because
 * the signal is emitted immediately upon JingleContent creation,
 * and parsing, which is before a corresponding MediaStream is
 * created. */
GList *
gabble_jingle_media_rtp_get_remote_codecs (GabbleJingleMediaRtp *self)
{
  GabbleJingleMediaRtpPrivate *priv =
    GABBLE_JINGLE_MEDIA_RTP_GET_PRIVATE (self);

  return priv->remote_codecs;
}

