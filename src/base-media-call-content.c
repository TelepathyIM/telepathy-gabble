/*
 * tpy-base-media-call-content.c - Source for TpyBaseMediaCallContent
 * Copyright (C) 2009-2010 Collabora Ltd.
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

#include "base-media-call-content.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void call_content_media_iface_init (gpointer, gpointer);
static void call_content_deinit (TpyBaseCallContent *base);
static void tpy_base_media_call_content_next_offer (
    TpyBaseMediaCallContent *self);

G_DEFINE_TYPE_WITH_CODE(TpyBaseMediaCallContent, tpy_base_media_call_content,
    TPY_TYPE_BASE_CALL_CONTENT,
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
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
struct _TpyBaseMediaCallContentPrivate
{
  gboolean initial_offer_appeared;
  TpyCallContentCodecOffer *current_offer;
  GQueue *outstanding_offers;
  GCancellable *offer_cancellable;
  guint offer_count;

  gboolean dispose_has_run;
  gboolean deinit_has_run;

  GHashTable *codec_map;

  GPtrArray *local_codecs;
};

static void
_free_codec_array (gpointer codecs)
{
  g_boxed_free (TPY_ARRAY_TYPE_CODEC_LIST, codecs);
}

static void
tpy_base_media_call_content_init (TpyBaseMediaCallContent *self)
{
  TpyBaseMediaCallContentPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TPY_TYPE_BASE_MEDIA_CALL_CONTENT, TpyBaseMediaCallContentPrivate);

  self->priv = priv;

  priv->outstanding_offers = g_queue_new ();
  priv->codec_map = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) _free_codec_array);
}

static void tpy_base_media_call_content_dispose (GObject *object);
static void tpy_base_media_call_content_finalize (GObject *object);

static void
tpy_base_media_call_content_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  TpyBaseMediaCallContent *content = TPY_BASE_MEDIA_CALL_CONTENT (object);
  TpyBaseMediaCallContentPrivate *priv = content->priv;

  switch (property_id)
    {
      case PROP_PACKETIZATION:
        /* TODO: actually set this to its real value */
        g_value_set_uint (value, TPY_CALL_CONTENT_PACKETIZATION_TYPE_RTP);
        break;
      case PROP_CONTACT_CODEC_MAP:
        g_value_set_boxed (value, priv->codec_map);
        break;
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
tpy_base_media_call_content_class_init (
    TpyBaseMediaCallContentClass *tpy_base_media_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (tpy_base_media_call_content_class);
  TpyBaseCallContentClass *bcc_class =
      TPY_BASE_CALL_CONTENT_CLASS (tpy_base_media_call_content_class);
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

  g_type_class_add_private (tpy_base_media_call_content_class,
      sizeof (TpyBaseMediaCallContentPrivate));

  object_class->get_property = tpy_base_media_call_content_get_property;
  object_class->dispose = tpy_base_media_call_content_dispose;
  object_class->finalize = tpy_base_media_call_content_finalize;

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
      G_OBJECT_CLASS_TYPE (tpy_base_media_call_content_class),
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
tpy_base_media_call_content_dispose (GObject *object)
{
  TpyBaseMediaCallContent *self = TPY_BASE_MEDIA_CALL_CONTENT (object);
  TpyBaseMediaCallContentPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_assert (priv->current_offer == NULL);

  g_hash_table_unref (priv->codec_map);
  priv->local_codecs = NULL;
  priv->codec_map = NULL;

  if (G_OBJECT_CLASS (tpy_base_media_call_content_parent_class)->dispose)
    G_OBJECT_CLASS (tpy_base_media_call_content_parent_class)->dispose (object);
}

static void
tpy_base_media_call_content_finalize (GObject *object)
{
  TpyBaseMediaCallContent *self = TPY_BASE_MEDIA_CALL_CONTENT (object);
  TpyBaseMediaCallContentPrivate *priv = self->priv;
  GObjectClass *object_class =
      G_OBJECT_CLASS (tpy_base_media_call_content_parent_class);

  g_queue_free (priv->outstanding_offers);

  if (object_class->finalize != NULL)
    object_class->finalize (object);
}

static gboolean
tpy_base_media_call_codec_array_equal (const GPtrArray *a, const GPtrArray *b)
{
  guint i;

  if (a == b)
    return TRUE;

  if (a == NULL || b == NULL)
    return FALSE;

  if (a->len != b->len)
    return FALSE;

  for (i = 0 ; i < a->len; i++)
    {
      GValueArray *va, *vb;
      GHashTable *ah, *bh;
      GHashTableIter iter;
      gpointer a_key, a_value, b_value;

      va = g_ptr_array_index (a, i);
      vb = g_ptr_array_index (b, i);

      /* id */
      if (g_value_get_uint (va->values + 0)
          != g_value_get_uint (vb->values + 0))
        return FALSE;

      /* name */
      if (tp_strdiff (g_value_get_string (va->values + 1),
          g_value_get_string (vb->values + 1)))
        return FALSE;
      /* clock-rate */
      if (g_value_get_uint (va->values + 2)
          != g_value_get_uint (vb->values + 2))
        return FALSE;

      /* channels */
      if (g_value_get_uint (va->values + 3)
          != g_value_get_uint (vb->values + 3))
        return FALSE;

      ah = g_value_get_boxed (va->values + 4);
      bh = g_value_get_boxed (vb->values + 4);

      if (g_hash_table_size (ah) != g_hash_table_size (bh))
        return FALSE;

      g_hash_table_iter_init (&iter, ah);

      while (g_hash_table_iter_next (&iter, &a_key, &a_value))
        {
          if (!g_hash_table_lookup_extended (bh, a_key, NULL, &b_value))
            return FALSE;

          if (tp_strdiff (a_value, b_value))
            return FALSE;
        }
  }

  return TRUE;
}

static void
tpy_base_media_call_content_set_local_codecs (TpyBaseMediaCallContent *self,
  const GPtrArray *codecs)
{
  TpyBaseMediaCallContentPrivate *priv = self->priv;
  TpBaseConnection *conn = tpy_base_call_content_get_connection (
      TPY_BASE_CALL_CONTENT (self));
  GPtrArray *c;

  if (tpy_base_media_call_codec_array_equal (priv->local_codecs, codecs))
    return;

  c = g_boxed_copy (TPY_ARRAY_TYPE_CODEC_LIST, codecs);
  priv->local_codecs = c;
  g_hash_table_replace (priv->codec_map, GUINT_TO_POINTER (conn->self_handle),
       c);

  g_signal_emit (self, signals[LOCAL_CODECS_UPDATED], 0, priv->local_codecs);
}

static void
tpy_base_media_call_content_update_codecs (
    TpySvcCallContentInterfaceMedia *iface,
    const GPtrArray *codecs,
    DBusGMethodInvocation *context)
{
  TpyBaseMediaCallContent *self = TPY_BASE_MEDIA_CALL_CONTENT (iface);

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

  tpy_base_media_call_content_set_local_codecs (self, codecs);
  tpy_svc_call_content_interface_media_return_from_update_codecs (context);
}

static void
call_content_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpySvcCallContentInterfaceMediaClass *klass =
    (TpySvcCallContentInterfaceMediaClass *) g_iface;

#define IMPLEMENT(x) tpy_svc_call_content_interface_media_implement_##x (\
    klass, tpy_base_media_call_content_##x)
  IMPLEMENT(update_codecs);
#undef IMPLEMENT
}

static gboolean
maybe_finish_deinit (TpyBaseMediaCallContent *self)
{
  TpyBaseMediaCallContentPrivate *priv = self->priv;

  g_assert (priv->deinit_has_run);

  if (priv->offer_count > 0)
    return FALSE;

  g_object_unref (self);
  return TRUE;
}

static void
call_content_deinit (TpyBaseCallContent *base)
{
  TpyBaseMediaCallContent *self = TPY_BASE_MEDIA_CALL_CONTENT (base);
  TpyBaseMediaCallContentPrivate *priv = self->priv;

  if (priv->deinit_has_run)
    return;

  priv->deinit_has_run = TRUE;


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

  TPY_BASE_CALL_CONTENT_CLASS (
    tpy_base_media_call_content_parent_class)->deinit (base);
}

static void
codec_offer_finished_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TpyBaseMediaCallContent *self = TPY_BASE_MEDIA_CALL_CONTENT (user_data);
  TpyBaseMediaCallContentPrivate *priv = self->priv;
  TpyCallContentCodecOffer *offer = TPY_CALL_CONTENT_CODEC_OFFER (source);
  GError *error = NULL;
  GPtrArray *local_codecs;
  TpHandle contact;
  GPtrArray *codecs;
  GArray *empty;

  local_codecs = tpy_call_content_codec_offer_offer_finish (
    offer, result, &error);

  if (error != NULL || priv->deinit_has_run ||
      priv->current_offer != TPY_CALL_CONTENT_CODEC_OFFER (source))
    goto out;

  g_object_get (offer,
    "remote-contact-codecs", &codecs,
    "remote-contact", &contact,
    NULL);

  if (codecs->len > 0)
    g_hash_table_replace (priv->codec_map, GUINT_TO_POINTER (contact), codecs);
  else
    _free_codec_array (codecs);

  tpy_base_media_call_content_set_local_codecs (self, local_codecs);

  empty = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  tpy_svc_call_content_interface_media_emit_codecs_changed (self,
      priv->codec_map, empty);
   g_array_free (empty, TRUE);

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
    tpy_base_media_call_content_next_offer (self);
}

static void
tpy_base_media_call_content_next_offer (TpyBaseMediaCallContent *self)
{
  TpyBaseMediaCallContentPrivate *priv = self->priv;
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
tpy_base_media_call_content_add_offer (TpyBaseMediaCallContent *self,
  TpyCallContentCodecOffer *offer)
{
  TpyBaseMediaCallContentPrivate *priv = self->priv;

  ++priv->offer_count;
  /* set this to TRUE so that after the initial offer disappears,
   * UpdateCodecs is allowed to be called. */
  priv->initial_offer_appeared = TRUE;

  g_queue_push_tail (priv->outstanding_offers, offer);
  tpy_base_media_call_content_next_offer (self);
}

GPtrArray *
tpy_base_media_call_content_get_local_codecs (TpyBaseMediaCallContent *self)
{
  return self->priv->local_codecs;
}
