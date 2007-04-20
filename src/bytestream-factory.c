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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_BYTESTREAM

#include "debug.h"
#include "gabble-connection.h"
#include "bytestream-ibb.h"
#include "bytestream-factory.h"
#include "namespaces.h"
#include "util.h"
#include "presence-cache.h"
#include <telepathy-glib/interfaces.h>

G_DEFINE_TYPE (GabbleBytestreamFactory, gabble_bytestream_factory,
    G_TYPE_OBJECT);

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleBytestreamFactoryPrivate GabbleBytestreamFactoryPrivate;
struct _GabbleBytestreamFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *iq_cb;
  LmMessageHandler *message_cb;

  GHashTable *ibb_bytestreams;

  gboolean dispose_has_run;
};

#define GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE(obj) \
    ((GabbleBytestreamFactoryPrivate *) obj->priv)

static LmHandlerResult
bytestream_factory_message_cb (LmMessageHandler *handler, LmConnection *lmconn,
    LmMessage *message, gpointer user_data);

static LmHandlerResult
bytestream_factory_iq_cb (LmMessageHandler *handler, LmConnection *lmconn,
    LmMessage *message, gpointer user_data);

static void
gabble_bytestream_factory_init (GabbleBytestreamFactory *self)
{
  GabbleBytestreamFactoryPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_FACTORY, GabbleBytestreamFactoryPrivate);

  self->priv = priv;

  priv->ibb_bytestreams = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  priv->iq_cb = NULL;
  priv->message_cb = NULL;

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

  priv->message_cb = lm_message_handler_new (bytestream_factory_message_cb,
      self, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->message_cb,
      LM_MESSAGE_TYPE_MESSAGE, LM_HANDLER_PRIORITY_FIRST);

  priv->iq_cb = lm_message_handler_new (bytestream_factory_iq_cb, self, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->iq_cb,
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
      priv->message_cb, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->message_cb);

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->iq_cb, LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_cb);

  g_hash_table_destroy (priv->ibb_bytestreams);
  priv->ibb_bytestreams = NULL;

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

/**
 * streaminit_parse_request
 *
 * Parses a SI request, or returns FALSE
 * if it can't be parsed.
 */
static gboolean
streaminit_parse_request (LmMessage *message,
                          const gchar **profile,
                          const gchar **from,
                          const gchar **stream_id,
                          const gchar **stream_init_id,
                          const gchar **mime_type,
                          GSList **stream_methods)
{
  LmMessageNode *iq, *si, *feature, *x, *field, *stream_method;

  if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_SET)
    return FALSE;

  iq = lm_message_get_node (message);

  *stream_init_id = lm_message_node_get_attribute (iq, "id");

  *from = lm_message_node_get_attribute (message->node, "from");
  if (*from == NULL)
    {
      NODE_DEBUG (message->node, "got a message without a from field");
      return FALSE;
    }

  /* Parse <si> */
  si = lm_message_node_get_child_with_namespace (iq, "si", NS_SI);
  if (si == NULL)
    return FALSE;

  *stream_id = lm_message_node_get_attribute (si, "id");
  if (*stream_id == NULL)
    {
      NODE_DEBUG (message->node, "got a SI request without a stream id field");
      return FALSE;
    }

  *mime_type = lm_message_node_get_attribute (si, "mime-type");
  /* if no mime_type is defined, we assume "binary/octect-stream" */

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

  field = lm_message_node_get_child (x, "field");
  if (field == NULL)
    {
      NODE_DEBUG (message->node,
          "got a SI request without stream method list");
      return FALSE;
    }

  if (tp_strdiff (lm_message_node_get_attribute (field, "var"),
        "stream-method"))
    {
      NODE_DEBUG (message->node,
          "got a SI request without stream method list");
      return FALSE;
    }

  if (tp_strdiff (lm_message_node_get_attribute (field, "type"),
        "list-single"))
    {
      NODE_DEBUG (message->node,
          "got a SI request without stream method list");
      return FALSE;
    }

  /* Get the stream methods offered */
  *stream_methods = NULL;
  for (stream_method = field->children; stream_method;
      stream_method = stream_method->next)
    {
      LmMessageNode *value;

      value = lm_message_node_get_child (stream_method, "value");
      if (value != NULL)
        {
          const gchar *stream_method;

          stream_method = lm_message_node_get_value (value);

          if (!tp_strdiff (stream_method, ""))
            continue;

          DEBUG ("Got stream-method %s", stream_method);

          /* Append to the stream_methods list */
          *stream_methods = g_slist_append (*stream_methods,
              (gchar*) stream_method);
        }
    }

  if (*stream_methods == NULL)
    {
      NODE_DEBUG (message->node,
          "got a SI request without stream method proposed");
      return FALSE;
    }

  return TRUE;
}

static LmMessage *
streaminit_create_request (const gchar *full_jid,
                           const gchar *stream_id,
                           const gchar *profile)
{
  LmMessage *msg;
  LmMessageNode *lm_node;
  LmMessageNode *field;

  msg = lm_message_new_with_sub_type (full_jid, LM_MESSAGE_TYPE_IQ,
                                           LM_MESSAGE_SUB_TYPE_SET);

  lm_node = lm_message_node_add_child (msg->node, "si", NULL);
  lm_message_node_set_attributes (lm_node,
      "xmlns", NS_SI,
      "id", stream_id,
      "profile", profile,
      "mime-type", "binary/octect-stream",
      NULL);

  lm_node = lm_message_node_add_child (lm_node, "feature", NULL);
  lm_message_node_set_attribute (lm_node, "xmlns", NS_FEATURENEG);

  lm_node = lm_message_node_add_child (lm_node, "x", NULL);
  lm_message_node_set_attributes (lm_node,
      "xmlns", NS_DATA,
      "type", "form",
      NULL);

  field = lm_message_node_add_child (lm_node, "field", NULL);
  lm_message_node_set_attributes (field,
      "var", "stream-method",
      "type", "list-single",
      NULL);

  /* Available bytestream methods: */
  lm_node = lm_message_node_add_child (field, "option", NULL);
  lm_message_node_add_child (lm_node, "value", NS_IBB);

  return msg;
}

/**
 * bytestream_factory_iq_cb:
 *
 * Called by loudmouth when we get an incoming <iq>.
 * This handler is concerned with Stream Initiation requests.
 *
 */

static LmHandlerResult
bytestream_factory_iq_cb (LmMessageHandler *handler,
                          LmConnection *lmconn,
                          LmMessage *msg,
                          gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (user_data);
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle peer_handle;
  GabbleBytestreamIBB *bytestream = NULL;
  GSList *l;
  const gchar *profile, *from, *stream_id, *stream_init_id, *mime_type;
  GSList *stream_methods = NULL;
  gchar *peer_resource;

  if (!streaminit_parse_request (msg, &profile, &from, &stream_id,
        &stream_init_id, &mime_type, &stream_methods))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  peer_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (peer_handle == 0)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  gabble_decode_jid (from, NULL, NULL, &peer_resource);

  /* check stream method */
  for (l = stream_methods; l != NULL; l = l->next)
    {
      if (!tp_strdiff (l->data, NS_IBB))
        {
          /* We create the stream according the stream method chosen.
           * User have to accept it before we consider it as open */
          bytestream = gabble_bytestream_factory_create_ibb (self, peer_handle,
              TP_HANDLE_TYPE_CONTACT, stream_id, stream_init_id, peer_resource,
              FALSE);
          break;
        }
    }

  if (bytestream != NULL)
    {
#ifdef HAVE_DBUS_TUBE
      /* We inform the right factory when received a SI request */
      if (strcmp (profile, NS_SI_TUBES) == 0)
        {
          gabble_tubes_factory_handle_request (priv->conn->tubes_factory,
              bytestream, peer_handle, stream_id, msg);
        }
#endif
    }
  else
    {
      DEBUG ("unknown stream method");
    }

  g_slist_free (stream_methods);
  g_free (peer_resource);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


/**
 * bytestream_factory_message_cb
 *
 * Called by loudmouth when we get an incoming <message>.
 */
static LmHandlerResult
bytestream_factory_message_cb (LmMessageHandler *handler,
                               LmConnection *lmconn,
                               LmMessage *msg,
                               gpointer user_data)
{
  GabbleBytestreamFactory *self = user_data;
  GabbleBytestreamFactoryPrivate *priv;
  GabbleBytestreamIBB *bytestream;
  LmMessageNode *data;
  const gchar *stream_id;

  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  data = lm_message_node_get_child_with_namespace (msg->node, "data", NS_IBB);

  if (data == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  stream_id = lm_message_node_get_attribute (data, "sid");

  bytestream = g_hash_table_lookup (priv->ibb_bytestreams, stream_id);

  if (bytestream == NULL)
    {
      DEBUG ("unknow stream: %s", stream_id);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  gabble_bytestream_ibb_receive (bytestream, msg);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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
bytestream_closed_cb (GabbleBytestreamIBB *bytestream,
                      gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (user_data);
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE
    (self);
  gchar *stream_id;

  if (priv->ibb_bytestreams == NULL)
    return;

  g_object_get (bytestream, "stream-id", &stream_id, NULL);
  g_hash_table_remove (priv->ibb_bytestreams, stream_id);
  g_free (stream_id);
}

gchar *
gabble_bytestream_factory_generate_stream_id (void)
{
  gchar *stream_id;
  time_t curtime;
  struct tm *loctime;
  gchar stamp[20];

  curtime = time (NULL);
  loctime = localtime (&curtime);
  strftime (stamp, sizeof (stamp), "%s", loctime);
  stream_id = g_strdup_printf ("%s%d", stamp, g_random_int());

  return stream_id;
}

GabbleBytestreamIBB *
gabble_bytestream_factory_create_ibb (GabbleBytestreamFactory *self,
                                      TpHandle peer_handle,
                                      TpHandleType peer_handle_type,
                                      const gchar *stream_id,
                                      const gchar *stream_init_id,
                                      const gchar *peer_resource,
                                      gboolean open)
{
  GabbleBytestreamFactoryPrivate *priv;
  GabbleBytestreamIBB *ibb;

  g_return_val_if_fail (GABBLE_IS_BYTESTREAM_FACTORY (self), NULL);
  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  ibb = g_object_new (GABBLE_TYPE_BYTESTREAM_IBB,
                      "connection", priv->conn,
                      "peer-handle", peer_handle,
                      "peer-handle-type", peer_handle_type,
                      "stream-id", stream_id,
                      "open", open,
                      NULL);

  if (stream_init_id != NULL)
    g_object_set (G_OBJECT (ibb), "stream-init-id", stream_init_id, NULL);

  if (peer_resource != NULL)
    g_object_set (G_OBJECT (ibb), "peer-resource", peer_resource, NULL);

  g_signal_connect (ibb, "closed", G_CALLBACK (bytestream_closed_cb), self);

  g_hash_table_insert (priv->ibb_bytestreams, g_strdup (stream_id), ibb);

  return ibb;
}

struct _streaminit_reply_cb_data
{
  gchar *stream_id;
  TpHandle peer_handle;
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
  GabbleBytestreamIBB *ibb = NULL;
  gchar *peer_resource = NULL;
  LmMessageNode *si, *feature, *x, *field, *value;
  const gchar *from, *stream_method;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      DEBUG ("stream %s declined\n", data->stream_id);
      goto END;
    }

  /* stream accepted */

  from = lm_message_node_get_attribute (reply_msg->node, "from");

  if (from == NULL)
    {
      NODE_DEBUG (reply_msg->node, "got a message without a from field");
      goto END;
    }

  gabble_decode_jid (from, NULL, NULL, &peer_resource);

  si = lm_message_node_get_child_with_namespace (reply_msg->node, "si",
      NS_SI);

  DEBUG ("stream %s accepted\n", data->stream_id);

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
      /* Remote user have accepted the stream so it's
       * open */
      ibb = gabble_bytestream_factory_create_ibb (self, data->peer_handle,
          TP_HANDLE_TYPE_CONTACT, data->stream_id, NULL,
          peer_resource, TRUE);
    }

END:
  /* user callback */
  data->func (ibb, (const gchar*) data->stream_id, reply_msg, data->user_data);

  if (peer_resource != NULL)
    g_free (peer_resource);

  g_free (data->stream_id);
  g_slice_free (struct _streaminit_reply_cb_data, data);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/*
 * gabble_bytestream_factory_negotiate_stream:
 *
 * @peer_handle: the handle of the contact to who you want to send the request
 * @profile: the namespace of the profile associated with the bytestream
 * @stream_id: the stream identifier
 * @node: a LmMessageNode containing profile specific information, or NULL if
 * any
 * @func: the callback to call when we receive the answser of the request
 * @user_data: user data to pass to the callback
 *
 * Send a Stream Initiation (XEP-0095) request.
 */
void
gabble_bytestream_factory_negotiate_stream (GabbleBytestreamFactory *self,
                                            TpHandle peer_handle,
                                            const gchar *profile,
                                            const gchar *stream_id,
                                            LmMessageNode *node,
                                            GabbleBytestreamFactoryNegotiateReplyFunc func,
                                            gpointer user_data)
{
  GabbleBytestreamFactoryPrivate *priv;
  TpHandleRepoIface *contact_repo;
  LmMessage *msg;
  GabblePresence *presence;
  const gchar *jid, *resource;
  gchar *full_jid;
  struct _streaminit_reply_cb_data *data;

  g_return_if_fail (GABBLE_IS_BYTESTREAM_FACTORY (self));
  g_return_if_fail (peer_handle != 0);
  g_return_if_fail (profile != NULL);
  g_return_if_fail (stream_id != NULL);
  g_return_if_fail (func != NULL);

  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  jid = tp_handle_inspect (contact_repo, peer_handle);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      peer_handle);
  resource = gabble_presence_pick_resource_by_caps (presence,
      PRESENCE_CAP_SI);

  full_jid = g_strdup_printf ("%s/%s", jid, resource);
  msg = streaminit_create_request (full_jid, stream_id, profile);

  if (node != NULL)
    {
      lm_message_node_add_child_node (msg->node, node);
    }

  data = g_slice_new (struct _streaminit_reply_cb_data);
  data->stream_id = g_strdup (stream_id);
  data->peer_handle = peer_handle;
  data->func = func;
  data->user_data = user_data;

  // XXX manage send errors
  _gabble_connection_send_with_reply (priv->conn, msg,
      streaminit_reply_cb, G_OBJECT (self), data, NULL);

  lm_message_unref (msg);
  g_free (full_jid);
}
