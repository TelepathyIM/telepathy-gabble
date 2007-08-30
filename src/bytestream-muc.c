/*
 * bytestream-muc.c - Source for GabbleBytestreamMuc
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

#include "bytestream-muc.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_BYTESTREAM

#include "bytestream-iface.h"
#include "base64.h"
#include "bytestream-factory.h"
#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleBytestreamMuc, gabble_bytestream_muc,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

/* signals */
enum
{
  DATA_RECEIVED,
  STATE_CHANGED,
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
  LAST_PROPERTY
};

typedef struct _GabbleBytestreamMucPrivate GabbleBytestreamMucPrivate;
struct _GabbleBytestreamMucPrivate
{
  GabbleConnection *conn;
  TpHandle peer_handle;
  TpHandleType peer_handle_type;
  gchar *stream_id;
  GabbleBytestreamState state;
  gchar *peer_jid;
};

#define GABBLE_BYTESTREAM_MUC_GET_PRIVATE(obj) \
    ((GabbleBytestreamMucPrivate *) obj->priv)

static void
gabble_bytestream_muc_init (GabbleBytestreamMuc *self)
{
  GabbleBytestreamMucPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_MUC, GabbleBytestreamMucPrivate);

  self->priv = priv;
}

static void
gabble_bytestream_muc_dispose (GObject *object)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (object);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);

  if (priv->state != GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      gabble_bytestream_iface_close (GABBLE_BYTESTREAM_IFACE (self));
    }

  G_OBJECT_CLASS (gabble_bytestream_muc_parent_class)->dispose (object);
}

static void
gabble_bytestream_muc_finalize (GObject *object)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (object);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);

  g_free (priv->stream_id);
  g_free (priv->peer_jid);

  G_OBJECT_CLASS (gabble_bytestream_muc_parent_class)->finalize (object);
}

static void
gabble_bytestream_muc_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (object);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_PEER_HANDLE:
        g_value_set_uint (value, priv->peer_handle);
        break;
      case PROP_PEER_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_ROOM);
        break;
      case PROP_STREAM_ID:
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_STREAM_INIT_ID:
        g_value_set_string (value, NULL);
        break;
      case PROP_PEER_RESOURCE:
        g_value_set_string (value, NULL);
        break;
      case PROP_PEER_JID:
        g_value_set_string (value, priv->peer_jid);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_bytestream_muc_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (object);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);

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
      case PROP_STATE:
        if (priv->state != g_value_get_uint (value))
            {
              priv->state = g_value_get_uint (value);
              g_signal_emit (object, signals[STATE_CHANGED], 0, priv->state);
            }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_bytestream_muc_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleBytestreamMucPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_bytestream_muc_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (GABBLE_BYTESTREAM_MUC (obj));

  g_assert (priv->conn != NULL);
  g_assert (priv->peer_handle != 0);

  TpHandleRepoIface *room_repo;

  room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);

  priv->peer_jid = g_strdup (tp_handle_inspect (room_repo,
        priv->peer_handle));

  return obj;
}

static void
gabble_bytestream_muc_class_init (
    GabbleBytestreamMucClass *gabble_bytestream_muc_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_bytestream_muc_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_bytestream_muc_class,
      sizeof (GabbleBytestreamMucPrivate));

  object_class->dispose = gabble_bytestream_muc_dispose;
  object_class->finalize = gabble_bytestream_muc_finalize;

  object_class->get_property = gabble_bytestream_muc_get_property;
  object_class->set_property = gabble_bytestream_muc_set_property;
  object_class->constructor = gabble_bytestream_muc_constructor;

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this Bytestream MUC object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_uint (
      "peer-handle",
      "Peer handle",
      "The TpHandle of the remote peer involved in this bytestream",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_HANDLE, param_spec);

  param_spec = g_param_spec_uint (
      "peer-handle-type",
      "Peer handle type",
      "The TpHandleType of the remote peer's associated handle",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_HANDLE_TYPE,
      param_spec);

  param_spec = g_param_spec_string (
      "stream-id",
      "stream ID",
      "the ID of the stream",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_ID, param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      "",
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);

  param_spec = g_param_spec_string (
      "peer-resource",
      "Peer resource",
      "the resource used by the remote peer during the SI, if any",
      "",
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
      param_spec);

  param_spec = g_param_spec_string (
      "peer-jid",
      "Peer JID",
      "The JID used by the remote peer during the SI",
      "",
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_JID,
      param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "Bytestream state",
      "An enum (GabbleBytestreamState) signifying the current state of"
      "this bytestream object",
      0, LAST_GABBLE_BYTESTREAM_STATE - 1,
      GABBLE_BYTESTREAM_STATE_LOCAL_PENDING,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
                  G_OBJECT_CLASS_TYPE (gabble_bytestream_muc_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (gabble_bytestream_muc_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

static gboolean
send_data_to (GabbleBytestreamMuc *self,
              const gchar *to,
              gboolean groupchat,
              guint len,
              gchar *str)
{
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);
  LmMessage *msg;
  gchar *encoded;
  gboolean ret;

  if (priv->state != GABBLE_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  encoded = base64_encode (len, str);

  msg = lm_message_build (to, LM_MESSAGE_TYPE_MESSAGE,
      '(', "data", encoded,
        '@', "xmlns", NS_MUC_BYTESTREAM,
        '@', "sid", priv->stream_id,
      ')',
      '(', "amp", "",
        '@', "xmlns", NS_AMP,
        '(', "rule", "",
          '@', "condition", "deliver-at",
          '@', "value", "stored",
          '@', "action", "error",
        ')',
        '(', "rule", "",
          '@', "condition", "match-resource",
          '@', "value", "exact",
          '@', "action", "error",
        ')',
      ')', NULL);

  if (groupchat)
    {
      lm_message_node_set_attribute (msg->node, "type", "groupchat");
    }

  ret = _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);
  g_free (encoded);

  return ret;
}

/*
 * gabble_bytestream_muc_send
 *
 * Implements gabble_bytestream_iface_send on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_muc_send (GabbleBytestreamIface *iface,
                            guint len,
                            gchar *str)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (iface);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);

  return send_data_to (self, priv->peer_jid, TRUE, len, str);
}

void
gabble_bytestream_muc_receive (GabbleBytestreamMuc *self,
                               LmMessage *msg)
{
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *from;
  LmMessageNode *data;
  GString *str;
  TpHandle sender;

  /* caller must have checked for this in order to know which bytestream to
   * route this packet to */
  data = lm_message_node_get_child_with_namespace (msg->node, "data",
      NS_MUC_BYTESTREAM);
  g_assert (data != NULL);

  if (priv->state != GABBLE_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't receive data through a not open bytestream (state: %d)",
          priv->state);
      return;
    }

  from = lm_message_node_get_attribute (msg->node, "from");
  g_return_if_fail (from != NULL);
  sender = tp_handle_lookup (contact_repo, from,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

  if (sender == 0)
    {
      DEBUG ("ignoring data in MUC from unknown contact %s", from);
      return;
    }

  str = base64_decode (lm_message_node_get_value (data));
  g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, sender, str);
  g_string_free (str, TRUE);

  return;
}

/*
 * gabble_bytestream_muc_accept
 *
 * Implements gabble_bytestream_iface_accept on GabbleBytestreamIface
 */
static void
gabble_bytestream_muc_accept (GabbleBytestreamIface *iface, LmMessage *msg)
{
  /* Don't have to accept a muc bytestream */
}

/*
 * gabble_bytestream_muc_close
 *
 * Implements gabble_bytestream_iface_close on GabbleBytestreamIface
 */
static void
gabble_bytestream_muc_close (GabbleBytestreamIface *iface)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (iface);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);

  if (priv->state == GABBLE_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSED, NULL);
}

gboolean
gabble_bytestream_muc_send_to (GabbleBytestreamMuc *self,
                               TpHandle contact,
                               guint len,
                               gchar *str)
{
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *to;

  to = tp_handle_inspect (contact_repo, contact);
  /* TODO check if the contact is a member of the muc */

  return send_data_to (self, to, FALSE, len, str);
}

/*
 * gabll_bytestream_muc_initiation
 *
 * Implements gabble_bytestream_iface_initiation on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_muc_initiation (GabbleBytestreamIface *iface)
{
  /* Nothing to do */
  return TRUE;
}

/*
 * gabble_bytestream_muc_get_protocol
 *
 * Implements gabble_bytestream_iface_get_protocol on GabbleBytestreamIface
 */
static const gchar *
gabble_bytestream_muc_get_protocol (GabbleBytestreamIface *iface)
{
  return NS_MUC_BYTESTREAM;
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleBytestreamIfaceClass *klass = (GabbleBytestreamIfaceClass *) g_iface;

  klass->initiation = gabble_bytestream_muc_initiation;
  klass->send = gabble_bytestream_muc_send;
  klass->close = gabble_bytestream_muc_close;
  klass->accept = gabble_bytestream_muc_accept;
  klass->get_protocol = gabble_bytestream_muc_get_protocol;
}
