/*
 * bytestream-muc.c - Source for GabbleBytestreamMuc
 * Copyright (C) 2007 Collabora Ltd.
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

/* 45k gives us 60k after base64 encoding, allowing 4k of header before we hit
 * ejabberd's default 64k maximum stanza size */
#define MAX_BLOCK_SIZE (1024 * 45)

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
  PROP_PEER_JID,
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
  const gchar *peer_jid;
  /* (gchar *): sender's muc-JID -> (GString *): accumulated message data */
  GHashTable *buffers;

  gboolean dispose_has_run;
};

#define GABBLE_BYTESTREAM_MUC_GET_PRIVATE(obj) \
    ((GabbleBytestreamMucPrivate *) obj->priv)

static void
free_buffer (GString *buffer)
{
  g_string_free (buffer, TRUE);
}

static void
gabble_bytestream_muc_init (GabbleBytestreamMuc *self)
{
  GabbleBytestreamMucPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_MUC, GabbleBytestreamMucPrivate);

  self->priv = priv;
  priv->buffers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) free_buffer);
}

static void
gabble_bytestream_muc_dispose (GObject *object)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (object);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_handle_unref (room_repo, priv->peer_handle);

  if (priv->state != GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      gabble_bytestream_iface_close (GABBLE_BYTESTREAM_IFACE (self), NULL);
    }

  G_OBJECT_CLASS (gabble_bytestream_muc_parent_class)->dispose (object);
}

static void
gabble_bytestream_muc_finalize (GObject *object)
{
  GabbleBytestreamMuc *self = GABBLE_BYTESTREAM_MUC (object);
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);

  g_free (priv->stream_id);

  if (priv->buffers != NULL)
    {
      g_hash_table_destroy (priv->buffers);
      priv->buffers = NULL;
    }

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
  TpHandleRepoIface *room_repo;

  obj = G_OBJECT_CLASS (gabble_bytestream_muc_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (GABBLE_BYTESTREAM_MUC (obj));

  g_assert (priv->conn != NULL);
  g_assert (priv->peer_handle != 0);


  room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);

  tp_handle_ref (room_repo, priv->peer_handle);

  priv->peer_jid = tp_handle_inspect (room_repo,
        priv->peer_handle);

  return obj;
}

static void
gabble_bytestream_muc_class_init (
    GabbleBytestreamMucClass *gabble_bytestream_muc_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_bytestream_muc_class);

  g_type_class_add_private (gabble_bytestream_muc_class,
      sizeof (GabbleBytestreamMucPrivate));

  object_class->dispose = gabble_bytestream_muc_dispose;
  object_class->finalize = gabble_bytestream_muc_finalize;

  object_class->get_property = gabble_bytestream_muc_get_property;
  object_class->set_property = gabble_bytestream_muc_set_property;
  object_class->constructor = gabble_bytestream_muc_constructor;

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

enum
{
  FRAG_COMPLETE = 0,
  FRAG_FIRST,
  FRAG_MIDDLE,
  FRAG_LAST
};

static gboolean
send_data_to (GabbleBytestreamMuc *self,
              const gchar *to,
              gboolean groupchat,
              guint len,
              const gchar *str)
{
  GabbleBytestreamMucPrivate *priv = GABBLE_BYTESTREAM_MUC_GET_PRIVATE (self);
  LmMessage *msg;
  guint sent, stanza_count;
  LmMessageNode *data = NULL;
  guint frag;

  if (priv->state != GABBLE_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  msg = lm_message_build (to, LM_MESSAGE_TYPE_MESSAGE,
      '(', "data", "",
        '*', &data,
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

  g_assert (data != NULL);

  if (groupchat)
    {
      lm_message_node_set_attribute (msg->node, "type", "groupchat");
    }

  sent = 0;
  stanza_count = 0;
  while (sent < len)
    {
      gboolean ret;
      gchar *encoded;
      guint send_now;
      GError *error = NULL;

      if ((len - sent) > MAX_BLOCK_SIZE)
        {
          /* We can't send all the remaining data in one stanza */
          send_now = MAX_BLOCK_SIZE;

          if (stanza_count == 0)
            frag = FRAG_FIRST;
          else
            frag = FRAG_MIDDLE;
        }
      else
        {
          /* Send all the remaining data */
          send_now = (len - sent);

          if (stanza_count == 0)
            frag = FRAG_COMPLETE;
          else
            frag = FRAG_LAST;
        }

      encoded = base64_encode (send_now, str + sent, FALSE);
      lm_message_node_set_value (data, encoded);

      switch (frag)
        {
          case FRAG_FIRST:
            lm_message_node_set_attribute (data, "frag", "first");
            break;
          case FRAG_MIDDLE:
            lm_message_node_set_attribute (data, "frag", "middle");
            break;
          case FRAG_LAST:
            lm_message_node_set_attribute (data, "frag", "last");
            break;
        }

      DEBUG ("send %d bytes", send_now);
      ret = _gabble_connection_send (priv->conn, msg, &error);

      g_free (encoded);

      if (!ret)
        {
          DEBUG ("error sending pseusdo IBB Muc stanza: %s", error->message);
          g_error_free (error);
          lm_message_unref (msg);
          return FALSE;
        }

      sent += send_now;
      stanza_count++;
    }

  DEBUG ("finished to send %d bytes (%d stanzas needed)", len, stanza_count);

  lm_message_unref (msg);
  return TRUE;
}

/*
 * gabble_bytestream_muc_send
 *
 * Implements gabble_bytestream_iface_send on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_muc_send (GabbleBytestreamIface *iface,
                            guint len,
                            const gchar *str)
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
  GString *buffer;
  const gchar *frag_val;
  guint frag;
  gboolean fully_received = FALSE;

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

  frag_val = lm_message_node_get_attribute (data, "frag");
  if (frag_val == NULL)
    frag = FRAG_COMPLETE;
  else if (!tp_strdiff (frag_val, "first"))
    frag = FRAG_FIRST;
  else if (!tp_strdiff (frag_val, "middle"))
    frag = FRAG_MIDDLE;
  else if (!tp_strdiff (frag_val, "last"))
    frag = FRAG_LAST;
  else if (!tp_strdiff (frag_val, "complete"))
    frag = FRAG_COMPLETE;
  else
    {
      DEBUG ("Invalid frag value: %s", frag_val);
      return;
    }

  str = base64_decode (lm_message_node_get_value (data));
  if (str == NULL)
    {
      DEBUG ("base64 decoding failed");
      return;
    }

  buffer = g_hash_table_lookup (priv->buffers, from);

  if (frag == FRAG_COMPLETE)
    {
      if (buffer != NULL)
        {
          DEBUG ("Drop incomplete buffer of %s. "
              "Received new unfragmented data", from);
          g_hash_table_remove (priv->buffers, from);
        }

      fully_received = TRUE;
    }

  else if (frag == FRAG_FIRST)
    {
      if (buffer != NULL)
        {
          DEBUG ("Drop incomplete buffer of %s. "
              "Received first part of new data", from);
          g_hash_table_remove (priv->buffers, from);
        }
      else
        {
          DEBUG ("New buffer for %s", from);
        }

      g_hash_table_insert (priv->buffers, g_strdup (from), str);
    }

  else if (frag == FRAG_MIDDLE)
    {
      if (buffer == NULL)
        {
          DEBUG ("Drop middle part stanza from %s, first parts not buffered",
              from);
        }
      else
        {
          DEBUG ("Append data to buffer of %s (%zu bytes)", from, str->len);
          g_string_append_len (buffer, str->str, str->len);
        }

      g_string_free (str, TRUE);
    }

  else if (frag == FRAG_LAST)
    {
      if (buffer == NULL)
          {
            DEBUG ("Drop last part stanza from %s, first parts not buffered",
                from);
            g_string_free (str, TRUE);
          }
      else
        {
          DEBUG ("Received last part from %s, buffer flushed", from);
          g_string_prepend_len (str, buffer->str, buffer->len);
          g_hash_table_remove (priv->buffers, from);
          fully_received = TRUE;
        }
    }

  if (fully_received)
    {
      DEBUG ("fully received %zu bytes of data", str->len);
      g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, sender,
          str);
      g_string_free (str, TRUE);
    }
}

/*
 * gabble_bytestream_muc_accept
 *
 * Implements gabble_bytestream_iface_accept on GabbleBytestreamIface
 */
static void
gabble_bytestream_muc_accept (GabbleBytestreamIface *iface,
                              GabbleBytestreamAugmentSiAcceptReply func,
                              gpointer user_data)
{
  /* Don't have to accept a muc bytestream */
}

/*
 * gabble_bytestream_muc_close
 *
 * Implements gabble_bytestream_iface_close on GabbleBytestreamIface
 */
static void
gabble_bytestream_muc_close (GabbleBytestreamIface *iface,
                             GError *error)
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
  g_return_val_if_fail (g_str_has_prefix (to, priv->peer_jid), FALSE);
  g_return_val_if_fail (to[strlen (priv->peer_jid)] == '/', FALSE);

  return send_data_to (self, to, FALSE, len, str);
}

/*
 * gabll_bytestream_muc_initiate
 *
 * Implements gabble_bytestream_iface_initiate on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_muc_initiate (GabbleBytestreamIface *iface)
{
  /* Nothing to do */
  return TRUE;
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleBytestreamIfaceClass *klass = (GabbleBytestreamIfaceClass *) g_iface;

  klass->initiate = gabble_bytestream_muc_initiate;
  klass->send = gabble_bytestream_muc_send;
  klass->close = gabble_bytestream_muc_close;
  klass->accept = gabble_bytestream_muc_accept;
}
