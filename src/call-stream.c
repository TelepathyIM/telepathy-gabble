/*
 * gabble-call-stream.c - Source for GabbleCallStream
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
#include <telepathy-glib/gtypes.h>
#include <extensions/extensions.h>

#include "call-stream.h"
#include "call-stream-endpoint.h"
#include "jingle-content.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void call_stream_iface_init (gpointer, gpointer);
static void call_stream_media_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(GabbleCallStream, gabble_call_stream,
  G_TYPE_OBJECT,
   G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
   G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_STREAM,
    call_stream_iface_init);
   G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_STREAM_INTERFACE_MEDIA,
    call_stream_media_iface_init);
  );

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_JINGLE_CONTENT,
  PROP_LOCAL_CANDIDATES,

  PROP_ENDPOINTS
};

#if 0
/* signal enum */
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif


/* private structure */
struct _GabbleCallStreamPrivate
{
  gboolean dispose_has_run;

  gchar *object_path;
  GabbleJingleContent *content;
  GList *endpoints;
};

static void
gabble_call_stream_init (GabbleCallStream *self)
{
  GabbleCallStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_STREAM, GabbleCallStreamPrivate);

  self->priv = priv;
}

static void gabble_call_stream_dispose (GObject *object);
static void gabble_call_stream_finalize (GObject *object);

static void
gabble_call_stream_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallStream *stream = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = stream->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_JINGLE_CONTENT:
        g_value_set_object (value, priv->content);
        break;
      case PROP_LOCAL_CANDIDATES:
        {
          GPtrArray *arr;
          GList *candidates =
            gabble_jingle_content_get_local_candidates (priv->content);

          arr = gabble_call_candidates_to_array (candidates);
          g_value_set_boxed (value, arr);
          g_ptr_array_unref (arr);
          break;
        }

      case PROP_ENDPOINTS:
        {
          GPtrArray *arr = g_ptr_array_sized_new (1);
          GList *l;

          for (l = priv->endpoints; l != NULL; l = g_list_next (l))
            {
              GabbleCallStreamEndpoint *e =
                GABBLE_CALL_STREAM_ENDPOINT (l->data);
              g_ptr_array_add (arr,
                (gpointer) gabble_call_stream_endpoint_get_object_path (e));
            }

          g_value_set_boxed (value, arr);
          g_ptr_array_free (arr, TRUE);
          break;
        }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_stream_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallStream *stream = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = stream->priv;

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

static void
gabble_call_stream_constructed (GObject *obj)
{
  GabbleCallStreamPrivate *priv;
  DBusGConnection *bus;
  GabbleCallStreamEndpoint *endpoint;
  gchar *path;

  priv = GABBLE_CALL_STREAM (obj)->priv;

  /* register object on the bus */
  bus = tp_get_bus ();
  DEBUG ("Registering %s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* Currently we'll only have one endpoint we know right away */
  path = g_strdup_printf ("%s/Endpoint", priv->object_path);
  endpoint = gabble_call_stream_endpoint_new (path, priv->content);
  priv->endpoints = g_list_append (priv->endpoints, endpoint);
  g_free (path);

  if (G_OBJECT_CLASS (gabble_call_stream_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_stream_parent_class)->constructed (obj);
}

static void
gabble_call_stream_class_init (GabbleCallStreamClass *gabble_call_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_stream_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl stream_props[] = {
    { NULL }
  };
  static TpDBusPropertiesMixinPropImpl stream_media_props[] = {
    { "LocalCandidates", "local-candidates", NULL },
    { "Endpoints", "endpoints", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_CALL_STREAM,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_props,
      },
      { GABBLE_IFACE_CALL_STREAM_INTERFACE_MEDIA,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_media_props,
      },
      { NULL }
  };

  g_type_class_add_private (gabble_call_stream_class,
    sizeof (GabbleCallStreamPrivate));

  object_class->set_property = gabble_call_stream_set_property;
  object_class->get_property = gabble_call_stream_get_property;

  object_class->dispose = gabble_call_stream_dispose;
  object_class->finalize = gabble_call_stream_finalize;
  object_class->constructed = gabble_call_stream_constructed;

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

  param_spec = g_param_spec_boxed ("local-candidates", "LocalCandidates",
      "List of local candidates",
      GABBLE_ARRAY_TYPE_CANDIDATE_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_CANDIDATES,
      param_spec);

  param_spec = g_param_spec_boxed ("endpoints", "Endpoints",
      "The endpoints of this content",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ENDPOINTS,
      param_spec);

  gabble_call_stream_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleCallStreamClass, dbus_props_class));
}

void
gabble_call_stream_dispose (GObject *object)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->content != NULL)
    g_object_unref (priv->content);

  priv->content = NULL;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_call_stream_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_stream_parent_class)->dispose (object);
}

void
gabble_call_stream_finalize (GObject *object)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_free (priv->object_path);

  G_OBJECT_CLASS (gabble_call_stream_parent_class)->finalize (object);
}

static void
gabble_call_stream_add_candidates (GabbleSvcCallStreamInterfaceMedia *iface,
    const GPtrArray *candidates,
    DBusGMethodInvocation *context)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (iface);
  GabbleCallStreamPrivate *priv = self->priv;
  GList *l = NULL;
  guint i;

  for (i = 0; i < candidates->len ; i++)
    {
      GValueArray *va;
      JingleCandidate *c;
      GHashTable *info;

      va = g_ptr_array_index (candidates, i);

      info = g_value_get_boxed (va->values + 3);

      c = jingle_candidate_new (
        /* transport protocol */
        tp_asv_get_uint32 (info, "Protocol", NULL),
        /* Candidate type */
        tp_asv_get_uint32 (info, "Type", NULL),
        /* id/foundation */
        tp_asv_get_string (info, "Foundation"),
        /* component */
        g_value_get_uint (va->values + 0),
        /* ip */
        g_value_get_string (va->values + 1),
        /* port */
        g_value_get_uint (va->values + 2),
        /* generation */
        0,
        /* preference */
        tp_asv_get_uint32 (info, "Priority", NULL) / 65536.0,
        /* username, password */
        tp_asv_get_string (info, "Username"),
        tp_asv_get_string (info, "Password"),
        /* network */
        0);

      l = g_list_append (l, c);
    }

  gabble_jingle_content_add_candidates (priv->content, l);

  gabble_svc_call_stream_interface_media_return_from_add_candidates (context);
}

static void
call_stream_iface_init (gpointer g_iface, gpointer iface_data)
{
}

static void
call_stream_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcCallStreamInterfaceMediaClass *klass =
    (GabbleSvcCallStreamInterfaceMediaClass *) g_iface;

#define IMPLEMENT(x) gabble_svc_call_stream_interface_media_implement_##x (\
    klass, gabble_call_stream_##x)
  IMPLEMENT(add_candidates);
#undef IMPLEMENT
}

const gchar *
gabble_call_stream_get_object_path (GabbleCallStream *stream)
{
  return stream->priv->object_path;
}
