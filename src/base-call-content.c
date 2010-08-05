/*
 * base-call-content.c - Source for GabbleBaseCallContent
 * Copyright © 2009–2010 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 * @author Will Thompson <will.thompson@collabora.co.uk>
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

#include "base-call-content.h"

#include "base-call-stream.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

G_DEFINE_TYPE_WITH_CODE(GabbleBaseCallContent, gabble_base_call_content,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    /* The base class doesn't implement Remove(), which is pretty
     * protocol-specific. It just implements the properties.
     */
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_CONTENT,
        NULL);
    );

struct _GabbleBaseCallContentPrivate
{
  GabbleConnection *conn;
  TpDBusDaemon *dbus_daemon;

  gchar *object_path;

  gchar *name;
  TpMediaStreamType media_type;
  TpHandle creator;
  GabbleCallContentDisposition disposition;

  GList *streams;

  gboolean dispose_has_run;
  gboolean deinit_has_run;
};

enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CONNECTION,

  PROP_NAME,
  PROP_MEDIA_TYPE,
  PROP_CREATOR,
  PROP_DISPOSITION,
  PROP_STREAMS
};

static void base_call_content_deinit_real (GabbleBaseCallContent *self);

static void
gabble_base_call_content_init (GabbleBaseCallContent *self)
{
  GabbleBaseCallContentPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BASE_CALL_CONTENT, GabbleBaseCallContentPrivate);

  self->priv = priv;
}

static void
gabble_base_call_content_constructed (GObject *obj)
{
  GabbleBaseCallContent *self = GABBLE_BASE_CALL_CONTENT (obj);
  GabbleBaseCallContentPrivate *priv = self->priv;

  if (G_OBJECT_CLASS (gabble_base_call_content_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_base_call_content_parent_class)->constructed (obj);

  DEBUG ("Registering %s", priv->object_path);
  priv->dbus_daemon = g_object_ref (
      tp_base_connection_get_dbus_daemon ((TpBaseConnection *) priv->conn));
  tp_dbus_daemon_register_object (priv->dbus_daemon, priv->object_path, obj);
}

static void
gabble_base_call_content_dispose (GObject *object)
{
  GabbleBaseCallContent *self = GABBLE_BASE_CALL_CONTENT (object);
  GabbleBaseCallContentPrivate *priv = self->priv;
  GList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  for (l = priv->streams; l != NULL; l = g_list_next (l))
    g_object_unref (l->data);

  tp_clear_pointer (&priv->streams, g_list_free);
  tp_clear_object (&priv->conn);

  if (G_OBJECT_CLASS (gabble_base_call_content_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (gabble_base_call_content_parent_class)->dispose (object);
}

static void
gabble_base_call_content_finalize (GObject *object)
{
  GabbleBaseCallContent *self = GABBLE_BASE_CALL_CONTENT (object);
  GabbleBaseCallContentPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_free (priv->object_path);
  g_free (priv->name);

  G_OBJECT_CLASS (gabble_base_call_content_parent_class)->finalize (object);
}

static void
gabble_base_call_content_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleBaseCallContent *content = GABBLE_BASE_CALL_CONTENT (object);
  GabbleBaseCallContentPrivate *priv = content->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
      case PROP_MEDIA_TYPE:
        g_value_set_uint (value, priv->media_type);
        break;
      case PROP_CREATOR:
        g_value_set_uint (value, priv->creator);
        break;
      case PROP_DISPOSITION:
        g_value_set_uint (value, priv->disposition);
        break;
      case PROP_STREAMS:
        {
          GPtrArray *arr = g_ptr_array_sized_new (2);
          GList *l;

          for (l = priv->streams; l != NULL; l = g_list_next (l))
            {
              GabbleBaseCallStream *s = GABBLE_BASE_CALL_STREAM (l->data);
              g_ptr_array_add (arr,
                  g_strdup (gabble_base_call_stream_get_object_path (s)));
            }

          g_value_take_boxed (value, arr);
          break;
        }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_base_call_content_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleBaseCallContent *content = GABBLE_BASE_CALL_CONTENT (object);
  GabbleBaseCallContentPrivate *priv = content->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        priv->object_path = g_value_dup_string (value);
        g_assert (priv->object_path != NULL);
        break;
      case PROP_CONNECTION:
        priv->conn = g_value_dup_object (value);
        break;
      case PROP_NAME:
        priv->name = g_value_dup_string (value);
        break;
      case PROP_MEDIA_TYPE:
        priv->media_type = g_value_get_uint (value);
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
gabble_base_call_content_class_init (
    GabbleBaseCallContentClass *bcc_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (bcc_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl content_props[] = {
    { "Name", "name", NULL },
    { "Type", "media-type", NULL },
    { "Creator", "creator", NULL },
    { "Disposition", "disposition", NULL },
    { "Streams", "streams", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_CALL_CONTENT,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        content_props,
      },
      { NULL }
  };

  g_type_class_add_private (bcc_class, sizeof (GabbleBaseCallContentPrivate));

  object_class->constructed = gabble_base_call_content_constructed;
  object_class->dispose = gabble_base_call_content_dispose;
  object_class->finalize = gabble_base_call_content_finalize;
  object_class->get_property = gabble_base_call_content_get_property;
  object_class->set_property = gabble_base_call_content_set_property;

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this call content",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("name", "Name",
      "The name of this content, if any",
      "",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_uint ("media-type", "Media Type",
      "The media type of this content",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE, param_spec);

  param_spec = g_param_spec_uint ("creator", "Creator",
      "The creator of this content",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR, param_spec);

  param_spec = g_param_spec_uint ("disposition", "Disposition",
      "The disposition of this content",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DISPOSITION, param_spec);

  param_spec = g_param_spec_boxed ("streams", "Stream",
      "The streams of this content",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STREAMS,
      param_spec);

  bcc_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleBaseCallContentClass, dbus_props_class));

  bcc_class->deinit = base_call_content_deinit_real;
}

GabbleConnection *
gabble_base_call_content_get_connection (GabbleBaseCallContent *self)
{
  g_return_val_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self), NULL);

  return self->priv->conn;
}

const gchar *
gabble_base_call_content_get_object_path (GabbleBaseCallContent *self)
{
  g_return_val_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self), NULL);

  return self->priv->object_path;
}

const gchar *
gabble_base_call_content_get_name (GabbleBaseCallContent *self)
{
  g_return_val_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self), NULL);

  return self->priv->name;
}

TpMediaStreamType
gabble_base_call_content_get_media_type (GabbleBaseCallContent *self)
{
  g_return_val_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self),
      TP_MEDIA_STREAM_TYPE_AUDIO);

  return self->priv->media_type;
}

GabbleCallContentDisposition
gabble_base_call_content_get_disposition (GabbleBaseCallContent *self)
{
  g_return_val_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self),
      GABBLE_CALL_CONTENT_DISPOSITION_NONE);

  return self->priv->disposition;
}

GList *
gabble_base_call_content_get_streams (GabbleBaseCallContent *self)
{
  g_return_val_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self), NULL);

  return self->priv->streams;
}

void
gabble_base_call_content_add_stream (GabbleBaseCallContent *self,
    GabbleBaseCallStream *stream)
{
  GPtrArray *paths;

  g_return_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self));

  self->priv->streams = g_list_prepend (self->priv->streams,
      g_object_ref (stream));

  paths = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);

  g_ptr_array_add (paths, g_strdup (
     gabble_base_call_stream_get_object_path (
         GABBLE_BASE_CALL_STREAM (stream))));
  gabble_svc_call_content_emit_streams_added (self, paths);
  g_ptr_array_unref (paths);
}

void
gabble_base_call_content_remove_stream (GabbleBaseCallContent *self,
    GabbleBaseCallStream *stream)
{
  GabbleBaseCallContentPrivate *priv;
  GList *l;
  GPtrArray *paths;

  g_return_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self));

  priv = self->priv;

  l = g_list_find (priv->streams, stream);
  g_return_if_fail (l != NULL);

  priv->streams = g_list_remove_link (priv->streams, l);
  paths = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
  g_ptr_array_add (paths, g_strdup (
     gabble_base_call_stream_get_object_path (
         GABBLE_BASE_CALL_STREAM (stream))));
  gabble_svc_call_content_emit_streams_removed (self, paths);
  g_ptr_array_unref (paths);
  g_object_unref (stream);
}

static void
base_call_content_deinit_real (GabbleBaseCallContent *self)
{
  GabbleBaseCallContentPrivate *priv = self->priv;

  if (priv->deinit_has_run)
    return;

  priv->deinit_has_run = TRUE;

  tp_dbus_daemon_unregister_object (priv->dbus_daemon, G_OBJECT (self));
  tp_clear_object (&priv->dbus_daemon);

  g_list_foreach (priv->streams, (GFunc) g_object_unref, NULL);
  tp_clear_pointer (&priv->streams, g_list_free);
}

void
gabble_base_call_content_deinit (GabbleBaseCallContent *self)
{
  GabbleBaseCallContentClass *klass;

  g_return_if_fail (GABBLE_IS_BASE_CALL_CONTENT (self));

  klass = GABBLE_BASE_CALL_CONTENT_GET_CLASS (self);
  g_return_if_fail (klass->deinit != NULL);
  klass->deinit (self);
}
