/*
 * tube-stream.c - Source for GabbleTubeStream
 * Copyright (C) 2007 Ltd.
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

#include "tube-stream.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "namespaces.h"
#include <telepathy-glib/svc-unstable.h>
#include "util.h"
#include "tube-iface.h"
#include "bytestream-ibb.h"
#include "gabble-signals-marshal.h"

static void
tube_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleTubeStream, gabble_tube_stream, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_TUBE_IFACE, tube_iface_init));

/* signals */
enum
{
  OPENED,
  CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_BYTESTREAM,
  PROP_TYPE,
  PROP_INITIATOR,
  PROP_SERVICE,
  PROP_PARAMETERS,
  PROP_STATE,
  LAST_PROPERTY
};

typedef struct _GabbleTubeStreamPrivate GabbleTubeStreamPrivate;
struct _GabbleTubeStreamPrivate
{
  GabbleConnection *conn;
  GabbleBytestreamIBB *bytestream;
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;

  gboolean dispose_has_run;
};

#define GABBLE_TUBE_STREAM_GET_PRIVATE(obj) \
    ((GabbleTubeStreamPrivate *) obj->priv)

static void data_received_cb (GabbleBytestreamIBB *ibb, TpHandle sender,
    GString *data, gpointer user_data);

static void
tube_stream_open (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  g_signal_connect (priv->bytestream, "data-received",
      G_CALLBACK (data_received_cb), self);
}

static void
gabble_tube_stream_init (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TUBE_STREAM, GabbleTubeStreamPrivate);

  self->priv = priv;

  priv->bytestream = NULL;
  priv->dispose_has_run = FALSE;
}

static TpTubeState
get_tube_state (GabbleTubeStream *self)
{
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  BytestreamIBBState bytestream_state;

  if (priv->bytestream == NULL)
    /* bytestream not yet created as we're waiting for the SI reply */
    return TP_TUBE_STATE_REMOTE_PENDING;

  g_object_get (priv->bytestream, "state", &bytestream_state, NULL);

  if (bytestream_state == BYTESTREAM_IBB_STATE_OPEN)
    return TP_TUBE_STATE_OPEN;

  else if (bytestream_state == BYTESTREAM_IBB_STATE_LOCAL_PENDING ||
      bytestream_state == BYTESTREAM_IBB_STATE_ACCEPTED)
    return TP_TUBE_STATE_LOCAL_PENDING;

  else if (bytestream_state == BYTESTREAM_IBB_STATE_INITIATING)
    return TP_TUBE_STATE_REMOTE_PENDING;

  else
    g_assert_not_reached ();
}

static void
bytestream_state_changed_cb (GabbleBytestreamIBB *bytestream,
                             BytestreamIBBState state,
                             gpointer user_data)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  if (state == BYTESTREAM_IBB_STATE_CLOSED)
    {
      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
      priv->bytestream = NULL;
    }
  else if (state == BYTESTREAM_IBB_STATE_OPEN)
    {
      tube_stream_open (self);
      g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
    }
}

static void
gabble_tube_stream_dispose (GObject *object)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  if (priv->bytestream)
    {
      gabble_bytestream_ibb_close (priv->bytestream);
      priv->bytestream  = NULL;
    }

  tp_handle_unref (contact_repo, priv->initiator);

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_tube_stream_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_tube_stream_parent_class)->dispose (object);
}

static void
gabble_tube_stream_finalize (GObject *object)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  g_free (priv->service);
  g_hash_table_destroy (priv->parameters);

  G_OBJECT_CLASS (gabble_tube_stream_parent_class)->finalize (object);
}

static void
gabble_tube_stream_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_BYTESTREAM:
        g_value_set_object (value, priv->bytestream);
        break;
      case PROP_TYPE:
        g_value_set_uint (value, TP_TUBE_TYPE_STREAM);
        break;
      case PROP_INITIATOR:
        g_value_set_uint (value, priv->initiator);
        break;
      case PROP_SERVICE:
        g_value_set_string (value, priv->service);
        break;
      case PROP_PARAMETERS:
        g_value_set_boxed (value, priv->parameters);
        break;
      case PROP_STATE:
        g_value_set_uint (value, get_tube_state (self));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_tube_stream_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (object);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_BYTESTREAM:
        if (priv->bytestream == NULL)
          {
            BytestreamIBBState state;
            priv->bytestream = g_value_get_object (value);

            g_object_get (priv->bytestream, "state", &state, NULL);
            if (state == BYTESTREAM_IBB_STATE_OPEN)
              {
                tube_stream_open (self);
              }

            g_signal_connect (priv->bytestream, "state-changed",
                G_CALLBACK (bytestream_state_changed_cb), self);
          }
        break;
      case PROP_INITIATOR:
        priv->initiator = g_value_get_uint (value);
        break;
      case PROP_SERVICE:
        g_free (priv->service);
        priv->service = g_value_dup_string (value);
        break;
      case PROP_PARAMETERS:
        priv->parameters = g_value_get_boxed (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_tube_stream_constructor (GType type,
                              guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  GabbleTubeStreamPrivate *priv;
  TpHandleRepoIface *contact_repo;

  obj = G_OBJECT_CLASS (gabble_tube_stream_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_TUBE_STREAM_GET_PRIVATE (GABBLE_TUBE_STREAM (obj));

  /* Ref the initiator handle */
  g_assert (priv->conn != NULL);
  g_assert (priv->initiator != 0);
  contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  tp_handle_ref (contact_repo, priv->initiator);

  return obj;
}

static void
gabble_tube_stream_class_init (GabbleTubeStreamClass *gabble_tube_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_tube_stream_class);
  GParamSpec *param_spec;

  object_class->get_property = gabble_tube_stream_get_property;
  object_class->set_property = gabble_tube_stream_set_property;
  object_class->constructor = gabble_tube_stream_constructor;

  g_type_class_add_private (gabble_tube_stream_class,
      sizeof (GabbleTubeStreamPrivate));

  object_class->dispose = gabble_tube_stream_dispose;
  object_class->finalize = gabble_tube_stream_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this D-Bus tube object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "bytestream",
      "GabbleBytestreamIBB object",
      "Gabble bytestream IBB object used for streaming data for this D-Bus"
      "tube object.",
      GABBLE_TYPE_BYTESTREAM_IBB,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_BYTESTREAM, param_spec);

  param_spec = g_param_spec_uint (
      "type",
      "Tube type",
      "The TpTubeType this D-Bus tube object.",
      0, G_MAXUINT32, TP_TUBE_TYPE_STREAM,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TYPE, param_spec);

  param_spec = g_param_spec_uint (
      "initiator",
      "Initiator handle",
      "The TpHandle of the initiator of this D-Bus tube object.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_string (
      "service",
      "service name",
      "the service associated with this D-BUS tube object.",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SERVICE, param_spec);

  param_spec = g_param_spec_boxed (
      "parameters",
      "parameters GHashTable",
      "GHashTable containing parameters of this STREAM tube object.",
      G_TYPE_HASH_TABLE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PARAMETERS, param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "Tube state",
      "The TpTubeState of this STREAM tube object",
      0, G_MAXUINT32, TP_TUBE_STATE_REMOTE_PENDING,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[OPENED] =
    g_signal_new ("opened",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
data_received_cb (GabbleBytestreamIBB *ibb,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  /*
  GabbleTubeStream *tube = GABBLE_TUBE_STREAM (user_data);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (tube);
  */
}

/**
 * gabble_tube_stream_get_stream_id
 *
 * Implements gabble_tube_iface_get_stream_id on GabbleTubeIface
 */
static gchar *
gabble_tube_stream_get_stream_id (GabbleTubeIface *tube)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  gchar *stream_id;

  if (priv->bytestream == NULL)
    return NULL;

  g_object_get (priv->bytestream, "stream-id", &stream_id, NULL);
  return stream_id;
}

/**
 * gabble_tube_stream_accept
 *
 * Implements gabble_tube_iface_accept on GabbleTubeIface
 */
static void
gabble_tube_stream_accept (GabbleTubeIface *tube)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);
  BytestreamIBBState state;
  const gchar *stream_init_id;

  g_assert (priv->bytestream != NULL);

  g_object_get (priv->bytestream,
      "state", &state,
      NULL);

  if (state != BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    return;

  g_object_get (priv->bytestream,
      "stream-init-id", &stream_init_id,
      NULL);

  if (stream_init_id != NULL)
    {
      /* Bytestream was created using a SI request so
       * we have to accept it */
      LmMessage *msg;
      LmMessageNode *si, *tube_node;

      DEBUG ("accept the SI request");

      msg = gabble_bytestream_ibb_make_accept_iq (priv->bytestream);

      si = lm_message_node_get_child_with_namespace (msg->node, "si",
          NS_SI);
      g_assert (si != NULL);

      tube_node = lm_message_node_add_child (si, "tube", "");
      lm_message_node_set_attribute (tube_node, "xmlns", NS_SI_TUBES);

      gabble_bytestream_ibb_accept (priv->bytestream, msg);

      lm_message_unref (msg);
    }
  else
    {
      /* No SI so the bytestream is open */
      DEBUG ("no SI, bytestream open");
      g_object_set (priv->bytestream,
          "state", BYTESTREAM_IBB_STATE_OPEN,
          NULL);
    }
}

/**
 * gabble_tube_stream_close
 *
 * Implements gabble_tube_iface_close on GabbleTubeIface
 */
static void
gabble_tube_stream_close (GabbleTubeIface *tube)
{
  GabbleTubeStream *self = GABBLE_TUBE_STREAM (tube);
  GabbleTubeStreamPrivate *priv = GABBLE_TUBE_STREAM_GET_PRIVATE (self);

  gabble_bytestream_ibb_close (priv->bytestream);
  priv->bytestream = NULL;
}

static void
tube_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  GabbleTubeIfaceClass *klass = (GabbleTubeIfaceClass *) g_iface;

  klass->get_stream_id = gabble_tube_stream_get_stream_id;
  klass->accept = gabble_tube_stream_accept;
  klass->close = gabble_tube_stream_close;
}
