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

static void
jingle_media_rtp_codec_free (JingleCodec *p)
{
  g_hash_table_destroy (p->params);
  g_free (p->name);
  g_slice_free (JingleCodec, p);
}

static void
add_codec_to_table (JingleCodec *codec,
                    GHashTable *table)
{
  g_hash_table_insert (table, GUINT_TO_POINTER (codec->id), codec);
}

static GHashTable *
build_codec_table (GList *codecs)
{
  GHashTable *table = g_hash_table_new (NULL, NULL);

  g_list_foreach (codecs, (GFunc) add_codec_to_table, table);
  return table;
}

static void
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

static JingleMediaType
extract_media_type (LmMessageNode *desc_node,
                    GError **error)
{
  if (lm_message_node_has_namespace (desc_node, NS_JINGLE_RTP, NULL))
    {
      const gchar *type = lm_message_node_get_attribute (desc_node, "media");

      if (type == NULL)
        {
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
              "missing required media type attribute");
          return JINGLE_MEDIA_TYPE_NONE;
        }

      if (!tp_strdiff (type, "audio"))
          return JINGLE_MEDIA_TYPE_AUDIO;

      if (!tp_strdiff (type, "video"))
        return JINGLE_MEDIA_TYPE_VIDEO;

      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "unknown media type %s", type);
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
  guint8 id;
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
                       GQuark domain,
                       gint code,
                       GError **e)
{
  if (old_c == NULL)
    {
      g_set_error (e, domain, code, "Codec with id %u ('%s') unknown",
          new_c->id, new_c->name);
      return FALSE;
    }

  if (tp_strdiff (new_c->name, old_c->name))
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
          new_c->id, new_c->name, new_c->clockrate, old_c->clockrate);
      return FALSE;
    }

  if (new_c->channels != old_c->channels)
    {
      g_set_error (e, domain, code,
          "tried to change codec %u (%s)'s channels from %u to %u",
          new_c->id, new_c->name, new_c->channels, old_c->channels);
      return FALSE;
    }

  return TRUE;
}

static void
update_remote_codecs (GabbleJingleMediaRtp *self,
                      GList *new_codecs,
                      GError **error)
{
  GabbleJingleMediaRtpPrivate *priv = self->priv;
  GHashTable *rc = NULL;
  JingleCodec *old_c, *new_c;
  GList *l;
  GError *e = NULL;

  if (priv->remote_codecs == NULL)
    {
      priv->remote_codecs = new_codecs;
      new_codecs = NULL;
      goto out;
    }

  rc = build_codec_table (priv->remote_codecs);

  /* We already know some remote codecs, so this is just the other end updating
   * some parameters.
   */
  for (l = new_codecs; l != NULL; l = l->next)
    {
      new_c = l->data;
      old_c = g_hash_table_lookup (rc, GUINT_TO_POINTER (new_c->id));

      if (!codec_update_coherent (old_c, new_c, GABBLE_XMPP_ERROR,
            XMPP_ERROR_BAD_REQUEST, &e))
        goto out;
    }

  /* Okay, all the updates are cool. Let's switch the parameters around. */
  for (l = new_codecs; l != NULL; l = l->next)
    {
      GHashTable *params;

      new_c = l->data;
      old_c = g_hash_table_lookup (rc, GUINT_TO_POINTER (new_c->id));

      params = old_c->params;
      old_c->params = new_c->params;
      new_c->params = params;
    }

out:
  jingle_media_rtp_free_codecs (new_codecs);

  if (rc != NULL)
    g_hash_table_unref (rc);

  if (e != NULL)
    {
      DEBUG ("Rejecting codec update: %s", e->message);
      g_propagate_error (error, e);
    }
  else
    {
      DEBUG ("Emitting remote-codecs signal");
      g_signal_emit (self, signals[REMOTE_CODECS], 0, priv->remote_codecs);
    }
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
      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "invalid payload");
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
 * compare_codecs:
 * @old: previous local codecs
 * @new: new local codecs supplied by streaming implementation
 * @changed: location at which to store the changed codecs
 * @error: location at which to store an error if the update was invalid
 *
 * Returns: %TRUE if the update made sense, %FALSE with @error set otherwise
 */
static gboolean
compare_codecs (GList *old,
                GList *new,
                GList **changed,
                GError **e)
{
  gboolean ret = FALSE;
  GHashTable *old_table = build_codec_table (old);
  GList *l;
  JingleCodec *old_c, *new_c;

  g_assert (changed != NULL && *changed == NULL);

  if (g_list_length (new) != g_list_length (old))
    {
      g_set_error (e, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "tried to change the number of codecs from %u to %u",
          g_list_length (old), g_list_length (new));
      goto out;
    }

  for (l = new; l != NULL; l = l->next)
    {
      new_c = l->data;
      old_c = g_hash_table_lookup (old_table, GUINT_TO_POINTER (new_c->id));

      if (!codec_update_coherent (old_c, new_c, TP_ERRORS,
            TP_ERROR_INVALID_ARGUMENT, e))
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

/* Takes in a list of slice-allocated JingleCodec structs */
gboolean
jingle_media_rtp_set_local_codecs (GabbleJingleMediaRtp *self,
                                   GList *codecs,
                                   GError **error)
{
  GabbleJingleMediaRtpPrivate *priv = self->priv;

  DEBUG ("setting new local codecs");

  if (priv->local_codecs != NULL)
    {
      GList *changed = NULL;
      GError *err = NULL;

      /* Calling _gabble_jingle_content_set_media_ready () should use and unset
       * these right after we set them.
       */
      g_assert (priv->local_codec_updates == NULL);

      if (!compare_codecs (priv->local_codecs, codecs, &changed, &err))
        {
          DEBUG ("codec update was illegal: %s", err->message);
          g_propagate_error (error, err);
          return FALSE;
        }

      if (changed == NULL)
        {
          DEBUG ("codec update changed nothing!");
          jingle_media_rtp_free_codecs (codecs);
          return TRUE;
        }

      DEBUG ("%u codecs changed", g_list_length (changed));
      priv->local_codec_updates = changed;

      jingle_media_rtp_free_codecs (priv->local_codecs);
    }

  priv->local_codecs = codecs;

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

