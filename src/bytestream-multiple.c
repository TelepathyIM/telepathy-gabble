/*
 * bytestream-multiple.c - Source for GabbleBytestreamMultiple
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#include "config.h"
#include "bytestream-multiple.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_BYTESTREAM

#include "base64.h"
#include "bytestream-factory.h"
#include "bytestream-iface.h"
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleBytestreamMultiple, gabble_bytestream_multiple,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

/* signals */
enum
{
  DATA_RECEIVED,
  STATE_CHANGED,
  CONNECTION_ERROR,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_PEER_HANDLE,
  PROP_PEER_HANDLE_TYPE,
  PROP_STREAM_ID,
  PROP_STREAM_INIT_ID,
  PROP_PEER_JID,
  PROP_PEER_RESOURCE,
  PROP_STATE,
  PROP_PROTOCOL,
  PROP_CLOSE_ON_CONNECTION_ERROR,
  LAST_PROPERTY
};

struct _GabbleBytestreamMultiplePrivate
{
  GabbleConnection *conn;
  TpHandle peer_handle;
  gchar *stream_id;
  gchar *stream_init_id;
  gchar *peer_resource;
  GabbleBytestreamState state;
  gchar *peer_jid;
  gboolean close_on_connection_error;

  GList *bytestreams;

  gboolean dispose_has_run;
};

#define GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE(obj) ((obj)->priv)

static void gabble_bytestream_multiple_close (GabbleBytestreamIface *iface,
    GError *error);

static void
gabble_bytestream_multiple_init (GabbleBytestreamMultiple *self)
{
  GabbleBytestreamMultiplePrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_MULTIPLE, GabbleBytestreamMultiplePrivate);

  self->priv = priv;

  priv->close_on_connection_error = TRUE;
}

static void
gabble_bytestream_multiple_dispose (GObject *object)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_handle_unref (contact_repo, priv->peer_handle);

  if (priv->state != GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      gabble_bytestream_iface_close (GABBLE_BYTESTREAM_IFACE (self), NULL);
    }

  G_OBJECT_CLASS (gabble_bytestream_multiple_parent_class)->dispose (object);
}

static void
gabble_bytestream_multiple_finalize (GObject *object)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  g_free (priv->stream_id);
  g_free (priv->stream_init_id);
  g_free (priv->peer_resource);
  g_free (priv->peer_jid);

  G_OBJECT_CLASS (gabble_bytestream_multiple_parent_class)->finalize (object);
}

static void
gabble_bytestream_multiple_get_property (GObject *object,
                                         guint property_id,
                                         GValue *value,
                                         GParamSpec *pspec)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_PEER_HANDLE:
        g_value_set_uint (value, priv->peer_handle);
        break;
      case PROP_PEER_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_STREAM_ID:
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_STREAM_INIT_ID:
        g_value_set_string (value, priv->stream_init_id);
        break;
      case PROP_PEER_RESOURCE:
        g_value_set_string (value, priv->peer_resource);
        break;
      case PROP_PEER_JID:
        g_value_set_string (value, priv->peer_jid);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_PROTOCOL:
        g_value_set_string (value, NS_BYTESTREAMS);
        break;
      case PROP_CLOSE_ON_CONNECTION_ERROR:
        g_value_set_boolean (value, priv->close_on_connection_error);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_bytestream_multiple_set_property (GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_PEER_HANDLE:
        priv->peer_handle = g_value_get_uint (value);
        break;
      case PROP_STREAM_ID:
        g_free (priv->stream_id);
        priv->stream_id = g_value_dup_string (value);
        break;
      case PROP_STREAM_INIT_ID:
        g_free (priv->stream_init_id);
        priv->stream_init_id = g_value_dup_string (value);
        break;
      case PROP_PEER_RESOURCE:
        g_free (priv->peer_resource);
        priv->peer_resource = g_value_dup_string (value);
        break;
      case PROP_STATE:
        if (priv->state != g_value_get_uint (value))
          {
            priv->state = g_value_get_uint (value);
            g_signal_emit (object, signals[STATE_CHANGED], 0, priv->state);
          }
        break;
      case PROP_CLOSE_ON_CONNECTION_ERROR:
        priv->close_on_connection_error = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_bytestream_multiple_constructor (GType type,
                                        guint n_props,
                                        GObjectConstructParam *props)
{
  GObject *obj;
  GabbleBytestreamMultiplePrivate *priv;
  TpHandleRepoIface *contact_repo;
  const gchar *jid;

  obj = G_OBJECT_CLASS (gabble_bytestream_multiple_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (GABBLE_BYTESTREAM_MULTIPLE (obj));

  g_assert (priv->conn != NULL);
  g_assert (priv->peer_handle != 0);
  g_assert (priv->stream_id != NULL);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, priv->peer_handle);

  jid = tp_handle_inspect (contact_repo, priv->peer_handle);

  if (priv->peer_resource != NULL)
    priv->peer_jid = g_strdup_printf ("%s/%s", jid, priv->peer_resource);
  else
    priv->peer_jid = g_strdup (jid);

  return obj;
}

static void
gabble_bytestream_multiple_class_init (
    GabbleBytestreamMultipleClass *gabble_bytestream_multiple_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_bytestream_multiple_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_bytestream_multiple_class,
      sizeof (GabbleBytestreamMultiplePrivate));

  object_class->dispose = gabble_bytestream_multiple_dispose;
  object_class->finalize = gabble_bytestream_multiple_finalize;

  object_class->get_property = gabble_bytestream_multiple_get_property;
  object_class->set_property = gabble_bytestream_multiple_set_property;
  object_class->constructor = gabble_bytestream_multiple_constructor;

   g_object_class_override_property (object_class, PROP_CONNECTION,
      "connection");
   g_object_class_override_property (object_class, PROP_PEER_HANDLE,
       "peer-handle");
   g_object_class_override_property (object_class, PROP_PEER_HANDLE_TYPE,
       "peer-handle-type");
   g_object_class_override_property (object_class, PROP_STREAM_ID,
       "stream-id");
   g_object_class_override_property (object_class, PROP_PEER_JID,
       "peer-jid");
   g_object_class_override_property (object_class, PROP_STATE,
       "state");
   g_object_class_override_property (object_class, PROP_PROTOCOL,
       "protocol");
   g_object_class_override_property (object_class,
        PROP_CLOSE_ON_CONNECTION_ERROR, "close-on-connection-error");

  param_spec = g_param_spec_string (
      "peer-resource",
      "Peer resource",
      "the resource used by the remote peer during the SI, if any",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
      param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
                  G_OBJECT_CLASS_TYPE (gabble_bytestream_multiple_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (gabble_bytestream_multiple_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[CONNECTION_ERROR] =
    g_signal_new ("connection-error",
                  G_OBJECT_CLASS_TYPE (gabble_bytestream_multiple_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

/*
 * gabble_bytestream_multiple_send
 *
 * Implements gabble_bytestream_iface_send on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_multiple_send (GabbleBytestreamIface *iface,
                                 guint len,
                                 const gchar *str)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  GabbleBytestreamIface *bytestream;

  if (priv->state != GABBLE_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  g_assert (priv->bytestreams);
  g_assert (priv->bytestreams->data);

  bytestream = priv->bytestreams->data;
  return gabble_bytestream_iface_send (bytestream, len, str);
}

/*
 * gabble_bytestream_multiple_accept
 *
 * Implements gabble_bytestream_iface_accept on GabbleBytestreamIface
 */
static void
gabble_bytestream_multiple_accept (GabbleBytestreamIface *iface,
                                   GabbleBytestreamAugmentSiAcceptReply func,
                                   gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  LmMessage *msg;
  LmMessageNode *si;

  if (priv->state != GABBLE_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      return;
    }

  msg = gabble_bytestream_factory_make_accept_iq (priv->peer_jid,
      priv->stream_init_id, NS_BYTESTREAMS);
  si = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  g_assert (si != NULL);

  if (func != NULL)
    {
      /* let the caller add his profile specific data */
      func (si, user_data);
    }

  if (_gabble_connection_send (priv->conn, msg, NULL))
    {
      DEBUG ("stream %s with %s is now accepted", priv->stream_id,
          priv->peer_jid);
      g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_ACCEPTED, NULL);
    }

  lm_message_unref (msg);
}

static void
gabble_bytestream_multiple_decline (GabbleBytestreamMultiple *self,
                                    GError *error)
{
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  LmMessage *msg;

  g_return_if_fail (priv->state == GABBLE_BYTESTREAM_STATE_LOCAL_PENDING);

  msg = lm_message_build (priv->peer_jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "error",
      '@', "id", priv->stream_init_id,
      NULL);

  if (error != NULL && error->domain == GABBLE_XMPP_ERROR)
    {
      gabble_xmpp_error_to_node (error->code, msg->node, error->message);
    }
  else
    {
      gabble_xmpp_error_to_node (XMPP_ERROR_FORBIDDEN, msg->node,
          "Offer Declined");
    }

  _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);

  g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSED, NULL);
}

/*
 * gabble_bytestream_multiple_close
 *
 * Implements gabble_bytestream_iface_close on GabbleBytestreamIface
 */
static void
gabble_bytestream_multiple_close (GabbleBytestreamIface *iface,
                                GError *error)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  if (priv->state == GABBLE_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  if (priv->state == GABBLE_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* Stream was created using SI so we decline the request */
      gabble_bytestream_multiple_decline (self, error);
    }
  else
    {
      /* FIXME: send a close stanza */
      /* FIXME: close the sub-bytesreams */

      g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSED, NULL);
    }
}

/*
 * gabble_bytestream_multiple_initiate
 *
 * Implements gabble_bytestream_iface_initiate on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_multiple_initiate (GabbleBytestreamIface *iface)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  GabbleBytestreamIface *bytestream;

  if (priv->state != GABBLE_BYTESTREAM_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d)",
          priv->state);
      return FALSE;
    }

  if (priv->bytestreams == NULL)
    return FALSE;

  /* Initiate the first available bytestream */
  bytestream = priv->bytestreams->data;

  return gabble_bytestream_iface_initiate (bytestream);
}

static void
bytestream_connection_error_cb (GabbleBytestreamIface *failed,
                                gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (user_data);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  GabbleBytestreamIface *fallback;

  priv->bytestreams = g_list_remove (priv->bytestreams, failed);

  if (!priv->bytestreams)
    return;

  /* If we have other methods to try, prevent the failed bytestrem to send a
     close stanza */
  g_object_set (failed, "close-on-connection-error", FALSE, NULL);

  g_object_unref (failed);

  fallback = priv->bytestreams->data;

  DEBUG ("Trying alternative streaming method");

  g_object_set (fallback, "state", GABBLE_BYTESTREAM_STATE_INITIATING, NULL);
  gabble_bytestream_iface_initiate (fallback);
}

static void
bytestream_data_received_cb (GabbleBytestreamIface *bytestream,
                             TpHandle sender,
                             GString *str,
                             gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (user_data);

  /* Just forward the data */
  g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, sender, str);
}

static void
bytestream_state_changed_cb (GabbleBytestreamIface *bytestream,
                             GabbleBytestreamState state,
                             gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (user_data);
  GabbleBytestreamMultiplePrivate *priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  if (state == GABBLE_BYTESTREAM_STATE_CLOSED &&
      g_list_length (priv->bytestreams) <= 1)
    {
      return;
    }

  g_object_set (self, "state", state, NULL);
}

/*
 * gabble_bytestream_multiple_add_bytestream
 *
 * Add an alternative stream method.
 */
void
gabble_bytestream_multiple_add_bytestream (GabbleBytestreamMultiple *self,
                                           GabbleBytestreamIface *bytestream)
{
  GabbleBytestreamMultiplePrivate *priv;

  g_return_if_fail (GABBLE_IS_BYTESTREAM_MULTIPLE (self));
  g_return_if_fail (GABBLE_IS_BYTESTREAM_IFACE (bytestream));

  priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  DEBUG ("Add bytestream");

  g_object_ref (bytestream);
  priv->bytestreams = g_list_append (priv->bytestreams, bytestream);

  g_signal_connect (bytestream, "connection-error",
      G_CALLBACK (bytestream_connection_error_cb), self);
  g_signal_connect (bytestream, "data-received",
      G_CALLBACK (bytestream_data_received_cb), self);
  g_signal_connect (bytestream, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), self);
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleBytestreamIfaceClass *klass = (GabbleBytestreamIfaceClass *) g_iface;

  klass->initiate = gabble_bytestream_multiple_initiate;
  klass->send = gabble_bytestream_multiple_send;
  klass->close = gabble_bytestream_multiple_close;
  klass->accept = gabble_bytestream_multiple_accept;
}
