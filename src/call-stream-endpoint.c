/*
 * gabble-call-stream-endpoint.c - Source for GabbleCallStreamEndpoint
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
#include <util.h>


#include <telepathy-glib/dbus.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/svc-properties-interface.h>

#include "call-stream-endpoint.h"
#include <extensions/extensions.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

static void call_stream_endpoint_iface_init (gpointer, gpointer);

static void call_stream_endpoint_new_candidates_cb (
    GabbleJingleContent *content,
    GList *candidates,
    gpointer user_data);

G_DEFINE_TYPE_WITH_CODE(GabbleCallStreamEndpoint,
  gabble_call_stream_endpoint,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_STREAM_ENDPOINT,
        call_stream_endpoint_iface_init);
   G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
);

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_JINGLE_CONTENT,
  PROP_REMOTE_CANDIDATES,
  PROP_REMOTE_CREDENTIALS,
  PROP_SELECTED_CANDIDATE,
  PROP_STREAM_STATE,
  PROP_TRANSPORT,
};

struct _GabbleCallStreamEndpointPrivate
{
  gboolean dispose_has_run;

  gchar *object_path;
  GabbleJingleContent *content;
  GValueArray *remote_credentials;
  GValueArray *selected_candidate;
  guint stream_state;
};

static void
gabble_call_stream_endpoint_init (GabbleCallStreamEndpoint *self)
{
  GabbleCallStreamEndpointPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_STREAM_ENDPOINT,
      GabbleCallStreamEndpointPrivate);

  self->priv = priv;

  priv->selected_candidate = gabble_value_array_build (4,
      G_TYPE_UINT, 0,
      G_TYPE_STRING, "",
      G_TYPE_UINT, 0,
      GABBLE_HASH_TYPE_CANDIDATE_INFO,
          g_hash_table_new (g_str_hash, g_str_equal),
      G_TYPE_INVALID);
}

static void gabble_call_stream_endpoint_dispose (GObject *object);
static void gabble_call_stream_endpoint_finalize (GObject *object);

static void
gabble_call_stream_endpoint_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallStreamEndpoint *endpoint = GABBLE_CALL_STREAM_ENDPOINT (object);
  GabbleCallStreamEndpointPrivate *priv = endpoint->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_JINGLE_CONTENT:
        g_value_set_object (value, priv->content);
        break;
      case PROP_REMOTE_CANDIDATES:
        {
          GPtrArray *arr;
          GList *candidates =
            gabble_jingle_content_get_remote_candidates (priv->content);

          arr = gabble_call_candidates_to_array (candidates);
          g_value_set_boxed (value, arr);
          g_ptr_array_unref (arr);
          break;
        }
      case PROP_REMOTE_CREDENTIALS:
        {
          if (priv->remote_credentials == NULL)
            {
              GValueArray *va = gabble_value_array_build (2,
                  G_TYPE_STRING, "",
                  G_TYPE_STRING, "",
                  G_TYPE_INVALID);
              g_value_set_boxed (value, va);
              g_boxed_free (G_TYPE_VALUE_ARRAY, va);
            }
          else
            {
              g_value_set_boxed (value, priv->remote_credentials);
            }
          break;
        }
      case PROP_SELECTED_CANDIDATE:
        g_value_set_boxed (value, priv->selected_candidate);
        break;
      case PROP_STREAM_STATE:
        g_value_set_uint (value, priv->stream_state);
        break;
      case PROP_TRANSPORT:
        {
          GabbleStreamTransportType type = 0;

          switch (gabble_jingle_content_get_transport_type (priv->content))
            {
            case JINGLE_TRANSPORT_GOOGLE_P2P:
                type = GABBLE_STREAM_TRANSPORT_TYPE_GTALK_P2P;
                break;
            case JINGLE_TRANSPORT_RAW_UDP:
                type = GABBLE_STREAM_TRANSPORT_TYPE_RAW_UDP;
                break;
            case JINGLE_TRANSPORT_ICE_UDP:
                type = GABBLE_STREAM_TRANSPORT_TYPE_ICE;
                break;
            case JINGLE_TRANSPORT_UNKNOWN:
            default:
                g_assert_not_reached ();
            }

          g_value_set_uint (value, type);
          break;
        }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_stream_endpoint_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallStreamEndpoint *endpoint = GABBLE_CALL_STREAM_ENDPOINT (object);
  GabbleCallStreamEndpointPrivate *priv = endpoint->priv;

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
gabble_call_stream_endpoint_constructed (GObject *obj)
{
  GabbleCallStreamEndpointPrivate *priv;
  DBusGConnection *bus;

  priv = GABBLE_CALL_STREAM_ENDPOINT (obj)->priv;

  /* register object on the bus */
  bus = tp_get_bus ();
  DEBUG ("Registering %s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  gabble_signal_connect_weak (priv->content, "new-candidates",
    G_CALLBACK (call_stream_endpoint_new_candidates_cb), obj);

  if (G_OBJECT_CLASS (gabble_call_stream_endpoint_parent_class)->constructed
      != NULL)
    G_OBJECT_CLASS (gabble_call_stream_endpoint_parent_class)->constructed (
      obj);
}

static void
gabble_call_stream_endpoint_class_init (
  GabbleCallStreamEndpointClass *gabble_call_stream_endpoint_class)
{
  GObjectClass *object_class =
      G_OBJECT_CLASS (gabble_call_stream_endpoint_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl endpoint_props[] = {
    { "RemoteCandidates", "remote-candidates", NULL },
    { "RemoteCredentials", "remote-credentials", NULL },
    { "SelectedCandidate", "selected-candidate", NULL },
    { "StreamState", "stream-state", NULL },
    { "Transport", "transport", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_CALL_STREAM_ENDPOINT,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        endpoint_props,
      },
      { NULL }
  };

  g_type_class_add_private (gabble_call_stream_endpoint_class,
      sizeof (GabbleCallStreamEndpointPrivate));

  object_class->dispose = gabble_call_stream_endpoint_dispose;
  object_class->finalize = gabble_call_stream_endpoint_finalize;
  object_class->constructed = gabble_call_stream_endpoint_constructed;

  object_class->set_property = gabble_call_stream_endpoint_set_property;
  object_class->get_property = gabble_call_stream_endpoint_get_property;

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

  param_spec = g_param_spec_boxed ("remote-candidates",
      "RemoteCandidates",
      "The remote candidates of this endpoint",
      GABBLE_ARRAY_TYPE_CANDIDATE_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_CANDIDATES,
      param_spec);

  param_spec = g_param_spec_boxed ("remote-credentials",
      "RemoteCredentials",
      "The remote credentials of this endpoint",
      dbus_g_type_get_struct ("GValueArray",
          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID),
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_CREDENTIALS,
      param_spec);

  param_spec = g_param_spec_boxed ("selected-candidate",
      "SelectedCandidate",
      "The candidate selected for this endpoint",
      GABBLE_STRUCT_TYPE_CANDIDATE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SELECTED_CANDIDATE,
      param_spec);

  param_spec = g_param_spec_uint ("stream-state", "StreamState",
      "The stream state of this endpoint.",
      0, G_MAXUINT32,
      TP_MEDIA_STREAM_STATE_DISCONNECTED,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_STATE,
      param_spec);

  param_spec = g_param_spec_uint ("transport",
      "Transport",
      "The transport type for the content of this endpoint.",
      0, NUM_GABBLE_STREAM_TRANSPORT_TYPES, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TRANSPORT, param_spec);

  gabble_call_stream_endpoint_class->dbus_props_class.interfaces =
      prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleCallStreamEndpointClass, dbus_props_class));
}

void
gabble_call_stream_endpoint_dispose (GObject *object)
{
  GabbleCallStreamEndpoint *self = GABBLE_CALL_STREAM_ENDPOINT (object);
  GabbleCallStreamEndpointPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->content != NULL)
    g_object_unref (priv->content);

  priv->content = NULL;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_call_stream_endpoint_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_stream_endpoint_parent_class)->dispose (
        object);
}

void
gabble_call_stream_endpoint_finalize (GObject *object)
{
  GabbleCallStreamEndpoint *self = GABBLE_CALL_STREAM_ENDPOINT (object);
  GabbleCallStreamEndpointPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_free (priv->object_path);

  g_boxed_free (GABBLE_STRUCT_TYPE_CANDIDATE, priv->selected_candidate);

  G_OBJECT_CLASS (gabble_call_stream_endpoint_parent_class)->finalize (object);
}

static void
call_stream_endpoint_new_candidates_cb (GabbleJingleContent *content,
    GList *candidates,
    gpointer user_data)
{
  GabbleCallStreamEndpoint *self = GABBLE_CALL_STREAM_ENDPOINT (user_data);
  GPtrArray *arr;

  if (candidates == NULL)
    return;

  arr = gabble_call_candidates_to_array (candidates);
  gabble_svc_call_stream_endpoint_emit_remote_candidates_added (self,
    arr);
  g_ptr_array_unref (arr);
}

static void
call_stream_endpoint_set_stream_state (GabbleSvcCallStreamEndpoint *iface,
    TpMediaStreamState state,
    DBusGMethodInvocation *context)
{
  GabbleCallStreamEndpoint *self = GABBLE_CALL_STREAM_ENDPOINT (iface);

  if (state >= NUM_TP_MEDIA_STREAM_STATES)
    {
      GError *error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Stream state %d is out of the valid range.", state);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  self->priv->stream_state = state;

  gabble_jingle_content_set_transport_state (self->priv->content,
    state);

  gabble_svc_call_stream_endpoint_emit_stream_state_changed (self, state);
  gabble_svc_call_stream_endpoint_return_from_set_stream_state (context);
}

static void
call_stream_endpoint_set_selected_candidate (
    GabbleSvcCallStreamEndpoint *iface,
    const GValueArray *candidate,
    DBusGMethodInvocation *context)
{
  GabbleCallStreamEndpoint *self = GABBLE_CALL_STREAM_ENDPOINT (iface);
  GValueArray *va = (GValueArray *) candidate;
  GValue *value;
  GError *error = NULL;

  if (candidate->n_values != 4)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "A candidate should have 4 values, got %d", candidate->n_values);
      goto error;
    }

  value = g_value_array_get_nth (va, 0);
  if (g_value_get_uint (value) > 2)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid component id: %d", g_value_get_uint (value));
      goto error;
    }

  value = g_value_array_get_nth (va, 1);
  if (g_value_get_string (value) == NULL ||
      g_value_get_string (value)[0] == 0)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid IP address: %s", g_value_get_string (value));
      goto error;
    }

  value = g_value_array_get_nth (va, 2);
  if (g_value_get_uint (value) > 65535)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid port: %d", g_value_get_uint (value));
      goto error;
    }

  g_boxed_free (GABBLE_STRUCT_TYPE_CANDIDATE,
      self->priv->selected_candidate);

  self->priv->selected_candidate =
      g_boxed_copy (GABBLE_STRUCT_TYPE_CANDIDATE, candidate);

  gabble_svc_call_stream_endpoint_emit_candidate_selected (self, candidate);
  gabble_svc_call_stream_endpoint_return_from_set_selected_candidate (context);
  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

static void
call_stream_endpoint_iface_init (gpointer iface, gpointer data)
{
  GabbleSvcCallStreamEndpointClass *klass =
    (GabbleSvcCallStreamEndpointClass *) iface;

  #define IMPLEMENT(x) gabble_svc_call_stream_endpoint_implement_##x (\
      klass, call_stream_endpoint_##x)
      IMPLEMENT(set_stream_state);
      IMPLEMENT(set_selected_candidate);
  #undef IMPLEMENT
}

GabbleCallStreamEndpoint *
gabble_call_stream_endpoint_new (const gchar *object_path,
  GabbleJingleContent *content)
{
  return g_object_new (GABBLE_TYPE_CALL_STREAM_ENDPOINT,
    "object-path", object_path,
    "jingle-content", content,
    NULL);
}

const gchar *
gabble_call_stream_endpoint_get_object_path (
    GabbleCallStreamEndpoint *endpoint)
{
  return endpoint->priv->object_path;
}
