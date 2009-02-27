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
  /* Holds (JingleCodec *)s borrowed from local_codecs, namely those which have
   * changed from local_codecs' previous value. Since the contents are
   * borrowed, this must be freed with g_list_free, not
   * jingle_media_rtp_free_codecs().
   */
  GList *local_codec_updates;

  GList *remote_codecs;
  JingleMediaType media_type;
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

  if (params != NULL)
      p->params = params;
  else
      p->params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
          g_free);

  return p;
}

void
jingle_media_rtp_codec_free (JingleCodec *p)
{
  g_hash_table_destroy (p->params);
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
  GabbleJingleMediaRtpPrivate *priv = trans->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  jingle_media_rtp_free_codecs (priv->remote_codecs);
  priv->remote_codecs = NULL;

  jingle_media_rtp_free_codecs (priv->local_codecs);
  priv->local_codecs = NULL;

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


static JingleMediaType
extract_media_type (LmMessageNode *desc_node,
                    GError **error)
{
  if (lm_message_node_has_namespace (desc_node, NS_JINGLE_RTP, NULL))
    {
      const gchar *type = lm_message_node_get_attribute (desc_node, "media");

      if (type == NULL)
        {
          SET_BAD_REQ("missing required media type attribute");
          return JINGLE_MEDIA_TYPE_NONE;
        }

      if (!tp_strdiff (type, "audio"))
          return JINGLE_MEDIA_TYPE_AUDIO;

      if (!tp_strdiff (type, "video"))
        return JINGLE_MEDIA_TYPE_VIDEO;

      SET_BAD_REQ("unknown media type %s", type);
      return JINGLE_MEDIA_TYPE_NONE;
    }

  if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_AUDIO, NULL))
    return JINGLE_MEDIA_TYPE_AUDIO;

  if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_VIDEO, NULL))
    return JINGLE_MEDIA_TYPE_VIDEO;

  if (lm_message_node_has_namespace (desc_node,
        NS_GOOGLE_SESSION_PHONE, NULL))
    return JINGLE_MEDIA_TYPE_AUDIO;

  /* If we get here, namespace in use is not one of namespaces we signed up
   * with, so obviously a bug somewhere.
   */
  g_assert_not_reached ();
}

/**
 * parse_payload_type:
 * @node: a <payload-type> node.
 *
 * Returns: a newly-allocated JingleCodec if parsing succeeds, or %NULL
 *          otherwise.
 */
static JingleCodec *
parse_payload_type (LmMessageNode *node)
{
  JingleCodec *p;
  const char *txt;
  guchar id;
  const gchar *name;
  guint clockrate = 0;
  guint channels = 1;
  LmMessageNode *param;

  txt = lm_message_node_get_attribute (node, "id");
  if (txt == NULL)
    return NULL;

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
    clockrate = atoi (txt);

  txt = lm_message_node_get_attribute (node, "channels");
  if (txt != NULL)
    channels = atoi (txt);

  p = jingle_media_rtp_codec_new (id, name, clockrate, channels, NULL);

  for (param = node->children; param != NULL; param = param->next)
    {
      const gchar *param_name, *param_value;

      if (tp_strdiff (lm_message_node_get_name (param), "parameter"))
        continue;

      param_name = lm_message_node_get_attribute (param, "name");
      param_value = lm_message_node_get_attribute (param, "value");

      if (param_name == NULL || param_value == NULL)
        continue;

      g_hash_table_insert (p->params, g_strdup (param_name),
          g_strdup (param_value));
    }

  DEBUG ("new remote codec: id = %u, name = %s, clockrate = %u, channels = %u",
      p->id, p->name, p->clockrate, p->channels);

  return p;
}

static void
update_remote_codecs (GabbleJingleMediaRtp *self,
                      GList *new_codecs,
                      GError **error)
{
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  GList *k, *l;

  if (priv->remote_codecs == NULL)
    {
      priv->remote_codecs = new_codecs;
      goto out;
    }

  /* We already know some remote codecs, so this is just the other end updating
   * some parameters.
   */
  for (k = new_codecs; k != NULL; k = k->next)
    {
      JingleCodec *new_codec = k->data;

      for (l = priv->remote_codecs; l != NULL; l = l->next)
        {
          JingleCodec *old_codec = l->data;
          GHashTable *tmp;

          if (old_codec->id != new_codec->id)
            continue;

          if (tp_strdiff (old_codec->name, new_codec->name))
            {
              DEBUG ("Codec with id %u has changed from %s to %s! Rejecting",
                  old_codec->id, old_codec->name, new_codec->name);
              SET_BAD_REQ ("Codec with id %u is %s, not %s", old_codec->id,
                  old_codec->name, new_codec->name);
              jingle_media_rtp_free_codecs (new_codecs);
              return;
            }

          old_codec->clockrate = new_codec->clockrate;
          old_codec->channels = new_codec->channels;

          tmp = old_codec->params;
          old_codec->params = new_codec->params;
          new_codec->params = tmp;

          break;
        }

      if (l == NULL)
        {
          DEBUG ("Codec with id %u ('%s') unknown; ignoring update",
              new_codec->id, new_codec->name);
        }
    }

out:
  DEBUG ("emitting remote-codecs signal");
  g_signal_emit (self, signals[REMOTE_CODECS], 0, priv->remote_codecs);
}

static void
parse_description (GabbleJingleContent *content,
    LmMessageNode *desc_node, GError **error)
{
  GabbleJingleMediaRtp *self = GABBLE_JINGLE_MEDIA_RTP (content);
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  JingleMediaType mtype = JINGLE_MEDIA_TYPE_NONE;
  GList *codecs = NULL;
  JingleCodec *p;
  LmMessageNode *node;

  DEBUG ("node: %s", desc_node->name);

  mtype = extract_media_type (desc_node, error);

  if (mtype == JINGLE_MEDIA_TYPE_NONE)
    return;

  DEBUG ("detected media type %u", mtype);

  for (node = desc_node->children; node; node = node->next)
    {
      if (tp_strdiff (lm_message_node_get_name (node), "payload-type"))
        continue;

      p = parse_payload_type (node);

      if (p == NULL)
        break;
      else
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

  update_remote_codecs (self, codecs, error);
}

static void
_produce_extra_param (gpointer key, gpointer value, gpointer user_data)
{
  LmMessageNode *pt_node = user_data;
  LmMessageNode *param;
  gchar *param_name = key;
  gchar *param_value = value;

  param = lm_message_node_add_child (pt_node, "parameter", NULL);
  lm_message_node_set_attribute (param, "name", param_name);
  lm_message_node_set_attribute (param, "value", param_value);
}

static void
produce_payload_type (LmMessageNode *desc_node,
                      JingleCodec *p,
                      JingleDialect dialect)
{
  LmMessageNode *pt_node;
  gchar buf[16];

  pt_node = lm_message_node_add_child (desc_node, "payload-type", NULL);

  /* id: required */
  sprintf (buf, "%d", p->id);
  lm_message_node_set_attribute (pt_node, "id", buf);

  /* name: optional */
  if (*p->name != '\0')
    lm_message_node_set_attribute (pt_node, "name", p->name);

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
    g_hash_table_foreach (p->params, _produce_extra_param, pt_node);
}

static void
produce_description (GabbleJingleContent *obj, LmMessageNode *content_node)
{
  GabbleJingleMediaRtp *desc = GABBLE_JINGLE_MEDIA_RTP (obj);
  GabbleJingleSession *sess;
  GabbleJingleMediaRtpPrivate *priv = desc->priv;
  LmMessageNode *desc_node;
  GList *li;
  JingleDialect dialect;
  const gchar *xmlns = NULL;

  sess = GABBLE_JINGLE_CONTENT(obj)->session;
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

  /* If we're only updating our codec parameters, only generate payload-types
   * for those.
   */
  if (priv->local_codec_updates != NULL)
    li = priv->local_codec_updates;
  else
    li = priv->local_codecs;

  for (; li != NULL; li = li->next)
    produce_payload_type (desc_node, li->data, dialect);

  /* If we were updating, then we're done with the diff. */
  g_list_free (priv->local_codec_updates);
  priv->local_codec_updates = NULL;
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

      if (tp_strdiff (a_value, b_value))
        return FALSE;
    }

  return TRUE;
}

/**
 * codec_info_equal:
 * Compares the clockrate, channels and params of the supplied codecs,
 * returning TRUE iff they are all equal.
 *
 * Does *not* compare the codecs' id or name.
 */
static gboolean
codec_info_equal (const JingleCodec *c,
                  const JingleCodec *d)
{
  return (c->clockrate == d->clockrate &&
    c->channels == d->channels &&
    string_string_maps_equal (c->params, d->params));
}

static GList *
changed_codecs (GList *old,
                GList *new)
{
  GList *changed = NULL;
  GList *k, *l;

  for (k = new; k != NULL; k = k->next)
    {
      JingleCodec *new_c = k->data;

      for (l = old; l != NULL; l = l->next)
        {
          JingleCodec *old_c = l->data;

          if (new_c->id != old_c->id)
            continue;

          if (tp_strdiff (new_c->name, old_c->name))
            {
              DEBUG ("streaming implementation has changed codec %u's name "
                  "from %s to %s!", new_c->id, old_c->name, new_c->name);

              /* FIXME: make CodecsUpdated fail. */
            }

          if (!codec_info_equal (old_c, new_c))
            {
              changed = g_list_prepend (changed, new_c);
              break;
            }
        }

      if (l == NULL)
        {
          DEBUG ("streaming implementation tried to update codec %u (%s) which "
              "wasn't there before", new_c->id, new_c->name);
          /* FIXME: make CodecsUpdated fail. */
        }
    }

  /* FIXME: this doesn't detect the streaming implementation trying to remove codecs. */

  return changed;
}

/* Takes in a list of slice-allocated JingleCodec structs */
void
jingle_media_rtp_set_local_codecs (GabbleJingleMediaRtp *self, GList *codecs)
{
  GabbleJingleMediaRtpPrivate *priv = self->priv;

  DEBUG ("setting new local codecs");

  if (priv->local_codecs != NULL)
    {
      /* Calling _gabble_jingle_content_set_media_ready () should use and unset
       * these right after we set them.
       */
      g_assert (priv->local_codec_updates == NULL);
      priv->local_codec_updates = changed_codecs (priv->local_codecs, codecs);

      jingle_media_rtp_free_codecs (priv->local_codecs);
      priv->local_codecs = codecs;
    }

  priv->local_codecs = codecs;

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
  return self->priv->remote_codecs;
}

