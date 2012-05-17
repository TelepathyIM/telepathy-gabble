/*
 * gabble-call-content.c - Source for GabbleCallContent
 * Copyright (C) 2009-2011 Collabora Ltd.
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

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>

#include "call-member.h"
#include "call-content.h"
#include "call-stream.h"
#include "jingle-content.h"
#include "jingle-session.h"
#include "jingle-media-rtp.h"
#include "jingle-tp-util.h"
#include "connection.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void call_content_deinit (TpBaseCallContent *base);
static void call_content_local_media_description_updated (
    GabbleCallContent *self,
    TpHandle contact,
    GHashTable *properties,
    gpointer data);

G_DEFINE_TYPE (GabbleCallContent, gabble_call_content,
    TP_TYPE_BASE_MEDIA_CALL_CONTENT);

/* private structure */
struct _GabbleCallContentPrivate
{
  /* CallMemberContent list */
  GList *contents;
  guint offers;

  gboolean dispose_has_run;
  gboolean deinit_has_run;
};

static void
gabble_call_content_init (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_CONTENT, GabbleCallContentPrivate);

  self->priv = priv;

  g_signal_connect (self, "local-media-description-updated",
    G_CALLBACK (call_content_local_media_description_updated), NULL);
}

static void gabble_call_content_dispose (GObject *object);

static void
gabble_call_content_class_init (
    GabbleCallContentClass *gabble_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_content_class);
  TpBaseCallContentClass *bcc_class =
      TP_BASE_CALL_CONTENT_CLASS (gabble_call_content_class);

  g_type_class_add_private (gabble_call_content_class,
      sizeof (GabbleCallContentPrivate));

  object_class->dispose = gabble_call_content_dispose;
  bcc_class->deinit = call_content_deinit;
}

static void
gabble_call_content_dispose (GObject *object)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (object);
  GabbleCallContentPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_assert (priv->contents == NULL);

  if (G_OBJECT_CLASS (gabble_call_content_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_content_parent_class)->dispose (object);
}

static void
call_content_deinit (TpBaseCallContent *base)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (base);
  GabbleCallContentPrivate *priv = self->priv;

  if (priv->deinit_has_run)
    return;

  priv->deinit_has_run = TRUE;

  while (priv->contents != NULL)
    {
      GabbleCallMemberContent *c = priv->contents->data;

      priv->contents = g_list_delete_link (priv->contents, priv->contents);
      gabble_call_member_content_remove (c);
    }

  TP_BASE_CALL_CONTENT_CLASS (
    gabble_call_content_parent_class)->deinit (base);
}

void
gabble_call_content_new_offer (GabbleCallContent *self,
  GabbleCallMemberContent *content)
{
  GabbleCallContentPrivate *priv = self->priv;
  TpBaseCallContent *base = TP_BASE_CALL_CONTENT (self);
  TpBaseConnection *conn = tp_base_call_content_get_connection (base);
  TpCallContentMediaDescription *md;
  gchar *path;
  TpHandle handle = 0;

  path = g_strdup_printf ("%s/Offer%d",
      tp_base_call_content_get_object_path (base),
      priv->offers++);

  if (content != NULL)
    {
      handle = gabble_call_member_get_handle (
        gabble_call_member_content_get_member (content));
    }

  /* FIXME: no idea... */
  md = tp_call_content_media_description_new (
      tp_base_connection_get_dbus_daemon (conn), path, handle,
      (content != NULL), (content == NULL));

  if (content != NULL)
    {
      GList *codecs, *l;

      codecs = gabble_call_member_content_get_remote_codecs (content);
      for (l = codecs; l != NULL; l = g_list_next (l))
        {
          JingleCodec *c = l->data;

          tp_call_content_media_description_append_codec (md,
              c->id, c->name, c->clockrate, c->channels,
              FALSE, /* FIXME: updated?? */
              c->params);
        }
    }

  /* FIXME: We have to handle cases where the new codecs are rejected */
  tp_base_media_call_content_offer_media_description_async (
      TP_BASE_MEDIA_CALL_CONTENT (self), md, NULL, NULL);

  g_object_unref (md);
  g_free (path);
}

JingleMediaType
gabble_call_content_get_media_type (GabbleCallContent *self)
{
  TpBaseCallContent *base = TP_BASE_CALL_CONTENT (self);

  return jingle_media_type_from_tp (
      tp_base_call_content_get_media_type (base));
}

static void
member_content_codecs_changed (GabbleCallMemberContent *mcontent,
  gpointer user_data)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (user_data);

  DEBUG ("Preparing new codec offer");
  gabble_call_content_new_offer (self, mcontent);
}

static GList *
codec_array_to_list (GPtrArray *codecs)
{
  guint i;
  GList *l = NULL;

  if (codecs == NULL)
    return NULL;

  for (i = 0; i < codecs->len ; i++)
    {
      JingleCodec *c;
      GValueArray *va;

      va = g_ptr_array_index (codecs, i);

      c = jingle_media_rtp_codec_new (
        g_value_get_uint (va->values + 0),
        g_value_get_string (va->values + 1),
        g_value_get_uint (va->values + 2),
        g_value_get_uint (va->values + 3),
        /* g_value_get_boolean (va->values + 4), updated? */
        g_value_get_boxed (va->values + 5));

        l = g_list_append (l, c);
    }

  return l;
}


static void
call_content_local_media_description_updated (GabbleCallContent *self,
    TpHandle contact,
    GHashTable *properties,
    gpointer data)
{
  GList *l;
  JingleMediaDescription *md = jingle_media_description_new ();

  md->codecs = codec_array_to_list (tp_asv_get_boxed (properties,
          TP_PROP_CALL_CONTENT_MEDIA_DESCRIPTION_CODECS,
          TP_ARRAY_TYPE_CODEC_LIST));

  for (l = self->priv->contents; l != NULL; l = g_list_next (l))
    {
      GabbleCallMemberContent *c = GABBLE_CALL_MEMBER_CONTENT (l->data);
      GabbleJingleContent *j =
        gabble_call_member_content_get_jingle_content (c);

      if (j == NULL)
        continue;

      /* FIXME react properly on errors ? */
      jingle_media_rtp_set_local_media_description (GABBLE_JINGLE_MEDIA_RTP (j),
        jingle_media_description_copy (md), TRUE, NULL);
    }

  jingle_media_description_free (md);
}

static TpStreamTransportType
_jingle_to_tp_transport (JingleTransportType jt)
{
  switch (jt)
  {
    case JINGLE_TRANSPORT_GOOGLE_P2P:
      return TP_STREAM_TRANSPORT_TYPE_GTALK_P2P;
    case JINGLE_TRANSPORT_RAW_UDP:
      return TP_STREAM_TRANSPORT_TYPE_RAW_UDP;
    case JINGLE_TRANSPORT_ICE_UDP:
      return TP_STREAM_TRANSPORT_TYPE_ICE;
    default:
      g_return_val_if_reached (G_MAXUINT);
  }
}

static void
call_content_setup_jingle (GabbleCallContent *self,
    GabbleCallMemberContent *mcontent)
{
  TpBaseCallContent *base = TP_BASE_CALL_CONTENT (self);
  GabbleJingleContent *jingle;
  GabbleCallStream *stream;
  gchar *path;
  JingleTransportType transport;
  JingleMediaDescription *md;
  GHashTable *tp_md;
  TpHandle contact;

  jingle = gabble_call_member_content_get_jingle_content (mcontent);

  if (jingle == NULL)
    return;

  transport = gabble_jingle_content_get_transport_type (jingle);
  path = g_strdup_printf ("%s/Stream%p",
      tp_base_call_content_get_object_path (base),
      jingle);
  stream = g_object_new (GABBLE_TYPE_CALL_STREAM,
      "object-path", path,
      "connection", tp_base_call_content_get_connection (base),
      "jingle-content", jingle,
      "transport", _jingle_to_tp_transport (transport),
      NULL);
  g_free (path);

  md = jingle_media_description_new ();

  /* FIXME: correct??? */
  contact = gabble_call_member_get_handle (
      gabble_call_member_content_get_member (mcontent));
  tp_md = tp_base_media_call_content_get_local_media_description (
      TP_BASE_MEDIA_CALL_CONTENT (self), contact);
  if (tp_md != NULL)
    {
      md->codecs = codec_array_to_list (tp_asv_get_boxed (tp_md,
          TP_PROP_CALL_CONTENT_MEDIA_DESCRIPTION_CODECS,
          TP_ARRAY_TYPE_CODEC_LIST));
    }

  if (md->codecs != NULL)
    jingle_media_rtp_set_local_media_description (
        GABBLE_JINGLE_MEDIA_RTP (jingle), md, TRUE, NULL);
  else
    jingle_media_description_free (md);

  tp_base_call_content_add_stream (base, TP_BASE_CALL_STREAM (stream));
  gabble_call_stream_update_member_states (stream);
  g_object_unref (stream);
}

static void
member_content_got_jingle_content_cb (GabbleCallMemberContent *mcontent,
    gpointer user_data)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (user_data);
  call_content_setup_jingle (self, mcontent);
}

static void
member_content_removed_cb (GabbleCallMemberContent *mcontent,
    gpointer user_data)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (user_data);
  GabbleCallContentPrivate *priv = self->priv;
  TpBaseCallContent *base = TP_BASE_CALL_CONTENT (self);
  GabbleJingleContent *content;
  GList *l;

  priv->contents = g_list_remove (priv->contents, mcontent);

  content = gabble_call_member_content_get_jingle_content (mcontent);

  for (l = tp_base_call_content_get_streams (base);
       l != NULL;
       l = l->next)
    {
      GabbleCallStream *stream = GABBLE_CALL_STREAM (l->data);

      if (content == gabble_call_stream_get_jingle_content (stream))
        {
          tp_base_call_content_remove_stream (base, l->data,
              0, TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");
          break;
        }
    }
}

void
gabble_call_content_add_member_content (GabbleCallContent *self,
    GabbleCallMemberContent *content)
{
  self->priv->contents = g_list_prepend (self->priv->contents, content);
  call_content_setup_jingle (self, content);

  gabble_signal_connect_weak (content, "codecs-changed",
    G_CALLBACK (member_content_codecs_changed), G_OBJECT (self));

  gabble_signal_connect_weak (content, "got-jingle-content",
    G_CALLBACK (member_content_got_jingle_content_cb), G_OBJECT (self));

  gabble_signal_connect_weak (content, "removed",
    G_CALLBACK (member_content_removed_cb), G_OBJECT (self));

  gabble_call_content_new_offer (self, content);
}

GList *
gabble_call_content_get_member_contents (GabbleCallContent *self)
{
  return self->priv->contents;
}
