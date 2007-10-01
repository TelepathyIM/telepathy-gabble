/*
 * bytestream-factory.c - Source for GabbleBytestreamFactory
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

#include "bytestream-factory.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_BYTESTREAM

#include <telepathy-glib/interfaces.h>
#include "debug.h"
#include "gabble-connection.h"
#include "bytestream-iface.h"
#include "bytestream-ibb.h"
#include "bytestream-muc.h"
#include "namespaces.h"
#include "util.h"
#include "presence-cache.h"

#include "private-tubes-factory.h"

G_DEFINE_TYPE (GabbleBytestreamFactory, gabble_bytestream_factory,
    G_TYPE_OBJECT);

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct
{
  gchar *jid;
  gchar *stream;
} BytestreamIdentifier;

typedef struct
{
  const gchar *jid;
  const gchar *stream;
} ConstBytestreamIdentifier;

gboolean
bytestream_id_equal (gconstpointer v1,
                     gconstpointer v2)
{
  const ConstBytestreamIdentifier *left = v1;
  const ConstBytestreamIdentifier *right = v2;

  return (!tp_strdiff (left->jid, right->jid)) &&
      (!tp_strdiff (left->stream, right->stream));
}

guint
bytestream_id_hash (gconstpointer v)
{
  const ConstBytestreamIdentifier *bsid = v;

  return g_str_hash (bsid->jid) ^ g_str_hash (bsid->stream);
}

BytestreamIdentifier *
bytestream_id_new (GabbleBytestreamIface *bytestream)
{
  BytestreamIdentifier *bsid = g_slice_new (BytestreamIdentifier);

  g_object_get (bytestream,
      "stream-id", &(bsid->stream),
      "peer-jid", &(bsid->jid),
      NULL);
  return bsid;
}

void
bytestream_id_free (gpointer v)
{
  BytestreamIdentifier *bsid = v;

  g_free (bsid->jid);
  g_free (bsid->stream);
  g_slice_free (BytestreamIdentifier, bsid);
}

typedef struct _GabbleBytestreamFactoryPrivate GabbleBytestreamFactoryPrivate;
struct _GabbleBytestreamFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *iq_si_cb;
  LmMessageHandler *iq_ibb_cb;
  LmMessageHandler *msg_data_cb;

  /* SI-initiated bytestreams - data sent by normal messages, IQs used to
   * open and close.
   *
   * BytestreamIdentifier -> GabbleBytestreamIBB */
  GHashTable *ibb_bytestreams;

  /* MUC pseudo-IBB - data sent by groupchat messages, IQs not allowed.
   *
   * BytestreamIdentifier -> GabbleBytestreamMuc */
  GHashTable *muc_bytestreams;

  gboolean dispose_has_run;
};

#define GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE(obj) \
    ((GabbleBytestreamFactoryPrivate *) obj->priv)

static LmHandlerResult
bytestream_factory_msg_data_cb (LmMessageHandler *handler,
    LmConnection *lmconn, LmMessage *message, gpointer user_data);

static LmHandlerResult
bytestream_factory_iq_si_cb (LmMessageHandler *handler, LmConnection *lmconn,
    LmMessage *message, gpointer user_data);

static LmHandlerResult
bytestream_factory_iq_ibb_cb (LmMessageHandler *handler, LmConnection *lmconn,
    LmMessage *message, gpointer user_data);

static void
gabble_bytestream_factory_init (GabbleBytestreamFactory *self)
{
  GabbleBytestreamFactoryPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_FACTORY, GabbleBytestreamFactoryPrivate);

  self->priv = priv;

  priv->ibb_bytestreams = g_hash_table_new_full (bytestream_id_hash,
      bytestream_id_equal, bytestream_id_free, g_object_unref);

  priv->muc_bytestreams = g_hash_table_new_full (bytestream_id_hash,
      bytestream_id_equal, bytestream_id_free, g_object_unref);

  priv->iq_si_cb = NULL;
  priv->iq_ibb_cb = NULL;
  priv->msg_data_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

static GObject *
gabble_bytestream_factory_constructor (GType type,
                                       guint n_props,
                                       GObjectConstructParam *props)
{
  GObject *obj;
  GabbleBytestreamFactory *self;
  GabbleBytestreamFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_bytestream_factory_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_BYTESTREAM_FACTORY (obj);
  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  priv->msg_data_cb = lm_message_handler_new (bytestream_factory_msg_data_cb,
      self, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
      priv->msg_data_cb, LM_MESSAGE_TYPE_MESSAGE, LM_HANDLER_PRIORITY_FIRST);

  priv->iq_si_cb = lm_message_handler_new (bytestream_factory_iq_si_cb, self,
      NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->iq_si_cb,
      LM_MESSAGE_TYPE_IQ, LM_HANDLER_PRIORITY_FIRST);

  priv->iq_ibb_cb = lm_message_handler_new (bytestream_factory_iq_ibb_cb, self,
      NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->iq_ibb_cb,
      LM_MESSAGE_TYPE_IQ, LM_HANDLER_PRIORITY_FIRST);

  return obj;
}

static void
gabble_bytestream_factory_dispose (GObject *object)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (object);
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->msg_data_cb, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->msg_data_cb);

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->iq_si_cb, LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_si_cb);

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->iq_ibb_cb, LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_ibb_cb);

  g_hash_table_destroy (priv->ibb_bytestreams);
  priv->ibb_bytestreams = NULL;

  g_hash_table_destroy (priv->muc_bytestreams);
  priv->muc_bytestreams = NULL;

  if (G_OBJECT_CLASS (gabble_bytestream_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_bytestream_factory_parent_class)->dispose (object);
}

static void
gabble_bytestream_factory_get_property (GObject *object,
                                        guint property_id,
                                        GValue *value,
                                        GParamSpec *pspec)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (object);
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_bytestream_factory_set_property (GObject *object,
                                        guint property_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (object);
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_bytestream_factory_class_init (
    GabbleBytestreamFactoryClass *gabble_bytestream_factory_class)
{
  GObjectClass *object_class =
    G_OBJECT_CLASS (gabble_bytestream_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_bytestream_factory_class,
      sizeof (GabbleBytestreamFactoryPrivate));

  object_class->constructor = gabble_bytestream_factory_constructor;
  object_class->dispose = gabble_bytestream_factory_dispose;

  object_class->get_property = gabble_bytestream_factory_get_property;
  object_class->set_property = gabble_bytestream_factory_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "GabbleConnection object",
      "Gabble connection object that owns this bytestream factory object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static void
remove_bytestream (GabbleBytestreamFactory *self,
                   GabbleBytestreamIface *bytestream)
{
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE
    (self);
  BytestreamIdentifier bsid = { NULL, NULL };
  guint handle_type;
  GHashTable *table;

  g_object_get (bytestream,
      "stream-id", &(bsid.stream),
      "peer-jid", &(bsid.jid),
      "peer-handle-type", &handle_type,
      NULL);

  if (handle_type == TP_HANDLE_TYPE_ROOM)
    {
      table = priv->muc_bytestreams;
    }
  else
    {
      table = priv->ibb_bytestreams;
    }

  if (table == NULL)
    return;

  DEBUG ("removing bytestream: <%s> from <%s>", bsid.stream, bsid.jid);
  g_hash_table_remove (table, &bsid);

  g_free (bsid.stream);
  g_free (bsid.jid);
}

/**
 * streaminit_parse_request
 *
 * Parses a SI request, or returns FALSE if it can't be parsed.
 *
 * The items in the linked list of stream methods are borrowed
 * from the message; their lifetime is only as long as that of the
 * message.
 */
static gboolean
streaminit_parse_request (LmMessage *message,
                          LmMessageNode *si,
                          const gchar **profile,
                          const gchar **from,
                          const gchar **stream_id,
                          const gchar **stream_init_id,
                          const gchar **mime_type,
                          GSList **stream_methods)
{
  LmMessageNode *iq = message->node;
  LmMessageNode *feature, *x, *field, *stream_method;

  *stream_init_id = lm_message_node_get_attribute (iq, "id");

  *from = lm_message_node_get_attribute (message->node, "from");
  if (*from == NULL)
    {
      NODE_DEBUG (message->node, "got a message without a from field");
      return FALSE;
    }

  /* Parse <si> */

  *stream_id = lm_message_node_get_attribute (si, "id");
  if (*stream_id == NULL)
    {
      NODE_DEBUG (message->node, "got a SI request without a stream id field");
      return FALSE;
    }

  *mime_type = lm_message_node_get_attribute (si, "mime-type");
  /* if no mime_type is defined, XEP-0095 says to assume "binary/octect-stream"
   * which is presumably a typo for "application/octet-stream" */

  *profile = lm_message_node_get_attribute (si, "profile");
  if (*profile == NULL)
    {
      NODE_DEBUG (message->node, "got a SI request without a profile field");
      return FALSE;
    }

  /* Parse <feature> */
  feature = lm_message_node_get_child_with_namespace (si, "feature",
      NS_FEATURENEG);
  if (feature == NULL)
    {
      NODE_DEBUG (message->node, "got a SI request without a feature field");
      return FALSE;
    }

  x = lm_message_node_get_child_with_namespace (feature, "x", NS_DATA);
  if (x == NULL)
    {
      NODE_DEBUG (message->node, "got a SI request without a X data field");
      return FALSE;
    }

  for (field = x->children; field; field = field->next)
    {
      if (tp_strdiff (lm_message_node_get_attribute (field, "var"),
            "stream-method"))
        /* some future field, ignore it */
        continue;

      if (tp_strdiff (lm_message_node_get_attribute (field, "type"),
            "list-single"))
        {
          NODE_DEBUG(message->node, "SI request's stream-method field was not "
              "of type list-single");
          return FALSE;
        }

      /* Get the stream methods offered */
      *stream_methods = NULL;
      for (stream_method = field->children; stream_method;
          stream_method = stream_method->next)
        {
          LmMessageNode *value;
          const gchar *stream_method_str;

          value = lm_message_node_get_child (stream_method, "value");
          if (value == NULL)
            continue;

          stream_method_str = lm_message_node_get_value (value);
          if (!tp_strdiff (stream_method_str, ""))
            continue;

          DEBUG ("Got stream-method %s", stream_method_str);

          /* Append to the stream_methods list */
          *stream_methods = g_slist_append (*stream_methods,
              (gchar *) stream_method_str);
        }

      /* no need to parse the rest of the fields, we've found the one we
       * wanted */
      break;
    }

  if (*stream_methods == NULL)
    {
      NODE_DEBUG (message->node,
          "got a SI request without stream method proposed");
      return FALSE;
    }

  return TRUE;
}

/**
 * gabble_bytestream_factory_make_stream_init_iq
 *
 * @full_jid: the full jid of the contact to whom we want to offer the stream
 * @stream_id: the stream ID of the new stream
 * @profile: the profile associated with the stream
 *
 * Create a SI request IQ as described in XEP-0095.
 *
 * The MIME type is not set - the receiving client will assume
 * application/octet-stream unless the caller sets a MIME type explicitly.
 */
LmMessage *
gabble_bytestream_factory_make_stream_init_iq (const gchar *full_jid,
                                               const gchar *stream_id,
                                               const gchar *profile)
{
  return lm_message_build (full_jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "set",
      '(', "si", "",
        '@', "xmlns", NS_SI,
        '@', "id", stream_id,
        '@', "profile", profile,
        '(', "feature", "",
          '@', "xmlns", NS_FEATURENEG,
          '(', "x", "",
            '@', "xmlns", NS_DATA,
            '@', "type", "form",
            '(', "field", "",
              '@', "var", "stream-method",
              '@', "type", "list-single",
              '(', "option", "",
                '(', "value", NS_IBB,
                ')',
              ')',
            ')',
          ')',
        ')',
      ')', NULL);
}

/**
 * bytestream_factory_iq_si_cb:
 *
 * Called by loudmouth when we get an incoming <iq>. This handler is concerned
 * with Stream Initiation requests (XEP-0095).
 *
 */
static LmHandlerResult
bytestream_factory_iq_si_cb (LmMessageHandler *handler,
                             LmConnection *lmconn,
                             LmMessage *msg,
                             gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (user_data);
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  LmMessageNode *si;
  TpHandle peer_handle, room_handle;
  GabbleBytestreamIface *bytestream = NULL;
  GSList *l;
  const gchar *profile, *from, *stream_id, *stream_init_id, *mime_type;
  GSList *stream_methods = NULL;
  gchar *peer_resource = NULL;

  if (lm_message_get_sub_type (msg) != LM_MESSAGE_SUB_TYPE_SET)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  si = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  if (si == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* after this point, the message is for us, so in all cases we either handle
   * it or send an error reply */

  if (!streaminit_parse_request (msg, si, &profile, &from, &stream_id,
        &stream_init_id, &mime_type, &stream_methods))
    {
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, "failed to parse SI request");
      goto out;
    }

  DEBUG ("received a SI request");

  peer_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (peer_handle == 0)
    {
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_JID_MALFORMED, NULL);
      goto out;
    }

  room_handle = gabble_get_room_handle_from_jid (room_repo, from);
  if (room_handle == 0)
    {
      /* JID is not a MUC JID so we need contact's resource */
      gabble_decode_jid (from, NULL, NULL, &peer_resource);
    }

  /* check stream method */
  for (l = stream_methods; l != NULL; l = l->next)
    {
      /* We create the stream according the stream method chosen.
       * User has to accept it */
      if (!tp_strdiff (l->data, NS_IBB))
        {
          bytestream = (GabbleBytestreamIface *)
            gabble_bytestream_factory_create_ibb (self, peer_handle,
              stream_id, stream_init_id, peer_resource,
              GABBLE_BYTESTREAM_STATE_LOCAL_PENDING);
          break;
        }
    }

  if (bytestream == NULL)
    {
      DEBUG ("SI request doesn't contain any supported stream methods.");

      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_SI_NO_VALID_STREAMS, NULL);
      goto out;
    }

  /* Now that we have a bytestream, it's responsible for declining the IQ
   * if needed. */

  /* We inform the right factory we received a SI request */
  if (tp_strdiff (profile, NS_TUBES))
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_SI_BAD_PROFILE, "" };
      DEBUG ("SI profile unsupported: %s", profile);

      gabble_bytestream_iface_close (bytestream, &e);
      goto out;
    }

  /* A Tubes SI request can be:
   *  - a 1-1 new tube offer
   *  - a 1-1 tube extra bytestream offer
   *  - a muc tube extra bytestream offer
   */
  if (lm_message_node_get_child_with_namespace (si, "tube", NS_TUBES) != NULL)
    {
      /* The SI request is a tube offer */
       gabble_tubes_factory_handle_si_tube_request (priv->conn->tubes_factory,
           bytestream, peer_handle, stream_id, msg);
    }
  else if (lm_message_node_get_child_with_namespace (si, "stream", NS_TUBES)
      != NULL)
    {
      /* The SI request is an extra bytestream for a 1-1 tube */
      gabble_tubes_factory_handle_si_stream_request (priv->conn->tubes_factory,
          bytestream, peer_handle, stream_id, msg);
    }
  else if (lm_message_node_get_child_with_namespace (si, "muc-stream",
        NS_TUBES) != NULL)
    {
      /* The SI request is an extra bytestream for a muc tube */

      if (room_handle == 0)
        {
          GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
              "<muc-stream> is only valid in a MUC context" };

          gabble_bytestream_iface_close (bytestream, &e);
        }
      else
        {
          gabble_muc_factory_handle_si_stream_request (priv->conn->muc_factory,
              bytestream, room_handle, stream_id, msg);
        }
    }
  else
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "Invalid tube SI request: expected <tube>, <stream> or "
          "<muc-stream>" };

      /* Invalid tube SI request */
      NODE_DEBUG (msg->node, e.message);
      gabble_bytestream_iface_close (bytestream, &e);
    }

out:
  g_slist_free (stream_methods);
  g_free (peer_resource);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
handle_ibb_open_iq (GabbleBytestreamFactory *self,
                    LmMessage *msg)
{
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  GabbleBytestreamIBB *bytestream;
  LmMessageNode *open_node;
  ConstBytestreamIdentifier bsid = { NULL, NULL };
  const gchar *tmp;

  if (lm_message_get_sub_type (msg) != LM_MESSAGE_SUB_TYPE_SET)
    return FALSE;

  open_node = lm_message_node_get_child_with_namespace (msg->node, "open",
      NS_IBB);
  if (open_node == NULL)
    return FALSE;

  bsid.jid = lm_message_node_get_attribute (msg->node, "from");
  if (bsid.jid == NULL)
    {
      DEBUG ("got a message without a from field");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, NULL);
      return TRUE;
    }

  bsid.stream = lm_message_node_get_attribute (open_node, "sid");
  if (bsid.stream == NULL)
    {
      DEBUG ("IBB open stanza doesn't contain stream id");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, NULL);
      return TRUE;
    }

  bytestream = g_hash_table_lookup (priv->ibb_bytestreams, &bsid);
  if (bytestream == NULL)
    {
      /* We don't accept streams not previously announced using SI */
      DEBUG ("unknown stream: <%s> from <%s>", bsid.stream, bsid.jid);
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, NULL);
      return TRUE;
    }

  tmp = lm_message_node_get_attribute (open_node, "block-size");
  if (tmp != NULL)
    {
      guint block_size = strtoul (tmp, NULL, 10);

      if (block_size > 0)
        g_object_set (bytestream, "block-size", block_size, NULL);
    }

  g_object_set (bytestream, "state", GABBLE_BYTESTREAM_STATE_OPEN,
      NULL);

  _gabble_connection_acknowledge_set_iq (priv->conn, msg);

  return TRUE;
}

static gboolean
handle_ibb_close_iq (GabbleBytestreamFactory *self,
                     LmMessage *msg)
{
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  ConstBytestreamIdentifier bsid = { NULL, NULL };
  GabbleBytestreamIBB *bytestream;
  LmMessage *reply;
  LmMessageNode *close_node;

  if (lm_message_get_sub_type (msg) != LM_MESSAGE_SUB_TYPE_SET)
    return FALSE;

  close_node = lm_message_node_get_child_with_namespace (msg->node, "close",
      NS_IBB);
  if (close_node == NULL)
    return FALSE;

  bsid.jid = lm_message_node_get_attribute (msg->node, "from");
  if (bsid.jid == NULL)
    {
      DEBUG ("got a message without a from field");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, "IBB <close> has no 'from' attribute");
      return TRUE;
    }

  bsid.stream = lm_message_node_get_attribute (close_node, "sid");
  if (bsid.stream == NULL)
    {
      DEBUG ("IBB close stanza doesn't contain stream id");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, "IBB <close> has no stream ID");
      return TRUE;
    }

  bytestream = g_hash_table_lookup (priv->ibb_bytestreams, &bsid);
  if (bytestream == NULL)
    {
      GabbleXmppError error = XMPP_ERROR_ITEM_NOT_FOUND;

      DEBUG ("unknown stream: <%s> from <%s>", bsid.stream, bsid.jid);

      reply = lm_message_new_with_sub_type (bsid.jid,
          LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_ERROR);

      gabble_xmpp_error_to_node (error, reply->node, NULL);
    }
  else
    {
      DEBUG ("received IBB close stanza. Bytestream closed");

      g_object_set (bytestream, "state", GABBLE_BYTESTREAM_STATE_CLOSED,
          NULL);

      reply = lm_message_new_with_sub_type (bsid.jid, LM_MESSAGE_TYPE_IQ,
          LM_MESSAGE_SUB_TYPE_RESULT);
    }

  lm_message_node_set_attribute (reply->node,
      "id", lm_message_node_get_attribute (msg->node, "id"));

  _gabble_connection_send (priv->conn, reply, NULL);

  lm_message_unref (reply);
  return TRUE;
}

/* IBB can be transported over either IQs or messages, so msg can either be
 * an <iq> or a <message>. If it's an <iq> we need to reply to it.
 *
 * Return TRUE if we take responsibility for this message. */
static gboolean
handle_ibb_data (GabbleBytestreamFactory *self,
                 LmMessage *msg,
                 gboolean is_iq)
{
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  GabbleBytestreamIBB *bytestream = NULL;
  LmMessageNode *data;
  ConstBytestreamIdentifier bsid = { NULL, NULL };

  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  if (is_iq && lm_message_get_sub_type (msg) != LM_MESSAGE_SUB_TYPE_SET)
    return FALSE;

  data = lm_message_node_get_child_with_namespace (msg->node, "data", NS_IBB);
  if (data == NULL)
    return FALSE;

  bsid.jid = lm_message_node_get_attribute (msg->node, "from");
  if (bsid.jid == NULL)
    {
      DEBUG ("got a message without a from field");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, "IBB <close> has no 'from' attribute");
      return TRUE;
    }

  bsid.stream = lm_message_node_get_attribute (data, "sid");
  if (bsid.stream == NULL)
    {
      DEBUG ("got a IBB message data without a stream id field");
      if (is_iq)
        _gabble_connection_send_iq_error (priv->conn, msg,
            XMPP_ERROR_BAD_REQUEST, "IBB <data> needs a stream ID");
      return TRUE;
    }

  bytestream = g_hash_table_lookup (priv->ibb_bytestreams, &bsid);

  if (bytestream == NULL)
    {
      DEBUG ("unknown stream: <%s> from <%s>", bsid.stream, bsid.jid);
      if (is_iq)
        _gabble_connection_send_iq_error (priv->conn, msg,
            XMPP_ERROR_BAD_REQUEST, "IBB <data> has unknown stream ID");

      return TRUE;
    }

  gabble_bytestream_ibb_receive (bytestream, msg, is_iq);

  return TRUE;
}

static gboolean
handle_muc_data (GabbleBytestreamFactory *self,
                 LmMessage *msg)
{
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  GabbleBytestreamMuc *bytestream = NULL;
  LmMessageNode *data;
  ConstBytestreamIdentifier bsid = { NULL, NULL };
  gchar *room_name;
  const gchar *from;

  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  data = lm_message_node_get_child_with_namespace (msg->node, "data",
      NS_MUC_BYTESTREAM);
  if (data == NULL)
    return FALSE;

  from = lm_message_node_get_attribute (msg->node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field");
      return TRUE;
    }

  bsid.stream = lm_message_node_get_attribute (data, "sid");
  if (bsid.stream == NULL)
    {
      DEBUG ("got a pseudo IBB muc message data without a stream id field");
      return TRUE;
    }

  room_name = gabble_remove_resource (from);
  bsid.jid = room_name;
  bytestream = g_hash_table_lookup (priv->muc_bytestreams, &bsid);

  if (bytestream == NULL)
    {
      DEBUG ("unknown muc stream: <%s> from <%s>", bsid.stream, bsid.jid);
      g_free (room_name);
      return TRUE;
    }
  g_assert (GABBLE_IS_BYTESTREAM_MUC (bytestream));

  gabble_bytestream_muc_receive (bytestream, msg);

  g_free (room_name);
  return TRUE;
}

/**
 * bytestream_factory_iq_ibb_cb:
 *
 * Called by loudmouth when we get an incoming <iq>.
 * This handler is concerned with IBB iq's.
 *
 */
static LmHandlerResult
bytestream_factory_iq_ibb_cb (LmMessageHandler *handler,
                              LmConnection *lmconn,
                              LmMessage *msg,
                              gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (user_data);

  if (handle_ibb_open_iq (self, msg))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (handle_ibb_close_iq (self, msg))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (handle_ibb_data (self, msg, TRUE))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/**
 * bytestream_factory_msg_data_cb
 *
 * Called by loudmouth when we get an incoming <message>.
 * This handler handles IBB data and pseudo IBB Muc data.
 */
static LmHandlerResult
bytestream_factory_msg_data_cb (LmMessageHandler *handler,
                                LmConnection *lmconn,
                                LmMessage *msg,
                                gpointer user_data)
{
  GabbleBytestreamFactory *self = user_data;

  if (handle_ibb_data (self, msg, FALSE))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (handle_muc_data (self, msg))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

GabbleBytestreamFactory *
gabble_bytestream_factory_new (GabbleConnection *conn)
{
  GabbleBytestreamFactory *factory;

  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  factory = GABBLE_BYTESTREAM_FACTORY (
      g_object_new (GABBLE_TYPE_BYTESTREAM_FACTORY,
        "connection", conn,
        NULL));

  return factory;
}

static void
bytestream_state_changed_cb (GabbleBytestreamIface *bytestream,
                             GabbleBytestreamState state,
                             gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (user_data);

  if (state == GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      remove_bytestream (self, bytestream);
    }
}

gchar *
gabble_bytestream_factory_generate_stream_id (void)
{
  gchar *stream_id;

  stream_id = g_strdup_printf ("%lu-%u", (unsigned long) time (NULL),
      g_random_int ());

  return stream_id;
}

GabbleBytestreamIBB *
gabble_bytestream_factory_create_ibb (GabbleBytestreamFactory *self,
                                      TpHandle peer_handle,
                                      const gchar *stream_id,
                                      const gchar *stream_init_id,
                                      const gchar *peer_resource,
                                      GabbleBytestreamState state)
{
  GabbleBytestreamFactoryPrivate *priv;
  GabbleBytestreamIBB *ibb;
  BytestreamIdentifier *id;

  g_return_val_if_fail (GABBLE_IS_BYTESTREAM_FACTORY (self), NULL);
  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  ibb = g_object_new (GABBLE_TYPE_BYTESTREAM_IBB,
                      "connection", priv->conn,
                      "peer-handle", peer_handle,
                      "stream-id", stream_id,
                      "state", state,
                      "stream-init-id", stream_init_id,
                      "peer-resource", peer_resource,
                      NULL);

  g_signal_connect (ibb, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), self);

  id = bytestream_id_new (GABBLE_BYTESTREAM_IFACE (ibb));
  DEBUG ("add private bytestream <%s> from <%s>", id->stream, id->jid);
  g_hash_table_insert (priv->ibb_bytestreams, id, ibb);

  return ibb;
}

GabbleBytestreamMuc *
gabble_bytestream_factory_create_muc (GabbleBytestreamFactory *self,
                                      TpHandle peer_handle,
                                      const gchar *stream_id,
                                      GabbleBytestreamState state)
{
  GabbleBytestreamFactoryPrivate *priv;
  GabbleBytestreamMuc *bytestream;
  BytestreamIdentifier *id;

  g_return_val_if_fail (GABBLE_IS_BYTESTREAM_FACTORY (self), NULL);
  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  bytestream = g_object_new (GABBLE_TYPE_BYTESTREAM_MUC,
                      "connection", priv->conn,
                      "peer-handle", peer_handle,
                      "stream-id", stream_id,
                      "state", state,
                      NULL);

  g_signal_connect (bytestream, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), self);

  id = bytestream_id_new (GABBLE_BYTESTREAM_IFACE (bytestream));
  DEBUG ("add muc bytestream <%s> from <%s>", id->stream, id->jid);
  g_hash_table_insert (priv->muc_bytestreams, id, bytestream);

  return bytestream;
}

struct _streaminit_reply_cb_data
{
  gchar *stream_id;
  GabbleBytestreamFactoryNegotiateReplyFunc func;
  gpointer user_data;
};

/* Called when we receive the reply of a SI request */
static LmHandlerResult
streaminit_reply_cb (GabbleConnection *conn,
                     LmMessage *sent_msg,
                     LmMessage *reply_msg,
                     GObject *obj,
                     gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (obj);
  struct _streaminit_reply_cb_data *data =
    (struct _streaminit_reply_cb_data*) user_data;
  GabbleBytestreamIface *bytestream = NULL;
  gchar *peer_resource = NULL;
  LmMessageNode *si, *feature, *x, *field, *value;
  const gchar *from, *stream_method;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle peer_handle = 0;
  gboolean success = FALSE;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      DEBUG ("stream %s declined", data->stream_id);
      goto END;
    }

  /* stream accepted */

  from = lm_message_node_get_attribute (reply_msg->node, "from");
  if (from == NULL)
    {
      NODE_DEBUG (reply_msg->node, "got a message without a from field");
      goto END;
    }

  peer_handle = tp_handle_ensure (contact_repo, from, NULL, NULL);

  if (gabble_get_room_handle_from_jid (room_repo, from) == 0)
    {
     /* jid is not a muc jid so we need contact's resource */
     gabble_decode_jid (from, NULL, NULL, &peer_resource);
    }

  si = lm_message_node_get_child_with_namespace (reply_msg->node, "si",
      NS_SI);
  if (si == NULL)
    {
      NODE_DEBUG (reply_msg->node, "got a SI reply without a si field");
      goto END;
    }

  feature = lm_message_node_get_child_with_namespace (si, "feature",
      NS_FEATURENEG);
  if (feature == NULL)
    {
      NODE_DEBUG (reply_msg->node,
          "got a SI reply without a feature field");
      goto END;
    }

  x = lm_message_node_get_child_with_namespace (feature, "x", NS_X_DATA);
  if (x == NULL)
    {
      NODE_DEBUG (reply_msg->node, "got a SI reply without a x field");
      goto END;
    }

  field = lm_message_node_get_child (x, "field");
  if (field == NULL ||
      tp_strdiff (lm_message_node_get_attribute (field, "var"),
        "stream-method"))
    {
      NODE_DEBUG (reply_msg->node,
          "got a SI reply without stream methods");
      goto END;
    }

  value = lm_message_node_get_child (field, "value");
  if (value == NULL)
    {
      NODE_DEBUG (reply_msg->node,
          "got a SI reply without stream-method value");
      goto END;
    }

  stream_method = lm_message_node_get_value (value);

  if (!tp_strdiff (stream_method, NS_IBB))
    {
      /* Remote user have accepted the stream */
      bytestream = GABBLE_BYTESTREAM_IFACE (
          gabble_bytestream_factory_create_ibb (self, peer_handle,
          data->stream_id, NULL, peer_resource,
          GABBLE_BYTESTREAM_STATE_INITIATING));
    }
  else
    {
      DEBUG ("Remote user chose an unsupported stream method");
      goto END;
    }

  DEBUG ("stream %s accepted", data->stream_id);

  /* Let's start the initiation of the stream */
  if (gabble_bytestream_iface_initiate (bytestream))
    {
      /* FIXME: we should really only "succeed" when our <open> succeeds.
       * It only really matters from the point of view of the data->func */
      success = TRUE;
    }

END:
  if (!success)
    {
      /* Initiation failed. We remove the stream */
      remove_bytestream (self, bytestream);
      bytestream = NULL;
    }

  /* user callback */
  data->func (bytestream, (const gchar*) data->stream_id, reply_msg,
      data->user_data);

  if (peer_resource != NULL)
    g_free (peer_resource);

  if (peer_handle != 0)
    tp_handle_unref (contact_repo, peer_handle);

  g_free (data->stream_id);
  g_slice_free (struct _streaminit_reply_cb_data, data);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/*
 * gabble_bytestream_factory_negotiate_stream:
 *
 * @msg: the SI negotiation IQ (created using
 * gabble_bytestream_factory_make_stream_init_iq)
 * @stream_id: the stream identifier
 * @func: the callback to call when we receive the answser of the request
 * @user_data: user data to pass to the callback
 * @error: pointer in which to return a GError in case of failure.
 *
 * Send a Stream Initiation (XEP-0095) request.
 */
gboolean
gabble_bytestream_factory_negotiate_stream (GabbleBytestreamFactory *self,
                                            LmMessage *msg,
                                            const gchar *stream_id,
                                            GabbleBytestreamFactoryNegotiateReplyFunc func,
                                            gpointer user_data,
                                            GError **error)
{
  GabbleBytestreamFactoryPrivate *priv;
  struct _streaminit_reply_cb_data *data;
  gboolean result;

  g_assert (GABBLE_IS_BYTESTREAM_FACTORY (self));
  g_assert (stream_id != NULL);
  g_assert (func != NULL);

  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  data = g_slice_new (struct _streaminit_reply_cb_data);
  data->stream_id = g_strdup (stream_id);
  data->func = func;
  data->user_data = user_data;

  result = _gabble_connection_send_with_reply (priv->conn, msg,
      streaminit_reply_cb, G_OBJECT (self), data, error);

  if (!result)
    {
      g_free (data->stream_id);
      g_slice_free (struct _streaminit_reply_cb_data, data);
    }

  return result;
}

/*
 * gabble_bytestream_factory_make_accept_iq
 *
 * @full_jid: the full jid of the stream initiator
 * @stream_init_id: the id of the SI request
 * @stream_method: the stream method chosen (one of them proposed
 * in the SI request)
 *
 * Create an IQ stanza accepting a stream in response to
 * a SI request (XEP-0095).
 *
 */
LmMessage *
gabble_bytestream_factory_make_accept_iq (const gchar *full_jid,
                                          const gchar *stream_init_id,
                                          const gchar *stream_method)
{
  return lm_message_build (full_jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "result",
      '@', "id", stream_init_id,
      '(', "si", "",
        '@', "xmlns", NS_SI,
        '(', "feature", "",
          '@', "xmlns", NS_FEATURENEG,
          '(', "x", "",
            '@', "xmlns", NS_DATA,
            '@', "type", "submit",
            '(', "field", "",
              '@', "var", "stream-method",
              '(', "value", stream_method, ')',
            ')',
          ')',
        ')',
      ')', NULL);
}
