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

#include "call-member.h"
#include "call-content.h"
#include "call-content-codec-offer.h"
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
static void call_content_new_offer (GabbleCallContent *self);

static GPtrArray *call_content_codec_list_to_array (GList *codecs);
static GHashTable *call_content_generate_codec_map (GabbleCallContent *self,
    gboolean include_local);

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
  PROP_INTERFACES = 1,
  PROP_OBJECT_PATH,
  PROP_CONNECTION,
  PROP_TARGET_HANDLE,

  PROP_NAME,
  PROP_DISPOSITION,
  PROP_PACKETIZATION,

  PROP_CONTACT_CODEC_MAP,
  PROP_MEDIA_TYPE,
  PROP_JINGLE_MEDIA_TYPE,
  PROP_STREAMS,
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

/* interfaces */
static const gchar *gabble_call_content_interfaces[] = {
    GABBLE_IFACE_CALL_CONTENT_INTERFACE_MEDIA,
    NULL
};

/* private structure */
struct _GabbleCallContentPrivate
{
  GabbleConnection *conn;
  TpDBusDaemon *dbus_daemon;

  gchar *object_path;
  TpHandle target;
  GList *contents;

  gboolean initial_offer_appeared;
  GabbleCallContentCodecOffer *offer;
  GCancellable *offer_cancellable;
  gint offers;
  guint offer_count;

  GabbleCallContentDisposition disposition;

  GList *streams;
  gboolean dispose_has_run;
  gboolean deinit_has_run;

  gchar *name;
  JingleMediaType mtype;

  GList *local_codecs;
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
      case PROP_INTERFACES:
        g_value_set_boxed (value, gabble_call_content_interfaces);
        break;
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_JINGLE_MEDIA_TYPE:
        g_value_set_uint (value, priv->mtype);
        break;
      case PROP_MEDIA_TYPE:
        {
          switch (priv->mtype)
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
                  g_strdup (gabble_call_stream_get_object_path (s)));
            }

          g_value_take_boxed (value, arr);
          break;
        }
      case PROP_NAME:
        {
          g_value_set_string (value, priv->name);
          break;
        }
      case PROP_DISPOSITION:
        g_value_set_uint (value, priv->disposition);
        break;
      case PROP_PACKETIZATION:
        /* TODO: actually set this to its real value */
        g_value_set_uint (value, GABBLE_CALL_CONTENT_PACKETIZATION_TYPE_RTP);
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

          arr = tp_value_array_build (2,
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
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_NAME:
        priv->name = g_value_dup_string (value);
        break;
      case PROP_JINGLE_MEDIA_TYPE:
        priv->mtype = g_value_get_uint (value);
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

  /* register object on the bus */
  DEBUG ("Registering %s", priv->object_path);
  priv->dbus_daemon = g_object_ref (
      tp_base_connection_get_dbus_daemon ((TpBaseConnection *) priv->conn));
  tp_dbus_daemon_register_object (priv->dbus_daemon, priv->object_path, obj);

  priv->initial_offer_appeared = FALSE;

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
    { "Interfaces", "interfaces", NULL },
    { "Name", "name", NULL },
    { "Type", "media-type", NULL },
    { "Disposition", "disposition", NULL },
    { "Streams", "streams", NULL },
    { "Packetization", "packetization", NULL },
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

  param_spec = g_param_spec_boxed ("interfaces", "Interfaces",
      "Extra Call Content interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this "
      "object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this media channel object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("name", "Name",
      "The name of this content if any",
      "",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_uint ("media-type", "Media Type",
      "The media type of this content",
      0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE,
      param_spec);

  param_spec = g_param_spec_uint ("jingle-media-type", "Jingle Media Type",
      "The JingleMediaType of this content",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JINGLE_MEDIA_TYPE,
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

  param_spec = g_param_spec_uint ("packetization", "Packetization",
      "The Packetization of this content",
      0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PACKETIZATION,
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

  signals[LOCAL_CODECS_UPDATED] = g_signal_new ("local-codecs-updated",
      G_OBJECT_CLASS_TYPE (gabble_call_content_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[REMOVED] = g_signal_new ("removed",
      G_OBJECT_CLASS_TYPE (gabble_call_content_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  gabble_call_content_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleCallContentClass, dbus_props_class));
}

void
gabble_call_content_dispose (GObject *object)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (object);
  GabbleCallContentPrivate *priv = self->priv;
  GList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_assert (priv->offer == NULL);

  for (l = priv->streams; l != NULL; l = g_list_next (l))
    {
      g_object_unref (l->data);
    }

  tp_clear_pointer (&priv->streams, g_list_free);
  tp_clear_pointer (&priv->local_codecs, jingle_media_rtp_free_codecs);

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
        g_value_get_string (va->values + 1),
        g_value_get_uint (va->values + 2),
        g_value_get_uint (va->values + 3),
        g_value_get_boxed (va->values + 4));

        l = g_list_append (l, c);
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
}

static void
gabble_call_content_remove (GabbleSvcCallContent *content,
    GabbleContentRemovalReason reason,
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
  gabble_call_content_deinit (GABBLE_CALL_CONTENT (content));
  gabble_svc_call_content_return_from_remove (context);
}

static void
call_content_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcCallContentClass *klass =
    (GabbleSvcCallContentClass *) g_iface;

#define IMPLEMENT(x) gabble_svc_call_content_implement_##x (\
    klass, gabble_call_content_##x)
  IMPLEMENT(remove);
#undef IMPLEMENT
}

static void
gabble_call_content_update_codecs (GabbleSvcCallContentInterfaceMedia *iface,
    const GPtrArray *codecs,
    DBusGMethodInvocation *context)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (iface);

  if (self->priv->offer != NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "There is a codec offer around so UpdateCodecs shouldn't be called." };
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
  gabble_svc_call_content_interface_media_return_from_update_codecs (context);
}

static void
call_content_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcCallContentInterfaceMediaClass *klass =
    (GabbleSvcCallContentInterfaceMediaClass *) g_iface;

#define IMPLEMENT(x) gabble_svc_call_content_interface_media_implement_##x (\
    klass, gabble_call_content_##x)
  IMPLEMENT(update_codecs);
#undef IMPLEMENT
}

const gchar *
gabble_call_content_get_object_path (GabbleCallContent *content)
{
  return content->priv->object_path;
}

static void
call_content_accept_stream (gpointer data, gpointer user_data)
{
  GabbleCallStream *stream = GABBLE_CALL_STREAM (data);

  if (gabble_call_stream_get_local_sending_state (stream) ==
      GABBLE_SENDING_STATE_PENDING_SEND)
    gabble_call_stream_set_sending (stream, TRUE);
}

void
gabble_call_content_accept (GabbleCallContent *content)
{
  GabbleCallContentPrivate *priv = content->priv;

  if (priv->disposition == GABBLE_CALL_CONTENT_DISPOSITION_INITIAL)
    g_list_foreach (priv->streams, call_content_accept_stream, NULL);
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

void
gabble_call_content_deinit (GabbleCallContent *content)
{
  GabbleCallContentPrivate *priv = content->priv;
  GList *l;

  if (priv->deinit_has_run)
    return;

  priv->deinit_has_run = TRUE;

  tp_dbus_daemon_unregister_object (priv->dbus_daemon, G_OBJECT (content));
  tp_clear_object (&priv->dbus_daemon);

  for (l = priv->streams; l != NULL; l = g_list_next (l))
    {
      g_object_unref (l->data);
    }

  tp_clear_pointer (&priv->streams, g_list_free);

  if (priv->offer_cancellable != NULL)
    g_cancellable_cancel (priv->offer_cancellable);
  else
    maybe_finish_deinit (content);
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
      arr = call_content_codec_list_to_array (priv->local_codecs);
      g_hash_table_insert (map,
        GUINT_TO_POINTER (TP_BASE_CONNECTION (priv->conn)->self_handle),
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

  local_codecs = gabble_call_content_codec_offer_offer_finish (
    GABBLE_CALL_CONTENT_CODEC_OFFER (source), result, &error);

  if (error != NULL || priv->deinit_has_run ||
      priv->offer != GABBLE_CALL_CONTENT_CODEC_OFFER (source))
    goto out;

  call_content_set_local_codecs (self, local_codecs);

  codec_map = call_content_generate_codec_map (self, TRUE);
  empty = g_array_new (FALSE, FALSE, sizeof (TpHandle));

  gabble_svc_call_content_interface_media_emit_codecs_changed (self,
    codec_map, empty);

  g_hash_table_unref (codec_map);
  g_array_free (empty, TRUE);

out:
  if (priv->offer == GABBLE_CALL_CONTENT_CODEC_OFFER (source))
    {
      priv->offer = NULL;
      priv->offer_cancellable = NULL;
    }

  --priv->offer_count;
  g_object_unref (source);

  if (priv->deinit_has_run)
    maybe_finish_deinit (self);
}

static void
call_content_new_offer (GabbleCallContent *self)
{
  GabbleCallContentPrivate *priv = self->priv;
  GHashTable *map;
  gchar *path;

  map = call_content_generate_codec_map (self, FALSE);

  if (priv->offer != NULL)
    g_cancellable_cancel (priv->offer_cancellable);

  path = g_strdup_printf ("%s/Offer%d",
    priv->object_path, priv->offers++);

  priv->offer = gabble_call_content_codec_offer_new (priv->dbus_daemon, path,
      map);
  priv->offer_cancellable = g_cancellable_new ();
  ++priv->offer_count;
  gabble_call_content_codec_offer_offer (priv->offer, priv->offer_cancellable,
    codec_offer_finished_cb, self);

  gabble_svc_call_content_interface_media_emit_new_codec_offer (
    self, path, map);

  g_hash_table_unref (map);
  g_free (path);

  /* set this to TRUE so that after the initial offer disappears,
   * UpdateCodecs is allowed to be called. */
  priv->initial_offer_appeared = TRUE;
}

const gchar *
gabble_call_content_get_name (GabbleCallContent *self)
{
  return self->priv->name;
}

GList *
gabble_call_content_get_local_codecs (GabbleCallContent *self)
{
  return self->priv->local_codecs;
}

JingleMediaType
gabble_call_content_get_media_type (GabbleCallContent *self)
{
  return self->priv->mtype;
}

static void
member_content_codecs_changed (GabbleCallMemberContent *mcontent,
  gpointer user_data)
{
  GabbleCallContent *self = GABBLE_CALL_CONTENT (user_data);

  DEBUG ("Popping up new codec offer");
  call_content_new_offer (self);
}

static void
call_content_setup_jingle (GabbleCallContent *self,
    GabbleCallMemberContent *mcontent)
{
  GabbleCallContentPrivate *priv = self->priv;
  GabbleJingleContent *jingle;
  GabbleCallStream *stream;
  gchar *path;
  GPtrArray *paths;

  jingle = gabble_call_member_content_get_jingle_content (mcontent);

  if (jingle == NULL)
    return;

  path = g_strdup_printf ("%s/Stream%p", priv->object_path, jingle);
  stream = g_object_new (GABBLE_TYPE_CALL_STREAM,
      "object-path", path,
      "connection", priv->conn,
      "jingle-content", jingle,
      NULL);

  jingle_media_rtp_set_local_codecs (GABBLE_JINGLE_MEDIA_RTP (jingle),
      jingle_media_rtp_copy_codecs (priv->local_codecs), TRUE, NULL);

  priv->streams = g_list_prepend (priv->streams, stream);

  paths = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
  g_ptr_array_add (paths, path);

  gabble_svc_call_content_emit_streams_added (self, paths);

  g_ptr_array_unref (paths);
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
  GabbleJingleContent *content;
  GList *l;
  GPtrArray *paths;

  priv->contents = g_list_remove (priv->contents, mcontent);

  content = gabble_call_member_content_get_jingle_content (mcontent);

  for (l = priv->streams; l != NULL; l = g_list_next (l))
    {
      GabbleCallStream *stream = GABBLE_CALL_STREAM (l->data);

      if (content == gabble_call_stream_get_jingle_content (stream))
        {
          priv->streams = g_list_delete_link (priv->streams, l);

          paths = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
          g_ptr_array_add (paths, g_strdup (
                  gabble_call_stream_get_object_path (stream)));
          gabble_svc_call_content_emit_streams_removed (self, paths);
          g_ptr_array_unref (paths);

          g_object_unref (stream);
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

  call_content_new_offer (self);
}

GList *
gabble_call_content_get_member_contents (GabbleCallContent *self)
{
  return self->priv->contents;
}
