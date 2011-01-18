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

#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/gtypes.h>

#include <telepathy-yell/gtypes.h>
#include <telepathy-yell/interfaces.h>
#include <telepathy-yell/svc-call.h>

#include <telepathy-yell/call-content-codec-offer.h>
#include <telepathy-yell/base-call-content.h>
#include <telepathy-yell/base-call-stream.h>

#include "call-member.h"
#include "call-content.h"
#include "call-stream.h"
#include "jingle-content.h"
#include "jingle-session.h"
#include "jingle-media-rtp.h"
#include "connection.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void call_content_deinit (TpyBaseCallContent *base);
static void call_content_local_codecs_updated (
    GabbleCallContent *self,
    GPtrArray *local_codecs,
    gpointer data);

G_DEFINE_TYPE (GabbleCallContent, gabble_call_content,
    TPY_TYPE_BASE_MEDIA_CALL_CONTENT);

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

  g_signal_connect (self, "local_codecs_updated",
    G_CALLBACK (call_content_local_codecs_updated), NULL);
}

static void gabble_call_content_dispose (GObject *object);

static void
gabble_call_content_class_init (
    GabbleCallContentClass *gabble_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_content_class);
  TpyBaseCallContentClass *bcc_class =
      TPY_BASE_CALL_CONTENT_CLASS (gabble_call_content_class);

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
call_content_deinit (TpyBaseCallContent *base)
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

  TPY_BASE_CALL_CONTENT_CLASS (
    gabble_call_content_parent_class)->deinit (base);
}

static GPtrArray *
call_content_codec_list_to_array (GList *codecs)
{
  GPtrArray *arr;
  GList *l;

  arr = g_ptr_array_sized_new (g_list_length (codecs));
  g_ptr_array_set_free_func (arr,
    (GDestroyNotify) g_value_array_free);

  for (l = codecs; l != NULL; l = g_list_next (l))
    {
      GValueArray *v;
      JingleCodec *c = l->data;

      g_assert (c->params != NULL);

      v = tp_value_array_build (5,
        G_TYPE_UINT, (guint) c->id,
        G_TYPE_STRING, c->name,
        G_TYPE_UINT, c->clockrate,
        G_TYPE_UINT, c->channels,
        DBUS_TYPE_G_STRING_STRING_HASHTABLE, c->params,
        G_TYPE_INVALID);

        g_ptr_array_add (arr, v);
    }

  return arr;
}

void
gabble_call_content_new_offer (GabbleCallContent *self,
  GabbleCallMemberContent *content)
{
  GabbleCallContentPrivate *priv = self->priv;
  TpyBaseCallContent *base = TPY_BASE_CALL_CONTENT (self);
  TpyCallContentCodecOffer *offer;
  gchar *path;
  TpHandle handle = 0;
  GPtrArray *codecs;

  if (content != NULL)
    {
      handle = gabble_call_member_get_handle (
        gabble_call_member_content_get_member (content));

      codecs = call_content_codec_list_to_array (
        gabble_call_member_content_get_remote_codecs (content));
    }
  else
    {
      codecs = g_ptr_array_new ();
    }

  path = g_strdup_printf ("%s/Offer%d",
      tpy_base_call_content_get_object_path (base),
      priv->offers++);

  offer = tpy_call_content_codec_offer_new (path, handle, codecs);
  tpy_base_media_call_content_add_offer (TPY_BASE_MEDIA_CALL_CONTENT (self),
    offer);

  g_ptr_array_unref (codecs);
  g_free (path);
}

JingleMediaType
gabble_call_content_get_media_type (GabbleCallContent *self)
{
  TpyBaseCallContent *base = TPY_BASE_CALL_CONTENT (self);

  return jingle_media_type_from_tp (
      tpy_base_call_content_get_media_type (base));
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
        g_value_get_boxed (va->values + 4));

        l = g_list_append (l, c);
    }

  return l;
}


static void
call_content_local_codecs_updated (GabbleCallContent *self,
    GPtrArray *local_codecs,
    gpointer data)
{
  GList *l;
  GList *codecs = codec_array_to_list (local_codecs);

  for (l = self->priv->contents; l != NULL; l = g_list_next (l))
    {
      GabbleCallMemberContent *c = GABBLE_CALL_MEMBER_CONTENT (l->data);
      GabbleJingleContent *j =
        gabble_call_member_content_get_jingle_content (c);

      if (j == NULL)
        continue;

      /* FIXME react properly on errors ? */
      jingle_media_rtp_set_local_codecs (GABBLE_JINGLE_MEDIA_RTP (j),
        jingle_media_rtp_copy_codecs (codecs), TRUE, NULL);
    }

  jingle_media_rtp_free_codecs (codecs);
}

static void
call_content_setup_jingle (GabbleCallContent *self,
    GabbleCallMemberContent *mcontent)
{
  TpyBaseCallContent *base = TPY_BASE_CALL_CONTENT (self);
  GabbleJingleContent *jingle;
  GabbleCallStream *stream;
  gchar *path;
  GList *codecs;

  jingle = gabble_call_member_content_get_jingle_content (mcontent);

  if (jingle == NULL)
    return;

  path = g_strdup_printf ("%s/Stream%p",
      tpy_base_call_content_get_object_path (base),
      jingle);
  stream = g_object_new (GABBLE_TYPE_CALL_STREAM,
      "object-path", path,
      "connection", tpy_base_call_content_get_connection (base),
      "jingle-content", jingle,
      NULL);
  g_free (path);

  codecs = codec_array_to_list (
      tpy_base_media_call_content_get_local_codecs (
        TPY_BASE_MEDIA_CALL_CONTENT (self)));

  if (codecs != NULL)
    jingle_media_rtp_set_local_codecs (GABBLE_JINGLE_MEDIA_RTP (jingle),
      codecs, TRUE, NULL);

  tpy_base_call_content_add_stream (base, TPY_BASE_CALL_STREAM (stream));
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
  TpyBaseCallContent *base = TPY_BASE_CALL_CONTENT (self);
  GabbleJingleContent *content;
  GList *l;

  priv->contents = g_list_remove (priv->contents, mcontent);

  content = gabble_call_member_content_get_jingle_content (mcontent);

  for (l = tpy_base_call_content_get_streams (base);
       l != NULL;
       l = l->next)
    {
      GabbleCallStream *stream = GABBLE_CALL_STREAM (l->data);

      if (content == gabble_call_stream_get_jingle_content (stream))
        {
          tpy_base_call_content_remove_stream (base, l->data);
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
