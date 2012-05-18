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

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-content.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "jingle-transport-google.h"

G_DEFINE_TYPE (GabbleJingleMediaRtp,
    gabble_jingle_media_rtp, GABBLE_TYPE_JINGLE_CONTENT);

/* signal enum */
enum
{
  REMOTE_MEDIA_DESCRIPTION,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MEDIA_TYPE = 1,
  PROP_REMOTE_MUTE,
  LAST_PROPERTY
};

typedef enum {
  JINGLE_MEDIA_PROFILE_RTP_AVP,
} JingleMediaProfile;

struct _GabbleJingleMediaRtpPrivate
{
  JingleMediaDescription *local_media_description;

  /* Holds (JingleCodec *)'s borrowed from local_media_description,
   * namely codecs which have changed from local_media_description's
   * previous value. Since the contents are borrowed, this must be
   * freed with g_list_free, not jingle_media_rtp_free_codecs().
   */
  GList *local_codec_updates;

  JingleMediaDescription *remote_media_description;
  JingleMediaType media_type;
  gboolean remote_mute;

  gboolean has_rtcp_fb;
  gboolean has_rtp_hdrext;

  gboolean dispose_has_run;
};

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
  p->trr_int = G_MAXUINT;

  if (params != NULL)
    {
      g_hash_table_ref (params);
      p->params = params;
    }
  else
    {
      p->params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
          g_free);
    }

  return p;
}


static GList *
jingle_feedback_message_list_copy (GList *fbs)
{
  GQueue new = G_QUEUE_INIT;
  GList *li;

  for (li = fbs; li; li = li->next)
    {
      JingleFeedbackMessage *fb = li->data;

      g_queue_push_tail (&new, jingle_feedback_message_new (fb->type,
              fb->subtype));
    }

  return new.head;
}

static void
jingle_feedback_message_list_free (GList *fbs)
{
  while (fbs != NULL)
    {
      jingle_feedback_message_free (fbs->data);
      fbs = g_list_delete_link (fbs, fbs);
    }
}

void
jingle_media_rtp_codec_free (JingleCodec *p)
{
  g_hash_table_unref (p->params);
  g_free (p->name);
  jingle_feedback_message_list_free (p->feedback_msgs);
  g_slice_free (JingleCodec, p);
}

static void
add_codec_to_table (JingleCodec *codec,
                    GHashTable *table)
{
  g_hash_table_insert (table, GUINT_TO_POINTER ((guint) codec->id), codec);
}

static GHashTable *
build_codec_table (GList *codecs)
{
  GHashTable *table = g_hash_table_new (NULL, NULL);

  g_list_foreach (codecs, (GFunc) add_codec_to_table, table);
  return table;
}

GList *
jingle_media_rtp_copy_codecs (GList *codecs)
{
  GList *ret = NULL, *l;

  for (l = codecs; l != NULL; l = g_list_next (l))
    {
      JingleCodec *c = l->data;
      JingleCodec *newc =  jingle_media_rtp_codec_new (c->id,
          c->name, c->clockrate, c->channels, c->params);
      newc->trr_int = c->trr_int;
      ret = g_list_append (ret, newc);
    }

  return ret;
}

void
jingle_media_rtp_free_codecs (GList *codecs)
{
  while (codecs != NULL)
    {
      jingle_media_rtp_codec_free (codecs->data);
      codecs = g_list_delete_link (codecs, codecs);
    }
}

static void
gabble_jingle_media_rtp_dispose (GObject *object)
{
  GabbleJingleMediaRtp *trans = GABBLE_JINGLE_MEDIA_RTP (object);
  GabbleJingleMediaRtpPrivate *priv = trans->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  if (priv->remote_media_description != NULL)
    jingle_media_description_free (priv->remote_media_description);
  priv->remote_media_description = NULL;

  if (priv->local_media_description != NULL)
    jingle_media_description_free (priv->local_media_description);
  priv->local_media_description = NULL;

  if (priv->local_codec_updates != NULL)
    {
      DEBUG ("We have an unsent codec parameter update! Weird.");

      g_list_free (priv->local_codec_updates);
      priv->local_codec_updates = NULL;
    }

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
  GabbleJingleMediaRtpPrivate *priv = trans->priv;

  switch (property_id) {
    case PROP_MEDIA_TYPE:
      g_value_set_uint (value, priv->media_type);
      break;
    case PROP_REMOTE_MUTE:
      g_value_set_boolean (value, priv->remote_mute);
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
  GabbleJingleMediaRtpPrivate *priv = trans->priv;

  switch (property_id) {
    case PROP_MEDIA_TYPE:
      priv->media_type = g_value_get_uint (value);
      break;
    case PROP_REMOTE_MUTE:
      priv->remote_mute = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void parse_description (GabbleJingleContent *content,
    WockyNode *desc_node, GError **error);
static void produce_description (GabbleJingleContent *obj,
    WockyNode *content_node);
static void transport_created (GabbleJingleContent *obj,
    GabbleJingleTransportIface *transport);

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
  content_class->transport_created = transport_created;

  param_spec = g_param_spec_uint ("media-type", "RTP media type",
      "Media type.",
      JINGLE_MEDIA_TYPE_NONE, G_MAXUINT32, JINGLE_MEDIA_TYPE_NONE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE, param_spec);

  param_spec = g_param_spec_boolean ("remote-mute", "Remote mute",
      "TRUE if the peer has muted this stream", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_MUTE, param_spec);

  /* signal definitions */

  signals[REMOTE_MEDIA_DESCRIPTION] = g_signal_new ("remote-media-description",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void transport_created (GabbleJingleContent *content,
    GabbleJingleTransportIface *transport)
{
  GabbleJingleMediaRtp *self = GABBLE_JINGLE_MEDIA_RTP (content);
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  GabbleJingleTransportGoogle *gtrans = NULL;
  JingleDialect dialect;

  if (GABBLE_IS_JINGLE_TRANSPORT_GOOGLE (transport))
    {
      gtrans = GABBLE_JINGLE_TRANSPORT_GOOGLE (transport);
      dialect = gabble_jingle_session_get_dialect (content->session);

      if (priv->media_type == JINGLE_MEDIA_TYPE_VIDEO &&
          (JINGLE_IS_GOOGLE_DIALECT (dialect) ||
           gabble_jingle_session_peer_has_cap (content->session,
               QUIRK_GOOGLE_WEBMAIL_CLIENT) ||
           gabble_jingle_session_peer_has_cap (content->session,
               QUIRK_ANDROID_GTALK_CLIENT)))
        {
          jingle_transport_google_set_component_name (gtrans, "video_rtp", 1);
          jingle_transport_google_set_component_name (gtrans, "video_rtcp", 2);
        }
      else
        {
          jingle_transport_google_set_component_name (gtrans, "rtp", 1);
          jingle_transport_google_set_component_name (gtrans, "rtcp", 2);
        }
    }
}


static JingleMediaType
extract_media_type (WockyNode *desc_node,
                    GError **error)
{
  if (wocky_node_has_ns (desc_node, NS_JINGLE_RTP))
    {
      const gchar *type = wocky_node_get_attribute (desc_node, "media");

      if (type == NULL)
        {
          g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
              "missing required media type attribute");
          return JINGLE_MEDIA_TYPE_NONE;
        }

      if (!wocky_strdiff (type, "audio"))
          return JINGLE_MEDIA_TYPE_AUDIO;

      if (!wocky_strdiff (type, "video"))
        return JINGLE_MEDIA_TYPE_VIDEO;

      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "unknown media type %s", type);
      return JINGLE_MEDIA_TYPE_NONE;
    }

  if (wocky_node_has_ns (desc_node, NS_JINGLE_DESCRIPTION_AUDIO))
    return JINGLE_MEDIA_TYPE_AUDIO;

  if (wocky_node_has_ns (desc_node, NS_JINGLE_DESCRIPTION_VIDEO))
    return JINGLE_MEDIA_TYPE_VIDEO;

  if (wocky_node_has_ns (desc_node, NS_GOOGLE_SESSION_PHONE))
    return JINGLE_MEDIA_TYPE_AUDIO;

  if (wocky_node_has_ns (desc_node, NS_GOOGLE_SESSION_VIDEO))
    return JINGLE_MEDIA_TYPE_VIDEO;

  /* If we get here, namespace in use is not one of namespaces we signed up
   * with, so obviously a bug somewhere.
   */
  g_assert_not_reached ();
}

static JingleFeedbackMessage *
parse_rtcp_fb (GabbleJingleContent *content, WockyNode *node)
{
  const gchar *pt_ns = wocky_node_get_ns (node);
  const gchar *type;
  const gchar *subtype;

  if (wocky_strdiff (pt_ns, NS_JINGLE_RTCP_FB))
    return NULL;

  type = wocky_node_get_attribute (node, "type");
  if (type == NULL)
    return NULL;

  subtype = wocky_node_get_attribute (node, "subtype");

  /* This is optional, defaults to "" */
  if (subtype == NULL)
    subtype = "";

  return jingle_feedback_message_new (type, subtype);
}


/*
 * Returns G_MAXUINT on error
 */
static guint
parse_rtcp_fb_trr_int (GabbleJingleContent *content, WockyNode *node)
{
  const gchar *pt_ns = wocky_node_get_ns (node);
  const gchar *txt;
  guint trr_int;
  gchar *endptr = NULL;

  if (wocky_strdiff (pt_ns, NS_JINGLE_RTCP_FB))
    return G_MAXUINT;

  txt = wocky_node_get_attribute (node, "value");
  if (txt == NULL)
    return G_MAXUINT;

  trr_int = strtol (txt, &endptr, 10);
  if (endptr == NULL || endptr == txt)
    return G_MAXUINT;

  return trr_int;
}


/**
 * parse_payload_type:
 * @node: a <payload-type> node.
 *
 * Returns: a newly-allocated JingleCodec if parsing succeeds, or %NULL
 *          otherwise.
 */
static JingleCodec *
parse_payload_type (GabbleJingleContent *content,
    WockyNode *node)
{
  GabbleJingleMediaRtp *self = GABBLE_JINGLE_MEDIA_RTP (content);
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  JingleCodec *p;
  const char *txt;
  guint8 id;
  const gchar *name;
  guint clockrate = 0;
  guint channels = 0;
  WockyNode *param;
  WockyNodeIter i;

  txt = wocky_node_get_attribute (node, "id");
  if (txt == NULL)
    return NULL;

  id = atoi (txt);

  name = wocky_node_get_attribute (node, "name");
  if (name == NULL)
    name = "";

  /* xep-0167 v0.22, gtalk libjingle 0.3/0.4 use "clockrate" */
  txt = wocky_node_get_attribute (node, "clockrate");
  /* older jingle rtp used "rate" ? */
  if (txt == NULL)
    txt = wocky_node_get_attribute (node, "rate");

  if (txt != NULL)
    clockrate = atoi (txt);

  txt = wocky_node_get_attribute (node, "channels");
  if (txt != NULL)
    channels = atoi (txt);

  p = jingle_media_rtp_codec_new (id, name, clockrate, channels, NULL);

  wocky_node_iter_init (&i, node, NULL, NULL);
  while (wocky_node_iter_next (&i, &param))
    {
      if (!wocky_strdiff (param->name, "parameter"))
        {
          const gchar *param_name, *param_value;

          param_name = wocky_node_get_attribute (param, "name");
          param_value = wocky_node_get_attribute (param, "value");

          if (param_name == NULL || param_value == NULL)
            continue;

          g_hash_table_insert (p->params, g_strdup (param_name),
              g_strdup (param_value));
        }
      else if (!wocky_strdiff (param->name, "rtcp-fb"))
        {
          JingleFeedbackMessage *fb = parse_rtcp_fb (content, param);

          if (fb != NULL)
            {
              p->feedback_msgs = g_list_append (p->feedback_msgs, fb);
              priv->has_rtcp_fb = TRUE;
            }
        }
      else if (!wocky_strdiff (param->name,
              "rtcp-fb-trr-int"))
        {
          guint trr_int = parse_rtcp_fb_trr_int (content, param);

          if (trr_int != G_MAXUINT)
            {
              p->trr_int = trr_int;
              priv->has_rtcp_fb = TRUE;
            }
        }
    }

  DEBUG ("new remote codec: id = %u, name = %s, clockrate = %u, channels = %u",
      p->id, p->name, p->clockrate, p->channels);

  return p;
}

static JingleRtpHeaderExtension *
parse_rtp_header_extension (WockyNode *node)
{
  guint id;
  JingleContentSenders senders;
  const gchar *uri;
  const char *txt;

  txt = wocky_node_get_attribute (node, "id");
  if (txt == NULL)
    return NULL;

  id = atoi (txt);

  /* Only valid ranges are 1-256 and 4096-4351 */
  if ((id < 1 || id > 256) && (id < 4096 || id > 4351))
    return NULL;

  txt = wocky_node_get_attribute (node, "senders");

  if (txt == NULL || !g_ascii_strcasecmp (txt, "both"))
    senders = JINGLE_CONTENT_SENDERS_BOTH;
  else if (!g_ascii_strcasecmp (txt, "initiator"))
    senders = JINGLE_CONTENT_SENDERS_INITIATOR;
  else if (!g_ascii_strcasecmp (txt, "responder"))
    senders = JINGLE_CONTENT_SENDERS_RESPONDER;
  else
    return NULL;

  uri = wocky_node_get_attribute (node, "uri");

  if (uri == NULL)
    return NULL;

  return jingle_rtp_header_extension_new (id, senders, uri);
}


/**
 * codec_update_coherent:
 * @old_c: Gabble's old cache of the codec, or %NULL if it hasn't heard of it.
 * @new_c: the proposed update, whose id must equal that of @old_c if the
 *         latter is non-NULL.
 * @domain: the error domain to set @e to if necessary
 * @code: the error code to set @e to if necessary
 * @e: location to hold an error
 *
 * Compares @old_c and @new_c, which are assumed to have the same id, to check
 * that the name, clockrate and number of channels hasn't changed. If they
 * have, returns %FALSE and sets @e.
 */
static gboolean
codec_update_coherent (const JingleCodec *old_c,
                       const JingleCodec *new_c,
                       GError **e)
{
  const GQuark domain = WOCKY_XMPP_ERROR;
  const gint code = WOCKY_XMPP_ERROR_BAD_REQUEST;

  if (old_c == NULL)
    {
      g_set_error (e, domain, code, "Codec with id %u ('%s') unknown",
          new_c->id, new_c->name);
      return FALSE;
    }

  if (g_ascii_strcasecmp (new_c->name, old_c->name))
    {
      g_set_error (e, domain, code,
          "tried to change codec %u's name from %s to %s",
          new_c->id, old_c->name, new_c->name);
      return FALSE;
    }

  if (new_c->clockrate != old_c->clockrate)
    {
      g_set_error (e, domain, code,
          "tried to change codec %u (%s)'s clockrate from %u to %u",
          new_c->id, new_c->name, old_c->clockrate, new_c->clockrate);
      return FALSE;
    }

  if (old_c->channels != 0 &&
      new_c->channels != old_c->channels)
    {
      g_set_error (e, domain, code,
          "tried to change codec %u (%s)'s channels from %u to %u",
          new_c->id, new_c->name, new_c->channels, old_c->channels);
      return FALSE;
    }

  return TRUE;
}

static void
update_remote_media_description (GabbleJingleMediaRtp *self,
                                 JingleMediaDescription *new_media_description,
                                 GError **error)
{
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  GHashTable *rc = NULL;
  JingleCodec *old_c, *new_c;
  GList *l;
  GError *e = NULL;

  if (priv->remote_media_description == NULL)
    {
      priv->remote_media_description = new_media_description;
      new_media_description = NULL;
      goto out;
    }

  rc = build_codec_table (priv->remote_media_description->codecs);

  /* We already know some remote codecs, so this is just the other end updating
   * some parameters.
   */
  for (l = new_media_description->codecs; l != NULL; l = l->next)
    {
      new_c = l->data;
      old_c = g_hash_table_lookup (rc, GUINT_TO_POINTER ((guint) new_c->id));

      if (!codec_update_coherent (old_c, new_c, &e))
        goto out;
    }

  /* Okay, all the updates are cool. Let's switch the parameters around. */
  for (l = new_media_description->codecs; l != NULL; l = l->next)
    {
      GHashTable *params;

      new_c = l->data;
      old_c = g_hash_table_lookup (rc, GUINT_TO_POINTER ((guint) new_c->id));

      params = old_c->params;
      old_c->params = new_c->params;
      new_c->params = params;
    }

out:
  if (new_media_description != NULL)
    jingle_media_description_free (new_media_description);

  if (rc != NULL)
    g_hash_table_unref (rc);

  if (e != NULL)
    {
      DEBUG ("Rejecting codec update: %s", e->message);
      g_propagate_error (error, e);
    }
  else
    {
      DEBUG ("Emitting remote-media-description signal");
      g_signal_emit (self, signals[REMOTE_MEDIA_DESCRIPTION], 0,
          priv->remote_media_description);
    }
}

static void
parse_description (GabbleJingleContent *content,
    WockyNode *desc_node, GError **error)
{
  GabbleJingleMediaRtp *self = GABBLE_JINGLE_MEDIA_RTP (content);
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  JingleMediaType mtype;
  JingleMediaDescription *md;
  JingleCodec *p;
  JingleDialect dialect = gabble_jingle_session_get_dialect (content->session);
  gboolean video_session = FALSE;
  WockyNodeIter i;
  WockyNode *node;
  gboolean description_error = FALSE;
  gboolean is_avpf = FALSE;

  DEBUG ("node: %s", desc_node->name);

  if (priv->media_type == JINGLE_MEDIA_TYPE_NONE)
    mtype = extract_media_type (desc_node, error);
  else
    mtype = priv->media_type;

  if (mtype == JINGLE_MEDIA_TYPE_NONE)
    return;

  DEBUG ("detected media type %u", mtype);

  if (dialect == JINGLE_DIALECT_GTALK3)
    {
      const gchar *desc_ns =
        wocky_node_get_ns (desc_node);
      video_session = !wocky_strdiff (desc_ns, NS_GOOGLE_SESSION_VIDEO);
    }

  md = jingle_media_description_new ();

  wocky_node_iter_init (&i, desc_node, NULL, NULL);
  while (wocky_node_iter_next (&i, &node) && !description_error)
    {
      if (!wocky_strdiff (node->name, "payload-type"))
        {
          if (dialect == JINGLE_DIALECT_GTALK3)
            {
              const gchar *pt_ns = wocky_node_get_ns (node);

              if (priv->media_type == JINGLE_MEDIA_TYPE_AUDIO)
                {
                  if (video_session &&
                      wocky_strdiff (pt_ns, NS_GOOGLE_SESSION_PHONE))
                    continue;
                }
              else if (priv->media_type == JINGLE_MEDIA_TYPE_VIDEO)
                {
                  if (!(video_session && pt_ns == NULL)
                      && wocky_strdiff (pt_ns, NS_GOOGLE_SESSION_VIDEO))
                    continue;
                }
            }

          p = parse_payload_type (content, node);

          if (p == NULL)
            {
              description_error = TRUE;
            }
          else
            {
              md->codecs = g_list_append (md->codecs, p);
              if (p->trr_int != G_MAXUINT || p->feedback_msgs)
                is_avpf = TRUE;
            }
        }
      else if (!wocky_strdiff (node->name, "rtp-hdrext"))
        {
          const gchar *pt_ns = wocky_node_get_ns (node);
          JingleRtpHeaderExtension *hdrext;

          if (wocky_strdiff (pt_ns, NS_JINGLE_RTP_HDREXT))
            continue;

          hdrext = parse_rtp_header_extension (node);

          if (hdrext == NULL)
            {
              description_error = TRUE;
            }
          else
            {
              md->hdrexts = g_list_append (md->hdrexts, hdrext);
              priv->has_rtp_hdrext = TRUE;
            }

        }
      else if (!wocky_strdiff (node->name, "rtcp-fb"))
        {
          JingleFeedbackMessage *fb = parse_rtcp_fb (content, node);

          if (fb == NULL)
            {
              description_error = TRUE;
            }
          else
            {
              md->feedback_msgs = g_list_append (md->feedback_msgs, fb);
              is_avpf = TRUE;
              priv->has_rtcp_fb = TRUE;
            }
        }
      else if (!wocky_strdiff (node->name, "rtcp-fb-trr-int"))
        {
          guint trr_int = parse_rtcp_fb_trr_int (content, node);

          if (trr_int == G_MAXUINT)
            {
              description_error = TRUE;
            }
          else
            {
              md->trr_int = trr_int;
              is_avpf = TRUE;
              priv->has_rtcp_fb = TRUE;
            }
       }
    }

  if (description_error)
    {
      /* rollback these */
      jingle_media_description_free (md);
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "invalid description");
      return;
    }

  /* If the profile is AVPF, the trr-int default to 0 */
  if (is_avpf && md->trr_int == G_MAXUINT)
    md->trr_int = 0;

  priv->media_type = mtype;

  update_remote_media_description (self, md, error);
}

/* The Google Talk desktop client is picky about the case of codec names, even
 * though SDP defines them to be case-insensitive. The particular case that was
 * causing problems was ILBC vs iLBC, but it seems safer to special-case the
 * lot. This list is taken from the initiate sent by the desktop client on
 * 2009-07-01.
 */
static const gchar * const codec_cases[] = {
    "CN",
    "EG711A",
    "EG711U",
    "G723",
    "IPCMWB",
    "ISAC",
    "PCMA",
    "PCMU",
    "iLBC",
    "speex",
    "telephone-event",
    NULL
};

static const gchar *
gtalk_case (const gchar *codec)
{
  const gchar * const *ret = codec_cases;

  for (; *ret != NULL; ret++)
    if (g_ascii_strcasecmp (*ret, codec) == 0)
      return *ret;

  return codec;
}

static void
_produce_extra_param (gpointer key, gpointer value, gpointer user_data)
{
  WockyNode *pt_node = user_data;
  WockyNode *param;
  gchar *param_name = key;
  gchar *param_value = value;

  param = wocky_node_add_child_with_content (pt_node, "parameter", NULL);
  wocky_node_set_attribute (param, "name", param_name);
  wocky_node_set_attribute (param, "value", param_value);
}

static void
produce_rtcp_fb_trr_int (WockyNode *node,
                         guint trr_int)
{
  WockyNode *trr_int_node;
  gchar tmp[10];

 if (trr_int == G_MAXUINT || trr_int == 0)
    return;

  trr_int_node = wocky_node_add_child_ns (node, "rtcp-fb-trr-int",
      NS_JINGLE_RTCP_FB);
  snprintf (tmp, 9, "%d", trr_int);
  wocky_node_set_attribute (trr_int_node, "value", tmp);
}


static void
produce_rtcp_fb (JingleFeedbackMessage *fb, WockyNode *node)
{
  WockyNode *fb_node;

  fb_node = wocky_node_add_child (node, "rtcp-fb");

  wocky_node_set_attribute (fb_node, "xmlns", NS_JINGLE_RTCP_FB);
  wocky_node_set_attribute (fb_node, "type", fb->type);

  if (fb->subtype != NULL && fb->subtype[0] != 0)
    wocky_node_set_attribute (fb_node, "subtype", fb->subtype);
}

static void
produce_payload_type (GabbleJingleContent *content,
                      WockyNode *desc_node,
                      JingleMediaType type,
                      JingleCodec *p,
                      JingleDialect dialect)
{
  GabbleJingleMediaRtp *self = GABBLE_JINGLE_MEDIA_RTP (content);
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  WockyNode *pt_node;
  gchar buf[16];

  pt_node = wocky_node_add_child_with_content (desc_node, "payload-type", NULL);

  /* id: required */
  sprintf (buf, "%d", p->id);
  wocky_node_set_attribute (pt_node, "id", buf);

  if (dialect == JINGLE_DIALECT_GTALK3)
    {
      if (type == JINGLE_MEDIA_TYPE_AUDIO)
        {
          /* Gtalk 03 has either an audio or a video session, in case of a
           * video session the audio codecs need to set their namespace to
           * NS_GOOGLE_SESSION_PHONE. In the case of an audio session it
           * doesn't matter, so just always set the namespace on audio
           * payloads.
           */
          pt_node->ns = g_quark_from_static_string (
              NS_GOOGLE_SESSION_PHONE);
        }
      else
        {
          /* If width, height and framerate aren't set the google server ignore
           * our initiate.. These are a recv parameters, to it doesn't matter
           * for what we're sending, just for what we're getting.. 320x240
           * seems a sane enough default */
          wocky_node_set_attributes (pt_node,
            "width", "320",
            "height", "240",
            "framerate", "30",
            NULL);
        }

    }

  /* name: optional */
  if (*p->name != '\0')
    {
      if (JINGLE_IS_GOOGLE_DIALECT (dialect))
        wocky_node_set_attribute (pt_node, "name", gtalk_case (p->name));
      else
        wocky_node_set_attribute (pt_node, "name", p->name);
    }

  /* clock rate: optional */
  if (p->clockrate != 0)
    {
      const gchar *attname = "clockrate";

      if (dialect == JINGLE_DIALECT_V015)
        attname = "rate";

      sprintf (buf, "%u", p->clockrate);
      wocky_node_set_attribute (pt_node, attname, buf);
    }

  if (p->channels != 0)
    {
      sprintf (buf, "%u", p->channels);
      wocky_node_set_attribute (pt_node, "channels", buf);
    }

  if (p->params != NULL)
    g_hash_table_foreach (p->params, _produce_extra_param, pt_node);


  if (priv->has_rtcp_fb)
    {
      g_list_foreach (p->feedback_msgs, (GFunc) produce_rtcp_fb, pt_node);
      produce_rtcp_fb_trr_int (pt_node, p->trr_int);
    }
}

static WockyNode *
produce_description_node (JingleDialect dialect, JingleMediaType media_type,
   WockyNode *content_node)
{
  WockyNode *desc_node;
  const gchar *xmlns = NULL;

  if (dialect == JINGLE_DIALECT_GTALK3)
    return NULL;

  desc_node = wocky_node_add_child_with_content (content_node, "description", NULL);

  switch (dialect)
    {
      case JINGLE_DIALECT_GTALK4:
        g_assert (media_type == JINGLE_MEDIA_TYPE_AUDIO);
        xmlns = NS_GOOGLE_SESSION_PHONE;
        break;
      case JINGLE_DIALECT_V015:
        if (media_type == JINGLE_MEDIA_TYPE_AUDIO)
            xmlns = NS_JINGLE_DESCRIPTION_AUDIO;
        else if (media_type == JINGLE_MEDIA_TYPE_VIDEO)
            xmlns = NS_JINGLE_DESCRIPTION_VIDEO;
        else
          {
            DEBUG ("unknown media type %u", media_type);
            xmlns = "";
          }
        break;
      default:
        xmlns = NS_JINGLE_RTP;
        if (media_type == JINGLE_MEDIA_TYPE_AUDIO)
            wocky_node_set_attribute (desc_node, "media", "audio");
        else if (media_type == JINGLE_MEDIA_TYPE_VIDEO)
            wocky_node_set_attribute (desc_node, "media", "video");
        else
            g_assert_not_reached ();
        break;
    }

  desc_node->ns = g_quark_from_string (xmlns);

  return desc_node;
}

static void
produce_hdrext (gpointer data, gpointer user_data)
{
  JingleRtpHeaderExtension *hdrext = data;
  WockyNode *desc_node = user_data;
  WockyNode *hdrext_node;
  gchar buf[16];

  hdrext_node = wocky_node_add_child (desc_node, "rtp-hdrext");

  /* id: required */
  sprintf (buf, "%d", hdrext->id);
  wocky_node_set_attribute (hdrext_node, "id", buf);
  wocky_node_set_attribute (hdrext_node, "uri", hdrext->uri);

  if (hdrext->senders == JINGLE_CONTENT_SENDERS_INITIATOR)
    wocky_node_set_attribute (hdrext_node, "senders", "initiator");
  else if (hdrext->senders == JINGLE_CONTENT_SENDERS_RESPONDER)
    wocky_node_set_attribute (hdrext_node, "senders", "responder");

  wocky_node_set_attribute (hdrext_node, "xmlns", NS_JINGLE_RTP_HDREXT);
}

static void
produce_description (GabbleJingleContent *content, WockyNode *content_node)
{
  GabbleJingleMediaRtp *self = GABBLE_JINGLE_MEDIA_RTP (content);
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  GList *li;
  JingleDialect dialect = gabble_jingle_session_get_dialect (content->session);
  WockyNode *desc_node;

  if (gabble_jingle_session_peer_has_cap (content->session, NS_JINGLE_RTCP_FB))
    priv->has_rtcp_fb = TRUE;

  if (gabble_jingle_session_peer_has_cap (content->session, NS_JINGLE_RTP_HDREXT))
    priv->has_rtp_hdrext = TRUE;

  desc_node = produce_description_node (dialect, priv->media_type,
      content_node);

  /* For GTalk3 the description is added by the session */
  if (desc_node == NULL)
    desc_node = content_node;

  /* If we're only updating our codec parameters, only generate payload-types
   * for those.
   */
  if (priv->local_codec_updates != NULL)
    li = priv->local_codec_updates;
  else
    li = priv->local_media_description->codecs;

  for (; li != NULL; li = li->next)
    produce_payload_type (content, desc_node, priv->media_type, li->data,
        dialect);

  if (priv->has_rtp_hdrext && priv->local_media_description->hdrexts)
    g_list_foreach (priv->local_media_description->hdrexts, produce_hdrext,
        desc_node);

  if (priv->has_rtcp_fb)
    {
      g_list_foreach (priv->local_media_description->feedback_msgs,
          (GFunc) produce_rtcp_fb, desc_node);
      produce_rtcp_fb_trr_int (desc_node,
          priv->local_media_description->trr_int);
    }
}

/**
 * string_string_maps_equal:
 *
 * Returns: TRUE iff @a and @b contain exactly the same keys and values when
 *          compared as strings.
 */
static gboolean
string_string_maps_equal (GHashTable *a,
                          GHashTable *b)
{
  GHashTableIter iter;
  gpointer a_key, a_value, b_value;

  if (g_hash_table_size (a) != g_hash_table_size (b))
    return FALSE;

  g_hash_table_iter_init (&iter, a);

  while (g_hash_table_iter_next (&iter, &a_key, &a_value))
    {
      if (!g_hash_table_lookup_extended (b, a_key, NULL, &b_value))
        return FALSE;

      if (wocky_strdiff (a_value, b_value))
        return FALSE;
    }

  return TRUE;
}

/**
 * compare_codecs:
 * @old: previous local codecs
 * @new: new local codecs supplied by streaming implementation
 * @changed: location at which to store the changed codecs
 * @error: location at which to store an error if the update was invalid
 *
 * Returns: %TRUE if the update made sense, %FALSE with @error set otherwise
 */
gboolean
jingle_media_rtp_compare_codecs (GList *old,
                GList *new,
                GList **changed,
                GError **e)
{
  gboolean ret = FALSE;
  GHashTable *old_table = build_codec_table (old);
  GList *l;
  JingleCodec *old_c, *new_c;

  g_assert (changed != NULL && *changed == NULL);

  for (l = new; l != NULL; l = l->next)
    {
      new_c = l->data;
      old_c = g_hash_table_lookup (old_table, GUINT_TO_POINTER (
            (guint) new_c->id));

      if (!codec_update_coherent (old_c, new_c, e))
        goto out;

      if (!string_string_maps_equal (old_c->params, new_c->params))
        *changed = g_list_prepend (*changed, new_c);
    }

  ret = TRUE;

out:
  if (!ret)
    {
      g_list_free (*changed);
      *changed = NULL;
    }

  g_hash_table_unref (old_table);
  return ret;
}

/*
 * @self: a content in an RTP session
 * @md: (transfer full): new media description for this content
 * @ready: whether the codecs can regarded as ready to sent from now on
 * @error: used to return a %WOCKY_XMPP_ERROR if the codec update is illegal.
 *
 * Sets or updates the media description (codecs, feedback messages, etc) for
 * @self.
 *
 * Returns: %TRUE if no description was previously set, or if the update is
 *  compatible with the existing description; %FALSE if the update is illegal
 *  (due to adding previously-unknown codecs or renaming an existing codec, for
 *  example)
 */
gboolean
jingle_media_rtp_set_local_media_description (GabbleJingleMediaRtp *self,
                                              JingleMediaDescription *md,
                                              gboolean ready,
                                              GError **error)
{
  GabbleJingleMediaRtpPrivate *priv = self->priv;

  DEBUG ("setting new local media description");

  if (priv->local_media_description != NULL)
    {
      GList *changed = NULL;
      GError *err = NULL;

      g_assert (priv->local_codec_updates == NULL);

      if (!jingle_media_rtp_compare_codecs (
            priv->local_media_description->codecs,
            md->codecs, &changed, &err))
        {
          DEBUG ("codec update was illegal: %s", err->message);
          jingle_media_description_free (md);
          g_propagate_error (error, err);
          return FALSE;
        }

      if (changed == NULL)
        {
          DEBUG ("codec update changed nothing!");
          jingle_media_description_free (md);
          goto out;
        }

      DEBUG ("%u codecs changed", g_list_length (changed));
      priv->local_codec_updates = changed;

      jingle_media_description_free (priv->local_media_description);
    }

  priv->local_media_description = md;

  /* Codecs have changed, sending a fresh description might be necessary */
  gabble_jingle_content_maybe_send_description (GABBLE_JINGLE_CONTENT (self));

  /* Update done if any, free the changed codecs if any */
  g_list_free (priv->local_codec_updates);
  priv->local_codec_updates = NULL;

out:
  if (ready)
    _gabble_jingle_content_set_media_ready (GABBLE_JINGLE_CONTENT (self));

  return TRUE;
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

  /* GTalk video call namespace */
  gabble_jingle_factory_register_content_type (factory,
      NS_GOOGLE_SESSION_VIDEO,
      GABBLE_TYPE_JINGLE_MEDIA_RTP);
}

/* We can't get remote media description when they're signalled, because
 * the signal is emitted immediately upon JingleContent creation,
 * and parsing, which is before a corresponding MediaStream is
 * created. */
JingleMediaDescription *
gabble_jingle_media_rtp_get_remote_media_description (
    GabbleJingleMediaRtp *self)
{
  GabbleJingleMediaRtpPrivate *priv = self->priv;

  return priv->remote_media_description;
}

JingleMediaDescription *
jingle_media_description_new (void)
{
  JingleMediaDescription *md = g_slice_new0 (JingleMediaDescription);

  md->trr_int = G_MAXUINT;

  return md;
}

void
jingle_media_description_free (JingleMediaDescription *md)
{
  jingle_media_rtp_free_codecs (md->codecs);

  while (md->hdrexts != NULL)
    {
      jingle_rtp_header_extension_free (md->hdrexts->data);
      md->hdrexts = g_list_delete_link (md->hdrexts, md->hdrexts);
    }

  g_slice_free (JingleMediaDescription, md);
}

JingleMediaDescription *
jingle_media_description_copy (JingleMediaDescription *md)
{
  JingleMediaDescription *newmd = g_slice_new0 (JingleMediaDescription);
  GList *li;

  newmd->codecs = jingle_media_rtp_copy_codecs (md->codecs);
  newmd->feedback_msgs = jingle_feedback_message_list_copy (md->feedback_msgs);
  newmd->trr_int = md->trr_int;

  for (li = md->hdrexts; li; li = li->next)
    {
      JingleRtpHeaderExtension *h = li->data;

      newmd->hdrexts = g_list_append (newmd->hdrexts,
          jingle_rtp_header_extension_new (h->id, h->senders, h->uri));
    }

  return newmd;
}

JingleRtpHeaderExtension *
jingle_rtp_header_extension_new (guint id, JingleContentSenders senders,
    const gchar *uri)
{
  JingleRtpHeaderExtension *hdrext = g_slice_new (JingleRtpHeaderExtension);

  hdrext->id = id;
  hdrext->senders = senders;
  hdrext->uri = g_strdup (uri);

  return hdrext;
}

void
jingle_rtp_header_extension_free (JingleRtpHeaderExtension *hdrext)
{
  g_free (hdrext->uri);
  g_slice_free (JingleRtpHeaderExtension, hdrext);
}

JingleFeedbackMessage *
jingle_feedback_message_new (const gchar *type, const gchar *subtype)
{
  JingleFeedbackMessage *fb = g_slice_new0 (JingleFeedbackMessage);

  fb->type = g_strdup (type);
  fb->subtype = g_strdup (subtype);

  return fb;
}

void
jingle_feedback_message_free (JingleFeedbackMessage *fb)
{
  g_free (fb->type);
  g_free (fb->subtype);
  g_slice_free (JingleFeedbackMessage, fb);
}


static gint
jingle_feedback_message_compare (const JingleFeedbackMessage *fb1,
    const JingleFeedbackMessage *fb2)
{
  if (!g_ascii_strcasecmp (fb1->type, fb2->type) &&
      !g_ascii_strcasecmp (fb1->subtype, fb2->subtype))
    return 0;
  else
    return 1;
}

/**
 * jingle_media_description_simplify:
 *
 * Removes duplicated Feedback message and put them in the global structure
 *
 * This function will iterate over every codec in a description and look for
 * feedback messages that are exactly the same in every codec and will instead
 * put the in the list in the description and remove them from the childs.
 * This limits the amount of duplication in the resulting XML.
 */

void
jingle_media_description_simplify (JingleMediaDescription *md)
{
  GList *item;
  guint trr_int = 0;
  gboolean trr_int_all_same = TRUE;
  gboolean init = FALSE;
  GList *identical_fbs = NULL;

  for (item = md->codecs; item; item = item->next)
    {
      JingleCodec *c = item->data;

      if (!init)
        {
          /* For the first codec, it stores the trr_int and the list
           * of feedback messages */
          trr_int = c->trr_int;
          identical_fbs = g_list_copy (c->feedback_msgs);
          init = TRUE;
        }
      else
        {
          GList *item2;

          /* For every subsequent codec, we check if the trr_int is the same */

          if (trr_int != c->trr_int)
            trr_int_all_same = FALSE;

          /* We also intersect the remembered list of feedback messages with
           * the list for that codec and remove any feedback message that isn't
           * in both
           */

          for (item2 = identical_fbs; item2;)
            {
              JingleFeedbackMessage *fb = identical_fbs->data;
              GList *next = item2->next;

              if (!g_list_find_custom (c->feedback_msgs, fb,
                      (GCompareFunc) jingle_feedback_message_compare))
                identical_fbs = g_list_delete_link (identical_fbs,  item2);

              item2 = next;
            }

          /* If the trr_int is not the same everywhere and there are not common
           * feedback messages, then stop
           */
          if (!trr_int_all_same && identical_fbs == NULL)
            break;
        }
    }

  if (trr_int_all_same && trr_int == G_MAXUINT)
    trr_int_all_same = FALSE;

  /* if the trr_int is the same everywhere, lets set it globally */
  if (trr_int_all_same)
    md->trr_int = trr_int;

  /* If there are feedback messages that are in every codec, put a copy of them
   * in the global structure
   */
  if (identical_fbs)
    {
      md->feedback_msgs = jingle_feedback_message_list_copy (identical_fbs);
      g_list_free (identical_fbs);
    }

  if (trr_int_all_same || md->feedback_msgs != NULL)
    for (item = md->codecs; item; item = item->next)
      {
        JingleCodec *c = item->data;
        GList *item2;

        /* If the trr_int is the same everywhere, lets put the default on
         * each codec, we have it in the main structure
         */
        if (trr_int_all_same)
          c->trr_int = G_MAXUINT;

        /* Find the feedback messages that were put in the main structure and
         * remove them from each codec
         */
        for (item2 = md->feedback_msgs; item2; item2 = item2->next)
          {
            GList *duplicated;
            JingleFeedbackMessage *fb = item2->data;

            while ((duplicated = g_list_find_custom (c->feedback_msgs, fb,
                        (GCompareFunc) jingle_feedback_message_compare)) != NULL)
              {
                jingle_feedback_message_free (duplicated->data);
                c->feedback_msgs = g_list_delete_link (c->feedback_msgs,
                    duplicated);
              }
          }
      }
}
