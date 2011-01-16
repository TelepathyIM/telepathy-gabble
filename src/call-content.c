/*
 * gabble-call-content.c - Source for GabbleCallContent
 * Copyright (C) 2009 Collabora Ltd.
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

static void call_content_iface_init (gpointer, gpointer);
static void call_content_media_iface_init (gpointer, gpointer);

static GPtrArray *call_content_codec_list_to_array (GList *codecs);
static GHashTable *call_content_generate_codec_map (GabbleCallContent *self,
    gboolean include_local);

static void call_content_deinit (TpyBaseCallContent *base);
static void gabble_call_content_next_offer (GabbleCallContent *self);

G_DEFINE_TYPE_WITH_CODE(GabbleCallContent, gabble_call_content,
    TPY_TYPE_BASE_CALL_CONTENT,
    G_IMPLEMENT_INTERFACE (TPY_TYPE_SVC_CALL_CONTENT,
        call_content_iface_init);
    G_IMPLEMENT_INTERFACE (TPY_TYPE_SVC_CALL_CONTENT_INTERFACE_MEDIA,
      call_content_media_iface_init);
    );

/* properties */
enum
{
  PROP_CONTACT_CODEC_MAP = 1,
  PROP_PACKETIZATION,

  PROP_CODEC_OFFER,
};

/* signal enum */
enum
{
    LOCAL_CODECS_UPDATED,
    REMOVED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
struct _GabbleCallContentPrivate
{
  GList *contents;

  gboolean initial_offer_appeared;
  TpyCallContentCodecOffer *current_offer;
  GQueue *outstanding_offers;
  GCancellable *offer_cancellable;
  gint offers;
  guint offer_count;

  gboolean dispose_has_run;
  gboolean deinit_has_run;

  GList *local_codecs;
};

static void
gabble_call_content_init (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_CONTENT, GabbleCallContentPrivate);

  self->priv = priv;

  priv->outstanding_offers = g_queue_new ();
}

static void gabble_call_content_dispose (GObject *object);
static void gabble_call_content_finalize (GObject *object);

static void
gabble_call_content_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallContent *content = GABBLE_CALL_CONTENT (object);
  GabbleCallContentPrivate *priv = content->priv;

  switch (property_id)
    {
      case PROP_PACKETIZATION:
        /* TODO: actually set this to its real value */
        g_value_set_uint (value, TPY_CALL_CONTENT_PACKETIZATION_TYPE_RTP);
        break;
      case PROP_CONTACT_CODEC_MAP:
        {
          GHashTable *map;

          map = call_content_generate_codec_map (content, TRUE);
          g_value_set_boxed (value, map);
          g_hash_table_unref (map);
          break;
        }
      case PROP_CODEC_OFFER:
        {
          GValueArray *arr;
          gchar *path;
          GPtrArray *codecs;
          TpHandle contact;

          if (priv->current_offer == NULL)
            {
              path = g_strdup ("/");
              contact = 0;
              codecs = g_ptr_array_new ();
            }
          else
            {
              g_object_get (priv->current_offer,
                "object-path", &path,
                "remote-contact", &contact,
                "remote-contact-codecs", &codecs,
                NULL);
            }

          arr = tp_value_array_build (3,
            DBUS_TYPE_G_OBJECT_PATH, path,
            G_TYPE_UINT, contact,
            TPY_ARRAY_TYPE_CODEC_LIST, codecs,
            G_TYPE_INVALID);

          g_value_take_boxed (value, arr);
          g_free (path);
          g_boxed_free (TPY_ARRAY_TYPE_CODEC_LIST, codecs);
          break;
        }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_content_constructed (GObject *obj)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (obj);
  GabbleCallContentPrivate *priv = self->priv;

  if (G_OBJECT_CLASS (gabble_call_content_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_content_parent_class)->constructed (obj);

  /* register object on the bus */
  priv->initial_offer_appeared = FALSE;
}

static void
gabble_call_content_class_init (
    GabbleCallContentClass *gabble_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_content_class);
  TpyBaseCallContentClass *bcc_class =
      TPY_BASE_CALL_CONTENT_CLASS (gabble_call_content_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl content_media_props[] = {
    { "ContactCodecMap", "contact-codec-map", NULL },
    { "CodecOffer", "codec-offer", NULL },
    { NULL }
  };
  static const gchar *interfaces[] = {
      TPY_IFACE_CALL_CONTENT_INTERFACE_MEDIA,
      NULL
  };

  g_type_class_add_private (gabble_call_content_class,
      sizeof (GabbleCallContentPrivate));

  object_class->constructed = gabble_call_content_constructed;
  object_class->get_property = gabble_call_content_get_property;
  object_class->dispose = gabble_call_content_dispose;
  object_class->finalize = gabble_call_content_finalize;

  param_spec = g_param_spec_uint ("packetization", "Packetization",
      "The Packetization of this content",
      0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PACKETIZATION,
      param_spec);

  param_spec = g_param_spec_boxed ("contact-codec-map", "ContactCodecMap",
      "The map of contacts to codecs",
      TPY_HASH_TYPE_CONTACT_CODEC_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT_CODEC_MAP,
      param_spec);

  param_spec = g_param_spec_boxed ("codec-offer", "CodecOffer",
      "The current codec offer if any",
      TPY_STRUCT_TYPE_CODEC_OFFERING,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CODEC_OFFER,
      param_spec);

  signals[LOCAL_CODECS_UPDATED] = g_signal_new ("local-codecs-updated",
      G_OBJECT_CLASS_TYPE (gabble_call_content_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  tp_dbus_properties_mixin_implement_interface (object_class,
      TPY_IFACE_QUARK_CALL_CONTENT_INTERFACE_MEDIA,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      content_media_props);

  bcc_class->extra_interfaces = interfaces;
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

  g_assert (priv->current_offer == NULL);

  jingle_media_rtp_free_codecs (priv->local_codecs);
  priv->local_codecs = NULL;

  if (G_OBJECT_CLASS (gabble_call_content_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_content_parent_class)->dispose (object);
}

static void
gabble_call_content_finalize (GObject *object)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (object);
  GabbleCallContentPrivate *priv = self->priv;

  g_queue_free (priv->outstanding_offers);

  if (G_OBJECT_CLASS (gabble_call_content_parent_class)->finalize)
    G_OBJECT_CLASS (gabble_call_content_parent_class)->finalize (object);
}

static gboolean
call_content_set_local_codecs (GabbleCallContent *self,
    const GPtrArray *codecs)
{
  GabbleCallContentPrivate *priv = self->priv;
  GList *l = NULL;
  guint i;

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

  if (jingle_media_rtp_codecs_equal (priv->local_codecs, l))
    {
      jingle_media_rtp_free_codecs (l);
      return FALSE;
    }

  jingle_media_rtp_free_codecs (priv->local_codecs);
  priv->local_codecs = l;

  for (l = priv->contents ; l != NULL; l = g_list_next (l))
    {
      GabbleCallMemberContent *c = GABBLE_CALL_MEMBER_CONTENT (l->data);
      GabbleJingleContent *j =
        gabble_call_member_content_get_jingle_content (c);

      if (j == NULL)
        continue;

      /* FIXME react properly on errors ? */
      jingle_media_rtp_set_local_codecs (GABBLE_JINGLE_MEDIA_RTP (j),
        jingle_media_rtp_copy_codecs (priv->local_codecs), TRUE, NULL);
    }

  g_signal_emit (self, signals[LOCAL_CODECS_UPDATED], 0, priv->local_codecs);

  return TRUE;
}

static void
gabble_call_content_remove (TpySvcCallContent *content,
    TpyContentRemovalReason reason,
    const gchar *detailed_removal_reason,
    const gchar *message,
    DBusGMethodInvocation *context)
{
  /* TODO: actually do something with this reason and message. */
  DEBUG ("removing content for reason %u, dbus error: %s, message: %s",
      reason, detailed_removal_reason, message);

  g_signal_emit (content, signals[REMOVED], 0, NULL);
  /* it doesn't matter if a ::removed signal handler calls deinit as
   * there are guards around it being called again and breaking, so
   * let's just call it be sure it's done. */
  tpy_base_call_content_deinit (TPY_BASE_CALL_CONTENT (content));
  tpy_svc_call_content_return_from_remove (context);
}

static void
call_content_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpySvcCallContentClass *klass =
    (TpySvcCallContentClass *) g_iface;

#define IMPLEMENT(x) tpy_svc_call_content_implement_##x (\
    klass, gabble_call_content_##x)
  IMPLEMENT(remove);
#undef IMPLEMENT
}

static void
gabble_call_content_update_codecs (TpySvcCallContentInterfaceMedia *iface,
    const GPtrArray *codecs,
    DBusGMethodInvocation *context)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (iface);

  if (self->priv->current_offer != NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "There is a codec offer around so "
          "UpdateCodecs shouldn't be called." };
      dbus_g_method_return_error (context, &error);
      return;
    }

  if (!self->priv->initial_offer_appeared)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "The initial CodecOffer object has not yet appeared; keep waiting." };
      dbus_g_method_return_error (context, &error);
      return;
    }

  call_content_set_local_codecs (GABBLE_CALL_CONTENT (iface), codecs);
  tpy_svc_call_content_interface_media_return_from_update_codecs (context);
}

static void
call_content_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpySvcCallContentInterfaceMediaClass *klass =
    (TpySvcCallContentInterfaceMediaClass *) g_iface;

#define IMPLEMENT(x) tpy_svc_call_content_interface_media_implement_##x (\
    klass, gabble_call_content_##x)
  IMPLEMENT(update_codecs);
#undef IMPLEMENT
}

static gboolean
maybe_finish_deinit (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = self->priv;

  g_assert (priv->deinit_has_run);

  if (priv->offer_count > 0)
    return FALSE;

  g_object_unref (self);
  return TRUE;
}

static void
call_content_deinit (TpyBaseCallContent *base)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (base);
  GabbleCallContentPrivate *priv = self->priv;

  if (priv->deinit_has_run)
    return;

  priv->deinit_has_run = TRUE;

  TPY_BASE_CALL_CONTENT_CLASS (gabble_call_content_parent_class)->deinit (
      base);

  /* Keep ourself alive until we've finished deinitializing;
   * maybe_finish_deinit() will drop this reference to ourself.
   */
  g_object_ref (base);

  g_queue_foreach (priv->outstanding_offers, (GFunc) g_object_unref, NULL);
  g_queue_clear (priv->outstanding_offers);

  if (priv->offer_cancellable != NULL)
    g_cancellable_cancel (priv->offer_cancellable);
  else
    maybe_finish_deinit (self);
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

static GHashTable *
call_content_generate_codec_map (GabbleCallContent *self,
  gboolean include_local)
{
  GabbleCallContentPrivate *priv = self->priv;
  GHashTable *map;
  GPtrArray *arr;
  GList *l;

  /* FIXME this map is always remote + local, irrespective of whether it has
   * been accepted or not */

  map = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, (GDestroyNotify) g_ptr_array_unref);

  if (include_local && priv->local_codecs != NULL)
    {
      TpBaseConnection *conn = tpy_base_call_content_get_connection (
          TPY_BASE_CALL_CONTENT (self));
      arr = call_content_codec_list_to_array (priv->local_codecs);
      g_hash_table_insert (map,
        GUINT_TO_POINTER (conn->self_handle),
        arr);
    }

  for (l = priv->contents ; l != NULL; l = g_list_next (l))
    {
      GabbleCallMemberContent *c = GABBLE_CALL_MEMBER_CONTENT (l->data);
      GList *codecs;

      codecs = gabble_call_member_content_get_remote_codecs (c);
      if (codecs != NULL)
        {
          GabbleCallMember *m = gabble_call_member_content_get_member (c);
          arr = call_content_codec_list_to_array (codecs);
          g_hash_table_insert (map,
              GUINT_TO_POINTER (gabble_call_member_get_handle (m)),
              arr);
        }
    }

  return map;
}

static void
codec_offer_finished_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (user_data);
  GabbleCallContentPrivate *priv = self->priv;
  GError *error = NULL;
  GPtrArray *local_codecs;
  GHashTable *codec_map;
  GArray *empty;

  local_codecs = tpy_call_content_codec_offer_offer_finish (
    TPY_CALL_CONTENT_CODEC_OFFER (source), result, &error);

  if (error != NULL || priv->deinit_has_run ||
      priv->current_offer != TPY_CALL_CONTENT_CODEC_OFFER (source))
    goto out;

  if (call_content_set_local_codecs (self, local_codecs))
    {
      codec_map = call_content_generate_codec_map (self, TRUE);
      empty = g_array_new (FALSE, FALSE, sizeof (TpHandle));

      tpy_svc_call_content_interface_media_emit_codecs_changed (self,
        codec_map, empty);

      g_hash_table_unref (codec_map);
      g_array_free (empty, TRUE);
    }

out:
  if (priv->current_offer == TPY_CALL_CONTENT_CODEC_OFFER (source))
    {
      priv->current_offer = NULL;
      priv->offer_cancellable = NULL;
    }

  --priv->offer_count;
  g_object_unref (source);

  if (priv->deinit_has_run)
    maybe_finish_deinit (self);
  else
    gabble_call_content_next_offer (self);
}

static void
gabble_call_content_next_offer (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = self->priv;
  TpyCallContentCodecOffer *offer;
  gchar *path;
  GPtrArray *codecs;
  TpHandle handle;

  if (priv->current_offer != NULL)
    {
      DEBUG ("Waiting for the current offer to finish"
        " before starting the next one");
      return;
    }

  offer = g_queue_pop_head (priv->outstanding_offers);

  if (offer == NULL)
    {
      DEBUG ("No more offers outstanding");
      return;
    }

  priv->current_offer = offer;

  g_assert (priv->offer_cancellable == NULL);
  priv->offer_cancellable = g_cancellable_new ();

  tpy_call_content_codec_offer_offer (priv->current_offer,
    priv->offer_cancellable,
    codec_offer_finished_cb, self);

  g_object_get (offer,
      "object-path", &path,
      "remote-contact", &handle,
      "remote-contact-codecs", &codecs,
      NULL);

  DEBUG ("emitting NewCodecOffer: %s", path);
  tpy_svc_call_content_interface_media_emit_new_codec_offer (
    self, handle, path, codecs);
  g_free (path);
  g_boxed_free (TPY_ARRAY_TYPE_CODEC_LIST, codecs);
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
  ++priv->offer_count;

  g_ptr_array_unref (codecs);
  g_free (path);

  /* set this to TRUE so that after the initial offer disappears,
   * UpdateCodecs is allowed to be called. */
  priv->initial_offer_appeared = TRUE;

  g_queue_push_tail (priv->outstanding_offers, offer);
  gabble_call_content_next_offer (self);
}

GList *
gabble_call_content_get_local_codecs (GabbleCallContent *self)
{
  return self->priv->local_codecs;
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

static void
call_content_setup_jingle (GabbleCallContent *self,
    GabbleCallMemberContent *mcontent)
{
  GabbleCallContentPrivate *priv = self->priv;
  TpyBaseCallContent *base = TPY_BASE_CALL_CONTENT (self);
  GabbleJingleContent *jingle;
  GabbleCallStream *stream;
  gchar *path;

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

  jingle_media_rtp_set_local_codecs (GABBLE_JINGLE_MEDIA_RTP (jingle),
      jingle_media_rtp_copy_codecs (priv->local_codecs), TRUE, NULL);

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
