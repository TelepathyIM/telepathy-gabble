/*
 * bytestream-ibb.c - Source for GabbleBytestreamIBB
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

#include "bytestream-ibb.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_TUBES

#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "namespaces.h"
#include <telepathy-glib/interfaces.h>
#include "util.h"
#include "base64.h"
#include "gabble-signals-marshal.h"
#include "bytestream-factory.h"

G_DEFINE_TYPE (GabbleBytestreamIBB, gabble_bytestream_ibb, G_TYPE_OBJECT);

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
  PROP_PEER_RESOURCE,
  PROP_STATE,
  LAST_PROPERTY
};

typedef struct _GabbleBytestreamIBBPrivate GabbleBytestreamIBBPrivate;
struct _GabbleBytestreamIBBPrivate
{
  GabbleConnection *conn;
  TpHandle peer_handle;
  TpHandleType peer_handle_type;
  gchar *stream_id;
  gchar *stream_init_id;
  gchar *peer_resource;
  BytestreamIBBState state;
  gchar *peer_jid;

  guint16 seq;
  guint16 last_seq_recv;
};

#define GABBLE_BYTESTREAM_IBB_GET_PRIVATE(obj) \
    ((GabbleBytestreamIBBPrivate *) obj->priv)

static void
gabble_bytestream_ibb_init (GabbleBytestreamIBB *self)
{
  GabbleBytestreamIBBPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_IBB, GabbleBytestreamIBBPrivate);

  self->priv = priv;

  priv->seq = 0;
  priv->last_seq_recv = 0;
}

static void
gabble_bytestream_ibb_dispose (GObject *object)
{
  GabbleBytestreamIBB *self = GABBLE_BYTESTREAM_IBB (object);
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->state != BYTESTREAM_IBB_STATE_CLOSED)
    {
      gabble_bytestream_ibb_close (self);
    }

  G_OBJECT_CLASS (gabble_bytestream_ibb_parent_class)->dispose (object);
}

static void
gabble_bytestream_ibb_finalize (GObject *object)
{
  GabbleBytestreamIBB *self = GABBLE_BYTESTREAM_IBB (object);
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);

  g_free (priv->stream_id);

  if (priv->stream_init_id)
    g_free (priv->stream_init_id);

  if (priv->peer_resource)
    g_free (priv->peer_resource);

  g_free (priv->peer_jid);

  G_OBJECT_CLASS (gabble_bytestream_ibb_parent_class)->finalize (object);
}

static void
gabble_bytestream_ibb_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GabbleBytestreamIBB *self = GABBLE_BYTESTREAM_IBB (object);
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_PEER_HANDLE:
        g_value_set_uint (value, priv->peer_handle);
        break;
      case PROP_PEER_HANDLE_TYPE:
        g_value_set_uint (value, priv->peer_handle_type);
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
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_bytestream_ibb_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GabbleBytestreamIBB *self = GABBLE_BYTESTREAM_IBB (object);
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_PEER_HANDLE:
        priv->peer_handle = g_value_get_uint (value);
        break;
      case PROP_PEER_HANDLE_TYPE:
        priv->peer_handle_type = g_value_get_uint (value);
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
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_bytestream_ibb_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleBytestreamIBBPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_bytestream_ibb_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (GABBLE_BYTESTREAM_IBB (obj));

  g_assert (priv->conn != NULL);
  g_assert (priv->peer_handle != 0);

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM)
    {
      TpHandleRepoIface *room_repo;

      room_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);

      priv->peer_jid = g_strdup (tp_handle_inspect (room_repo,
            priv->peer_handle));
    }
  else
    {
      TpHandleRepoIface *contact_repo;
      const gchar *jid;

      contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

      jid = tp_handle_inspect (contact_repo, priv->peer_handle);

      if (priv->peer_resource != NULL)
        priv->peer_jid = g_strdup_printf ("%s/%s", jid, priv->peer_resource);
      else
        priv->peer_jid = g_strdup (jid);
    }

  return obj;
}

static void
gabble_bytestream_ibb_class_init (
    GabbleBytestreamIBBClass *gabble_bytestream_ibb_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_bytestream_ibb_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_bytestream_ibb_class,
      sizeof (GabbleBytestreamIBBPrivate));

  object_class->dispose = gabble_bytestream_ibb_dispose;
  object_class->finalize = gabble_bytestream_ibb_finalize;

  object_class->get_property = gabble_bytestream_ibb_get_property;
  object_class->set_property = gabble_bytestream_ibb_set_property;
  object_class->constructor = gabble_bytestream_ibb_constructor;

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this Bytestream IBB object.",
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
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
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
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
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
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
      param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "Bytestream state",
      "An enum (BytestreamIBBState) signifying the current state of"
      "this bytestream object",
      0, LAST_BYTESTREAM_IBB_STATE - 1, BYTESTREAM_IBB_STATE_LOCAL_PENDING,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
                  G_OBJECT_CLASS_TYPE (gabble_bytestream_ibb_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (gabble_bytestream_ibb_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

gboolean
send_data_to (GabbleBytestreamIBB *self,
              const gchar *to,
              gboolean groupchat,
              guint len,
              gchar *str)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);
  LmMessage *msg;
  gchar *seq, *encoded;
  gboolean ret;

  if (priv->state != BYTESTREAM_IBB_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  seq = g_strdup_printf ("%u", priv->seq++);

  encoded = base64_encode (len, str);

  msg = lm_message_build (to, LM_MESSAGE_TYPE_MESSAGE,
      '(', "data", encoded,
        '@', "xmlns", NS_IBB,
        '@', "sid", priv->stream_id,
        '@', "seq", seq,
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
  g_free (seq);

  return ret;
}

gboolean
gabble_bytestream_ibb_send (GabbleBytestreamIBB *self,
                            guint len,
                            gchar *str)
{
  GabbleBytestreamIBBPrivate *priv;
  gboolean groupchat = FALSE;

  g_assert (GABBLE_IS_BYTESTREAM_IBB (self));
  priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM)
    groupchat = TRUE;

  return send_data_to (self, priv->peer_jid, groupchat, len, str);
}

gboolean
gabble_bytestream_ibb_receive (GabbleBytestreamIBB *self,
                               LmMessage *msg)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);
  LmMessageNode *data;
  GString *str;
  const gchar *from, *msg_type;
  TpHandle sender;
  TpHandleRepoIface *contact_repo;

  if (priv->state != BYTESTREAM_IBB_STATE_OPEN)
    {
      DEBUG ("can't receive data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  data = lm_message_node_get_child_with_namespace (msg->node, "data", NS_IBB);
  if (data == NULL)
    {
      NODE_DEBUG (msg->node, "got a message without a data element, ignoring");
      return FALSE;
    }

  from = lm_message_node_get_attribute (msg->node, "from");
  if (from == NULL)
    {
      NODE_DEBUG (msg->node, "got a message without a from field, ignoring");
      return FALSE;
    }

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  /* This will only recognise it as a MUC handle if it's one it's seen before;
   * but people we're having a tube with should be people we've established
   * contact with before.
   */
  sender = tp_handle_lookup (contact_repo, from, NULL, NULL);

  msg_type = lm_message_node_get_attribute (msg->node, "type");

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM)
    {
      /* multi users stream */
      gchar *room_jid;
      TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
      TpHandle room_handle;

      room_jid = gabble_remove_resource (from);

      if (!room_jid)
        return FALSE;

      room_handle = tp_handle_lookup (room_repo, room_jid, NULL, NULL);

      g_free (room_jid);

      if (room_handle != priv->peer_handle)
        /* Data are not for this stream */
        return FALSE;
    }
  else
    {
      /* Private stream */
      if (priv->peer_handle != sender)
        return FALSE;

      // XXX check sequence number
    }

  str = base64_decode (lm_message_node_get_value (data));
  g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, sender, str);

  g_string_free (str, TRUE);
  return TRUE;
}

LmMessage *
gabble_bytestream_ibb_make_accept_iq (GabbleBytestreamIBB *self)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);
  LmMessage *msg;

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM ||
      priv->stream_init_id == NULL)
    {
      DEBUG ("bytestream was not created due to a SI request");
      return NULL;
    }

  msg = gabble_bytestream_factory_make_accept_iq (priv->peer_jid,
      priv->stream_init_id, NS_IBB);

  return msg;
}

void
gabble_bytestream_ibb_accept (GabbleBytestreamIBB *self, LmMessage *msg)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->state != BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      return;
    }

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM ||
      priv->stream_init_id == NULL)
    {
      DEBUG ("can't accept a bytestream not created due to a SI request");
      return;
    }

  if (_gabble_connection_send (priv->conn, msg, NULL))
    {
      priv->state = BYTESTREAM_IBB_STATE_ACCEPTED;
    }
}

static void
gabble_bytestream_ibb_decline (GabbleBytestreamIBB *self)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);
  LmMessage *msg;

  if (priv->state != BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    {
      DEBUG ("bytestream is not in the local pending state (state %d)",
          priv->state);
      return;
    }

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM ||
      priv->stream_init_id == NULL)
    {
      DEBUG ("can't decline a bytestream not created due to a SI request");
      return;
    }

  msg = gabble_bytestream_factory_make_decline_iq (priv->peer_jid,
      priv->stream_init_id);

  _gabble_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);
}

void
gabble_bytestream_ibb_close (GabbleBytestreamIBB *self)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->state == BYTESTREAM_IBB_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  if (priv->state == BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    {
      if (priv->stream_init_id != NULL)
        {
          /* Stream was created using SI so we decline the request */
          gabble_bytestream_ibb_decline (self);
        }
    }

  else if (priv->peer_handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* XXX : Does it make sense to send a close message in a
       * muc bytestream ? */
      LmMessage *msg;

      DEBUG ("send IBB close stanza");

      msg = lm_message_build (priv->peer_jid, LM_MESSAGE_TYPE_IQ,
          '@', "type", "set",
          '(', "close", "",
            '@', "xmlns", NS_IBB,
            '@', "sid", priv->stream_id,
          ')', NULL);

      _gabble_connection_send (priv->conn, msg, NULL);

      lm_message_unref (msg);
    }

  g_object_set (self, "state", BYTESTREAM_IBB_STATE_CLOSED, NULL);
}

gboolean
gabble_bytestream_ibb_send_to (GabbleBytestreamIBB *self,
                               TpHandle contact,
                               guint len,
                               gchar *str)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *to;

  if (priv->peer_handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Private stream */
      if (priv->peer_handle != contact)
        {
          to = tp_handle_inspect (contact_repo, contact);

          DEBUG ("This bytestream can't be used to send data"
              "to this contact: %s", to);
          return FALSE;
        }

      return gabble_bytestream_ibb_send (self, len, str);
    }

  to = tp_handle_inspect (contact_repo, contact);

  return send_data_to (self, to, FALSE, len, str);
}

static LmHandlerResult
ibb_init_reply_cb (GabbleConnection *conn,
                   LmMessage *sent_msg,
                   LmMessage *reply_msg,
                   GObject *obj,
                   gpointer user_data)
{
  GabbleBytestreamIBB *self = GABBLE_BYTESTREAM_IBB (obj);

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_RESULT)
    {
      /* yeah, stream initiated */
      DEBUG ("IBB stream initiated");
      g_object_set (self, "state", BYTESTREAM_IBB_STATE_OPEN, NULL);
    }
  else
    {
      DEBUG ("error during IBB initiation");
      g_object_set (self, "state", BYTESTREAM_IBB_STATE_CLOSED, NULL);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

gboolean
gabble_bytestream_ibb_initiation (GabbleBytestreamIBB *self)
{
  GabbleBytestreamIBBPrivate *priv = GABBLE_BYTESTREAM_IBB_GET_PRIVATE (self);
  LmMessage *msg;

  if (priv->state != BYTESTREAM_IBB_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d",
          priv->state);
      return FALSE;
    }

  if (priv->peer_handle_type != TP_HANDLE_TYPE_CONTACT)
    {
      DEBUG ("Can only initiate a private bytestream");
      return FALSE;
    }

  if (priv->stream_id == NULL)
    {
      DEBUG ("stream doesn't have an ID");
      return FALSE;
    }

  msg = lm_message_build (priv->peer_jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "set",
      '(', "open", "",
        '@', "xmlns", NS_IBB,
        '@', "sid", priv->stream_id,
        '@', "block-size", "4096",
      ')', NULL);

  if (!_gabble_connection_send_with_reply (priv->conn, msg,
      ibb_init_reply_cb, G_OBJECT (self), NULL, NULL))
    {
      DEBUG ("Error when sending IBB init stanza");

      lm_message_unref (msg);
      return FALSE;
    }

  lm_message_unref (msg);

  return TRUE;
}
