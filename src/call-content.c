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

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/base-connection.h>
#include <extensions/extensions.h>

#include "call-content.h"
#include "jingle-content.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void call_content_iface_init (gpointer, gpointer);
static void call_content_media_iface_init (gpointer, gpointer);

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
  PROP_JINGLE_CONTENT,
};

#if 0
/* signal enum */
enum
{
    STREAM_ADDED,
    STREAM_REMOVED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

/* private structure */
struct _GabbleCallContentPrivate
{
  gchar *object_path;
  GabbleJingleContent *content;

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
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_JINGLE_CONTENT:
        priv->content = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_call_content_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *obj;
  GabbleCallContentPrivate *priv;
  DBusGConnection *bus;

  obj = G_OBJECT_CLASS (gabble_call_content_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_CALL_CONTENT (obj)->priv;

  /* register object on the bus */
  bus = tp_get_bus ();
  DEBUG ("Registering %s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_call_content_class_init (
    GabbleCallContentClass *gabble_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_content_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_call_content_class,
    sizeof (GabbleCallContentPrivate));

  object_class->constructor = gabble_call_content_constructor;

  object_class->get_property = gabble_call_content_get_property;
  object_class->set_property = gabble_call_content_set_property;

  object_class->dispose = gabble_call_content_dispose;
  object_class->finalize = gabble_call_content_finalize;

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this "
      "object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_object ("jingle-content", "Jingle Content",
      "The Jingle Content related to this content object",
      GABBLE_TYPE_JINGLE_CONTENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_JINGLE_CONTENT,
      param_spec);
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

  G_OBJECT_CLASS (gabble_call_content_parent_class)->finalize (object);
}

static void
call_content_iface_init (gpointer g_iface, gpointer iface_data)
{
}

static void
call_content_media_iface_init (gpointer g_iface, gpointer iface_data)
{
}

const gchar *
gabble_call_content_get_object_path (GabbleCallContent *content)
{
  return content->priv->object_path;
}
