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

#include <extensions/extensions.h>

#include "call-content.h"
#include "call-content-codecoffer.h"
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
static void call_content_remote_codecs_cb (
  GabbleJingleMediaRtp *media, GList *codecs, gpointer user_data);
static void call_content_new_offer (GabbleCallContent *self);

static GPtrArray *call_content_codec_list_to_array (GList *codecs);
static GHashTable *call_content_generate_codec_map (GabbleCallContent *self);

G_DEFINE_TYPE_WITH_CODE(GabbleCallContent, gabble_call_content,
  G_TYPE_OBJECT,
   G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
   G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_CONTENT,
    call_content_iface_init);
   G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_CONTENT_INTERFACE_MEDIA,
    call_content_media_iface_init);
  );

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CONNECTION,
  PROP_JINGLE_CONTENT,
  PROP_TARGET_HANDLE,

  PROP_NAME,
  PROP_CREATOR,
  PROP_DISPOSITION,

  PROP_CONTACT_CODEC_MAP,
  PROP_MEDIA_TYPE,
  PROP_STREAMS,
  PROP_CODEC_OFFER,
};

/* private structure */
struct _GabbleCallContentPrivate
{
  GabbleConnection *conn;

  gchar *object_path;
  TpHandle target;
  GabbleJingleContent *content;

  GabbleCallContentCodecoffer *offer;
  gint offers;

  gchar *name;
  GabbleCallContentDisposition disposition;
  TpHandle creator;

  GList *streams;
  gboolean dispose_has_run;
};

static void
gabble_call_content_init (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_CONTENT, GabbleCallContentPrivate);

  self->priv = priv;
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
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_JINGLE_CONTENT:
        g_value_set_object (value, priv->content);
        break;
      case PROP_TARGET_HANDLE:
        g_value_set_uint (value, priv->target);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_MEDIA_TYPE:
        {
          JingleMediaType mtype;
          g_object_get (priv->content, "media-type", &mtype, NULL);
          switch (mtype)
            {
              case JINGLE_MEDIA_TYPE_AUDIO:
                g_value_set_uint (value, TP_MEDIA_STREAM_TYPE_AUDIO);
                break;
              case JINGLE_MEDIA_TYPE_VIDEO:
                g_value_set_uint (value, TP_MEDIA_STREAM_TYPE_VIDEO);
                break;
              default:
                g_assert_not_reached ();
            }
          break;
        }
      case PROP_STREAMS:
        {
          GPtrArray *arr = g_ptr_array_sized_new (2);
          GList *l;

          for (l = priv->streams; l != NULL; l = g_list_next (l))
            {
              GabbleCallStream *s = GABBLE_CALL_STREAM (l->data);
              g_ptr_array_add (arr,
                  (gpointer) gabble_call_stream_get_object_path (s));
            }

          g_value_set_boxed (value, arr);
          g_ptr_array_free (arr, TRUE);
          break;
        }
      case PROP_NAME:
        {
          gchar *name;
          g_object_get (priv->content, "name", &name, NULL);
          if (name != NULL)
            g_value_take_string (value, name);
          else
            g_value_set_static_string (value, "");
          break;
        }
      case PROP_CREATOR:
        g_value_set_uint (value, priv->creator);
        break;
      case PROP_DISPOSITION:
        g_value_set_uint (value, priv->disposition);
        break;
      case PROP_CONTACT_CODEC_MAP:
        {
          GHashTable *map;

          map = call_content_generate_codec_map (content);
          g_value_set_boxed (value, map);
          g_hash_table_unref (map);
          break;
        }
      case PROP_CODEC_OFFER:
        {
          GValueArray *arr;
          gchar *path;
          GHashTable *map;

          if (priv->offer == NULL)
            {
              path = g_strdup ("/");
              map = g_hash_table_new (NULL, NULL);
            }
          else
            {
              g_object_get (priv->offer,
                "object-path", &path,
                "remote-contact-codec-map", &map,
                NULL);
            }

          arr = gabble_value_array_build (2,
            DBUS_TYPE_G_OBJECT_PATH, path,
            GABBLE_HASH_TYPE_CONTACT_CODEC_MAP, map,
            G_TYPE_INVALID);

          g_value_take_boxed (value, arr);
          g_free (path);
          g_boxed_free (GABBLE_HASH_TYPE_CONTACT_CODEC_MAP, map);
          break;
        }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_content_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallContent *content = GABBLE_CALL_CONTENT (object);
  GabbleCallContentPrivate *priv = content->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        priv->object_path = g_value_dup_string (value);
        g_assert (priv->object_path != NULL);
        break;
      case PROP_JINGLE_CONTENT:
        priv->content = g_value_dup_object (value);
        break;
      case PROP_TARGET_HANDLE:
        priv->target = g_value_get_uint (value);
        break;
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_CREATOR:
        priv->creator = g_value_get_uint (value);
        break;
      case PROP_DISPOSITION:
        priv->disposition = g_value_get_uint (value);
        break;
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
  DBusGConnection *bus;

  /* register object on the bus */
  bus = tp_get_bus ();
  DEBUG ("Registering %s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  if (priv->content != NULL)
    {
      GabbleCallStream *stream;
      gchar *path;

      path = g_strdup_printf ("%s/Stream%p", priv->object_path, priv->content);
      stream = g_object_new (GABBLE_TYPE_CALL_STREAM,
        "object-path", path,
        "connection", priv->conn,
        "jingle-content", priv->content,
        NULL);
      g_free (path);

      priv->streams = g_list_prepend (priv->streams, stream);

      if (gabble_jingle_media_rtp_get_remote_codecs (
          GABBLE_JINGLE_MEDIA_RTP (priv->content)) != NULL)
        {
          call_content_new_offer (self);
        }

      gabble_signal_connect_weak (priv->content, "remote-codecs",
        G_CALLBACK (call_content_remote_codecs_cb),
        obj);
    }

  if (G_OBJECT_CLASS (gabble_call_content_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_content_parent_class)->constructed (obj);
}

static void
gabble_call_content_class_init (
    GabbleCallContentClass *gabble_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_content_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl content_props[] = {
    { "Name", "name", NULL },
    { "Type", "media-type", NULL },
    { "Creator", "creator", NULL },
    { "Disposition", "disposition", NULL },
    { "Streams", "streams", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinPropImpl content_media_props[] = {
    { "ContactCodecMap", "contact-codec-map", NULL },
    { "CodecOffer", "codec-offer", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_CALL_CONTENT,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        content_props,
      },
      { GABBLE_IFACE_CALL_CONTENT_INTERFACE_MEDIA,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        content_media_props,
      },
      { NULL }
  };

  g_type_class_add_private (gabble_call_content_class,
      sizeof (GabbleCallContentPrivate));

  object_class->constructed = gabble_call_content_constructed;

  object_class->get_property = gabble_call_content_get_property;
  object_class->set_property = gabble_call_content_set_property;

  object_class->dispose = gabble_call_content_dispose;
  object_class->finalize = gabble_call_content_finalize;

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this "
      "object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_object ("jingle-content", "Jingle Content",
      "The Jingle Content related to this content object",
      GABBLE_TYPE_JINGLE_CONTENT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JINGLE_CONTENT,
      param_spec);

  param_spec = g_param_spec_uint ("target-handle", "Target Handle",
      "Target handle of the call channel",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_HANDLE,
      param_spec);

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this media channel object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("name", "Name",
      "The name of this content if any",
      "",
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_uint ("media-type", "Media Type",
      "The media type of this content",
      0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE,
      param_spec);

  param_spec = g_param_spec_uint ("creator", "Creator",
      "The creator content",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR,
      param_spec);

  param_spec = g_param_spec_uint ("disposition", "Disposition",
      "The disposition of this content",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DISPOSITION,
      param_spec);

  param_spec = g_param_spec_boxed ("streams", "Stream",
      "The streams of this content",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STREAMS,
      param_spec);

  param_spec = g_param_spec_boxed ("contact-codec-map", "ContactCodecMap",
      "The map of contacts to codecs",
      GABBLE_HASH_TYPE_CONTACT_CODEC_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT_CODEC_MAP,
      param_spec);

  param_spec = g_param_spec_boxed ("codec-offer", "CodecOffer",
      "The current codec offer if any",
      GABBLE_STRUCT_TYPE_CODEC_OFFERING,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CODEC_OFFER,
      param_spec);

  gabble_call_content_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleCallContentClass, dbus_props_class));
}

void
gabble_call_content_dispose (GObject *object)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (object);
  GabbleCallContentPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  g_object_unref (priv->content);
  priv->content = NULL;

  if (G_OBJECT_CLASS (gabble_call_content_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_content_parent_class)->dispose (object);
}

void
gabble_call_content_finalize (GObject *object)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (object);
  GabbleCallContentPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_free (priv->object_path);
  g_free (priv->name);

  G_OBJECT_CLASS (gabble_call_content_parent_class)->finalize (object);
}

static void
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
        g_value_dup_string (va->values + 1),
        g_value_get_uint (va->values + 2),
        g_value_get_uint (va->values + 3),
        g_value_dup_boxed (va->values + 4));

        l = g_list_append (l, c);
    }


  /* FIXME react properly on errors */
  jingle_media_rtp_set_local_codecs (GABBLE_JINGLE_MEDIA_RTP (priv->content),
    l, TRUE, NULL);

}

static void
gabble_call_content_set_codecs (GabbleSvcCallContentInterfaceMedia *iface,
    const GPtrArray *codecs,
    DBusGMethodInvocation *context)
{
  call_content_set_local_codecs (GABBLE_CALL_CONTENT (iface), codecs);
  gabble_svc_call_content_interface_media_return_from_set_codecs (context);
}

static void
call_content_iface_init (gpointer g_iface, gpointer iface_data)
{
}

static void
call_content_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcCallContentInterfaceMediaClass *klass =
    (GabbleSvcCallContentInterfaceMediaClass *) g_iface;

#define IMPLEMENT(x) gabble_svc_call_content_interface_media_implement_##x (\
    klass, gabble_call_content_##x)
  IMPLEMENT(set_codecs);
#undef IMPLEMENT
}

const gchar *
gabble_call_content_get_object_path (GabbleCallContent *content)
{
  return content->priv->object_path;
}

static GPtrArray *
call_content_codec_list_to_array (GList *codecs)
{
  GPtrArray *arr;
  GList *l;

  arr = g_ptr_array_sized_new (g_list_length (codecs));

  for (l = codecs; l != NULL; l = g_list_next (l))
    {
      GValueArray *v;
      JingleCodec *c = l->data;

      v = gabble_value_array_build (5,
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
call_content_generate_codec_map (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = self->priv;
  GList *codecs;
  GHashTable *map;
  GPtrArray *arr;

  /* FIXME this map is always remote + local, irrespective of whether it has
   * been accepted or not */

  map = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, (GDestroyNotify) g_ptr_array_unref);

  /* Local codecs */
  codecs = gabble_jingle_media_rtp_get_local_codecs (
     GABBLE_JINGLE_MEDIA_RTP (priv->content));

  if (codecs != NULL)
    {
      arr = call_content_codec_list_to_array (codecs);
      g_hash_table_insert (map,
        GUINT_TO_POINTER (TP_BASE_CONNECTION (priv->conn)->self_handle),
        arr);
    }

  codecs = gabble_jingle_media_rtp_get_remote_codecs (
     GABBLE_JINGLE_MEDIA_RTP (priv->content));

  if (codecs != NULL)
    {
      arr = call_content_codec_list_to_array (codecs);
      g_hash_table_insert (map, GUINT_TO_POINTER (priv->target), arr);
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

  local_codecs = gabble_call_content_codecoffer_offer_finish (
    GABBLE_CALL_CONTENT_CODECOFFER (source), result, &error);

  if (error != NULL)
    goto out;

  call_content_set_local_codecs (self, local_codecs);

  codec_map = call_content_generate_codec_map (self);
  empty = g_array_new (FALSE, FALSE, sizeof (TpHandle));

  gabble_svc_call_content_interface_media_emit_codecs_changed (self,
    codec_map, empty);

  g_hash_table_unref (codec_map);
  g_array_free (empty, TRUE);

out:
  g_object_unref (priv->offer);
  priv->offer = NULL;
}

static void
call_content_new_offer (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = self->priv;
  GList *codecs;
  GHashTable *map;
  GPtrArray *arr;
  gchar *path;

  codecs = gabble_jingle_media_rtp_get_remote_codecs (
     GABBLE_JINGLE_MEDIA_RTP (priv->content));

  map = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, (GDestroyNotify) g_ptr_array_unref);

  arr = call_content_codec_list_to_array (codecs);
  g_hash_table_insert (map, GUINT_TO_POINTER (priv->target), arr);

  /* FIXME: Support switching offers */
  g_assert (priv->offer == NULL);
  path = g_strdup_printf ("%s/Offer%d",
    priv->object_path, priv->offers++);

  priv->offer = gabble_call_content_codecoffer_new (path, map);
  gabble_call_content_codecoffer_offer (priv->offer, NULL,
    codec_offer_finished_cb, self);

  gabble_svc_call_content_interface_media_emit_new_codec_offer (
    self, path, map);

  g_hash_table_unref (map);
  g_free (path);
}

static void
call_content_remote_codecs_cb (GabbleJingleMediaRtp *media,
    GList *codecs,
    gpointer user_data)
{
  call_content_new_offer (GABBLE_CALL_CONTENT (user_data));
}
