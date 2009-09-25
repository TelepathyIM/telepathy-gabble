/*
 * bytestream-factory.c - Source for GabbleBytestreamFactory
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
#include "bytestream-factory.h"

#include <string.h>
#include <stdlib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_BYTESTREAM

#include "bytestream-ibb.h"
#include "bytestream-iface.h"
#include "bytestream-muc.h"
#include "bytestream-multiple.h"
#include "bytestream-socks5.h"
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "private-tubes-factory.h"
#include "util.h"

G_DEFINE_TYPE (GabbleBytestreamFactory, gabble_bytestream_factory,
    G_TYPE_OBJECT);

#define NB_SOCKS5_PROXIES_USED 5
#define SOCKS5_PROXY_TIMEOUT 10

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

static gboolean
bytestream_id_equal (gconstpointer v1,
                     gconstpointer v2)
{
  const ConstBytestreamIdentifier *left = v1;
  const ConstBytestreamIdentifier *right = v2;

  return (!tp_strdiff (left->jid, right->jid)) &&
      (!tp_strdiff (left->stream, right->stream));
}

static guint
bytestream_id_hash (gconstpointer v)
{
  const ConstBytestreamIdentifier *bsid = v;

  return g_str_hash (bsid->jid) ^ g_str_hash (bsid->stream);
}

static BytestreamIdentifier *
bytestream_id_new (GabbleBytestreamIface *bytestream)
{
  BytestreamIdentifier *bsid = g_slice_new (BytestreamIdentifier);

  g_object_get (bytestream,
      "stream-id", &(bsid->stream),
      "peer-jid", &(bsid->jid),
      NULL);
  return bsid;
}

static void
bytestream_id_free (gpointer v)
{
  BytestreamIdentifier *bsid = v;

  g_free (bsid->jid);
  g_free (bsid->stream);
  g_slice_free (BytestreamIdentifier, bsid);
}

static GabbleSocks5Proxy *
gabble_socks5_proxy_new (const gchar *jid,
                         const gchar *host,
                         const gchar *port)
{
  GabbleSocks5Proxy *proxy;

  proxy = g_slice_new (GabbleSocks5Proxy);
  proxy->jid = g_strdup (jid);
  proxy->host = g_strdup (host);
  proxy->port = g_strdup (port);

  return proxy;
}

static void
gabble_socks5_proxy_free (GabbleSocks5Proxy *proxy)
{
  g_free (proxy->jid);
  g_free (proxy->host);
  g_free (proxy->port);

  g_slice_free (GabbleSocks5Proxy, proxy);
}

struct _GabbleBytestreamFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *iq_si_cb;
  LmMessageHandler *iq_ibb_cb;
  LmMessageHandler *iq_socks5_cb;
  LmMessageHandler *msg_data_cb;

  /* SI-initiated bytestreams - data sent by normal messages, IQs used to
   * open and close.
   *
   * BytestreamIdentifier -> GabbleBytestreamIBB */
  GHashTable *ibb_bytestreams;
  /* BytestreamIdentifier -> GabbleBytestreamSocks5 */
  GHashTable *socks5_bytestreams;

  /* MUC pseudo-IBB - data sent by groupchat messages, IQs not allowed.
   *
   * BytestreamIdentifier -> GabbleBytestreamMuc */
  GHashTable *muc_bytestreams;

  /* SI-initiated bytestreams - real data sent through another bytestream.
   *
   * BytestreamIdentifier -> GabbleBytestreamMultiple */
  GHashTable *multiple_bytestreams;

  /* List of GabbleSocks5Proxy discovered on the connection */
  GSList *socks5_proxies;
  /* List of GabbleSocks5Proxy found using the fallback-socks5-proxies param */
  GSList *socks5_fallback_proxies;
  /* List of SOCKS5's jids that have not been queried yet */
  GSList *socks5_potential_proxies;
  guint socks5_proxies_timer;

  gboolean dispose_has_run;
};

#define GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE(obj) ((obj)->priv)

static LmHandlerResult
bytestream_factory_msg_data_cb (LmMessageHandler *handler,
    LmConnection *lmconn, LmMessage *message, gpointer user_data);

static LmHandlerResult
bytestream_factory_iq_si_cb (LmMessageHandler *handler, LmConnection *lmconn,
    LmMessage *message, gpointer user_data);

static LmHandlerResult
bytestream_factory_iq_ibb_cb (LmMessageHandler *handler, LmConnection *lmconn,
    LmMessage *message, gpointer user_data);

static LmHandlerResult
bytestream_factory_iq_socks5_cb (LmMessageHandler *handler,
    LmConnection *lmconn, LmMessage *message, gpointer user_data);

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

  priv->socks5_bytestreams = g_hash_table_new_full (bytestream_id_hash,
      bytestream_id_equal, bytestream_id_free, g_object_unref);

  priv->multiple_bytestreams = g_hash_table_new_full (bytestream_id_hash,
      bytestream_id_equal, bytestream_id_free, g_object_unref);
}

static LmHandlerResult
socks5_proxy_query_reply_cb (GabbleConnection *conn,
                             LmMessage *sent_msg,
                             LmMessage *reply_msg,
                             GObject *obj,
                             gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (obj);
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);
  LmMessageNode *query, *streamhost;
  const gchar *jid, *host, *port;
  GabbleSocks5Proxy *proxy;
  gboolean fallback = GPOINTER_TO_INT (user_data);

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  query = lm_message_node_get_child_with_namespace (reply_msg->node, "query",
      NS_BYTESTREAMS);
  if (query == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  streamhost = lm_message_node_get_child (query, "streamhost");
  if (streamhost == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  jid = lm_message_node_get_attribute (streamhost, "jid");
  host = lm_message_node_get_attribute (streamhost, "host");
  port = lm_message_node_get_attribute (streamhost, "port");

  if (jid == NULL || host == NULL || port == NULL)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  proxy = gabble_socks5_proxy_new (jid, host, port);

  if (fallback)
    {
      DEBUG ("Add fallback SOCKS5 proxy: %s %s:%s", jid, host, port);
      priv->socks5_fallback_proxies = g_slist_prepend (
          priv->socks5_fallback_proxies, proxy);
    }
  else
    {
      DEBUG ("Discovered SOCKS5 proxy: %s %s:%s", jid, host, port);
      priv->socks5_proxies = g_slist_prepend (priv->socks5_proxies, proxy);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
send_proxy_query (GabbleBytestreamFactory *self,
                  const gchar *jid,
                  gboolean fallback)
{
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);
  LmMessage *query;

  DEBUG ("send SOCKS5 query to %s", jid);

  query = lm_message_build (jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "get",
      '(', "query", "",
        '@', "xmlns", NS_BYTESTREAMS,
      ')', NULL);

  _gabble_connection_send_with_reply (priv->conn, query,
      socks5_proxy_query_reply_cb, G_OBJECT (self), GINT_TO_POINTER (fallback),
      NULL);

  lm_message_unref (query);
}

static void
disco_item_found_cb (GabbleDisco *disco,
                     GabbleDiscoItem *item,
                     GabbleBytestreamFactory *self)
{
  if (tp_strdiff (item->category, "proxy") ||
      tp_strdiff (item->type, "bytestreams"))
    return;

  send_proxy_query (self, item->jid, FALSE);
}

static void query_socks5_proxies (GabbleBytestreamFactory *self);

static gboolean
socks5_proxies_timeout_cb (gpointer data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (data);
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);

  priv->socks5_proxies_timer = 0;

  /* query more proxies if needed */
  query_socks5_proxies (self);

  return FALSE;
}

static void
query_socks5_proxies (GabbleBytestreamFactory *self)
{
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);
  guint nb_proxies_found;
  guint nb_proxies_needed;
  guint i;

  nb_proxies_found = g_slist_length (priv->socks5_proxies) +
    g_slist_length (priv->socks5_fallback_proxies);

  if (nb_proxies_found >= NB_SOCKS5_PROXIES_USED)
    {
      DEBUG ("we already have discovered enough proxies (%u)",
          nb_proxies_found);
      return;
    }

  nb_proxies_needed = NB_SOCKS5_PROXIES_USED - nb_proxies_found;
  DEBUG ("Need %u more proxies", nb_proxies_needed);

  for (i = 0; i < nb_proxies_needed &&
      priv->socks5_potential_proxies != NULL; i++)
    {
      gchar *jid;

      jid = priv->socks5_potential_proxies->data;
      send_proxy_query (self, jid, TRUE);

      g_free (jid);
      priv->socks5_potential_proxies = g_slist_delete_link (
          priv->socks5_potential_proxies, priv->socks5_potential_proxies);
    }

  if (priv->socks5_potential_proxies != NULL && priv->socks5_proxies_timer == 0)
    {
      /* More proxies are available. Set a timer so we'll query then later if
       * needed */
      priv->socks5_proxies_timer = g_timeout_add_seconds (SOCKS5_PROXY_TIMEOUT,
          socks5_proxies_timeout_cb, self);
    }
}

static GSList *
randomize_g_slist (GSList *list)
{
  guint len;
  guint i;
  GSList *new_head, *new_tail;

  len = g_slist_length (list);
  if (len <= 1)
    return list;

  i = g_random_int_range (0, len);
  if (i == 0)
    return list;

  /* Cut the list at the i th position and make it the new head of the
   * list */
  new_tail = g_slist_nth (list, i - 1);
  g_assert (new_tail != NULL);

  new_head = new_tail->next;
  g_assert (new_head != NULL);

  new_tail->next = NULL;

  return g_slist_concat (new_head, list);
}

static void
conn_status_changed_cb (GabbleConnection *conn,
                        TpConnectionStatus status,
                        TpConnectionStatusReason reason,
                        gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (user_data);
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);

  if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
      GStrv jids;
      guint i;

      /* we can't intialize socks5_potential_proxies in the constructor
       * because Connection's properties are not set yet at this point */
      g_object_get (priv->conn, "fallback-socks5-proxies", &jids, NULL);

      for (i = 0; jids != NULL && jids[i] != NULL; i++)
        {
          priv->socks5_potential_proxies = g_slist_prepend (
              priv->socks5_potential_proxies, g_strdup (jids[i]));
        }

      /* randomize the list to not always use the same proxies */
      priv->socks5_potential_proxies = randomize_g_slist (
              priv->socks5_potential_proxies);

      query_socks5_proxies (self);

      g_strfreev (jids);
    }
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

  priv->iq_socks5_cb = lm_message_handler_new (bytestream_factory_iq_socks5_cb,
      self, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
      priv->iq_socks5_cb, LM_MESSAGE_TYPE_IQ, LM_HANDLER_PRIORITY_FIRST);

  /* Track SOCKS5 proxy available on the connection */
  gabble_signal_connect_weak (priv->conn->disco, "item-found",
      G_CALLBACK (disco_item_found_cb), G_OBJECT (self));

  gabble_signal_connect_weak (priv->conn, "status-changed",
      G_CALLBACK (conn_status_changed_cb), G_OBJECT (self));

  return obj;
}

static void
gabble_bytestream_factory_dispose (GObject *object)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (object);
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  GSList *proxies, *l;

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

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->iq_socks5_cb, LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_socks5_cb);

  g_hash_table_destroy (priv->ibb_bytestreams);
  priv->ibb_bytestreams = NULL;

  g_hash_table_destroy (priv->muc_bytestreams);
  priv->muc_bytestreams = NULL;

  g_hash_table_destroy (priv->socks5_bytestreams);
  priv->socks5_bytestreams = NULL;

  g_hash_table_destroy (priv->multiple_bytestreams);
  priv->multiple_bytestreams = NULL;

  proxies = g_slist_concat (priv->socks5_proxies,
      priv->socks5_fallback_proxies);

  for (l = proxies; l != NULL; l = g_slist_next (l))
    {
      GabbleSocks5Proxy *proxy = (GabbleSocks5Proxy *) l->data;

      gabble_socks5_proxy_free (proxy);
    }
  g_slist_free (proxies);

  priv->socks5_proxies = NULL;
  priv->socks5_fallback_proxies = NULL;

  g_slist_foreach (priv->socks5_potential_proxies, (GFunc) g_free, NULL);
  g_slist_free (priv->socks5_potential_proxies);
  priv->socks5_potential_proxies = NULL;

  if (priv->socks5_proxies_timer != 0)
    g_source_remove (priv->socks5_proxies_timer);

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
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
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
  GHashTable *table = NULL;

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
      if (GABBLE_IS_BYTESTREAM_IBB (bytestream))
        table = priv->ibb_bytestreams;
      else if (GABBLE_IS_BYTESTREAM_SOCKS5 (bytestream))
        table = priv->socks5_bytestreams;
      else if (GABBLE_IS_BYTESTREAM_MULTIPLE (bytestream))
        table = priv->multiple_bytestreams;
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
                          GSList **stream_methods,
                          gboolean *multiple)
{
  LmMessageNode *iq = message->node;
  LmMessageNode *feature, *x, *si_multiple;
  NodeIter i, j;

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

  x = lm_message_node_get_child_with_namespace (feature, "x", NS_X_DATA);
  if (x == NULL)
    {
      NODE_DEBUG (message->node, "got a SI request without a X data field");
      return FALSE;
    }

  for (i = node_iter (x); i; i = node_iter_next (i))
    {
      LmMessageNode *field = node_iter_data (i);

      if (tp_strdiff (lm_message_node_get_attribute (field, "var"),
            "stream-method"))
        /* some future field, ignore it */
        continue;

      if (tp_strdiff (lm_message_node_get_attribute (field, "type"),
            "list-single"))
        {
          NODE_DEBUG (message->node, "SI request's stream-method field was "
              "not of type list-single");
          return FALSE;
        }

      /* Get the stream methods offered */
      *stream_methods = NULL;
      for (j = node_iter (field); j; j = node_iter_next (j))
        {
          LmMessageNode *stream_method = node_iter_data (j);
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

  si_multiple = lm_message_node_get_child_with_namespace (si, "si-multiple",
      NS_SI_MULTIPLE);
  if (si_multiple == NULL)
    *multiple = FALSE;
  else
    *multiple = TRUE;

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
            '@', "xmlns", NS_X_DATA,
            '@', "type", "form",
            '(', "field", "",
              '@', "var", "stream-method",
              '@', "type", "list-single",
              '(', "option", "",
                '(', "value", NS_BYTESTREAMS,
                ')',
              ')',
              '(', "option", "",
                '(', "value", NS_IBB,
                ')',
              ')',
            ')',
          ')',
        ')',
        '(', "si-multiple", "",
          '@', "xmlns", NS_SI_MULTIPLE,
        ')',
      ')', NULL);
}

static gboolean
stream_method_supported (const gchar *stream_method)
{
  /* IBB */
  if (!tp_strdiff (stream_method, NS_IBB))
    return TRUE;

  /* Sock5 */
  if (!tp_strdiff (stream_method, NS_BYTESTREAMS))
    return TRUE;

  return FALSE;
}

static GabbleBytestreamMultiple *gabble_bytestream_factory_create_multiple (
    GabbleBytestreamFactory *self, TpHandle peer_handle,
    const gchar *stream_id, const gchar *stream_init_id,
    const gchar *peer_resource, const gchar *self_jid,
    GabbleBytestreamState state);

static GabbleBytestreamIBB *gabble_bytestream_factory_create_ibb (
    GabbleBytestreamFactory *fac, TpHandle peer_handle, const gchar *stream_id,
    const gchar *stream_init_id, const gchar *peer_resource,
    GabbleBytestreamState state);

static GabbleBytestreamSocks5 *gabble_bytestream_factory_create_socks5 (
    GabbleBytestreamFactory *fac, TpHandle peer_handle, const gchar *stream_id,
    const gchar *stream_init_id, const gchar *peer_resource,
    const gchar *self_jid, GabbleBytestreamState state);

static void
si_tube_received (GabbleBytestreamFactory *self,
                  LmMessage *msg,
                  LmMessageNode *si,
                  GabbleBytestreamIface *bytestream,
                  TpHandle peer_handle,
                  TpHandle room_handle,
                  const gchar *stream_id)
{
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  /* A Tubes SI request can be:
   *  - a 1-1 new tube offer
   *  - a 1-1 tube extra bytestream offer
   *  - a muc tube extra bytestream offer
   */
  if (lm_message_node_get_child_with_namespace (si, "tube", NS_TUBES) != NULL)
    {
      /* The SI request is a tube offer */
       gabble_private_tubes_factory_handle_si_tube_request (
           priv->conn->private_tubes_factory, bytestream, peer_handle,
           stream_id, msg);
    }
  else if (lm_message_node_get_child_with_namespace (si, "stream", NS_TUBES)
      != NULL)
    {
      /* The SI request is an extra bytestream for a 1-1 tube */
      gabble_private_tubes_factory_handle_si_stream_request (
          priv->conn->private_tubes_factory, bytestream, peer_handle,
          stream_id, msg);
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
  TpHandle peer_handle = 0, room_handle;
  GabbleBytestreamIface *bytestream = NULL;
  GSList *l;
  const gchar *profile, *from, *stream_id, *stream_init_id, *mime_type;
  GSList *stream_methods = NULL;
  gboolean multiple;
  gchar *peer_resource = NULL;
  gchar *self_jid = NULL;

  if (lm_message_get_sub_type (msg) != LM_MESSAGE_SUB_TYPE_SET)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  si = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  if (si == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* after this point, the message is for us, so in all cases we either handle
   * it or send an error reply */

  if (!streaminit_parse_request (msg, si, &profile, &from, &stream_id,
        &stream_init_id, &mime_type, &stream_methods, &multiple))
    {
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, "failed to parse SI request");
      goto out;
    }

  DEBUG ("received a SI request");

  room_handle = gabble_get_room_handle_from_jid (room_repo, from);

  if (room_handle == 0)
    {
      /* jid is not a muc jid so we need contact's resource */

      if (!gabble_decode_jid (from, NULL, NULL, &peer_resource))
        {
          DEBUG ("Got an SI IQ response from a bad JID. Ignoring.");
          goto out;
        }

      if (!peer_resource)
        {
          DEBUG ("Got an SI IQ response from a JID without a resource."
              "Ignoring.");
          goto out;
        }

      peer_handle = tp_handle_ensure (contact_repo, from, NULL, NULL);

      /* we are not in a muc so our own jid is the one in the 'to' attribute */
      self_jid = g_strdup (lm_message_node_get_attribute (msg->node,
            "to"));
    }
  else
    {
      /* we are in a muc so need to get our muc jid */
      GabbleMucChannel *muc;

      peer_handle = tp_handle_ensure (contact_repo, from,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

      muc = gabble_muc_factory_find_text_channel (priv->conn->muc_factory,
          room_handle);

      if (muc == NULL)
        {
          DEBUG ("Got an IQ from a muc in which we are not. Ignoring.");
          goto out;
        }

      g_object_get (muc, "self-jid", &self_jid, NULL);
    }

  if (peer_handle == 0)
    {
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_JID_MALFORMED, NULL);
      goto out;
    }

  if (multiple)
    {
      DEBUG ("Receiver supports multi bytestreams");

      bytestream = (GabbleBytestreamIface *)
          gabble_bytestream_factory_create_multiple (self, peer_handle,
            stream_id, stream_init_id, peer_resource, self_jid,
            GABBLE_BYTESTREAM_STATE_LOCAL_PENDING);
    }

  /* check stream method */
  for (l = stream_methods; l != NULL; l = l->next)
    {
      if (multiple)
        {
          if (stream_method_supported (l->data))
            {
              DEBUG ("add %s to multi-bytestream methods",
                  (const gchar *) l->data);

              gabble_bytestream_multiple_add_stream_method (
                  GABBLE_BYTESTREAM_MULTIPLE (bytestream), l->data);
            }
          else
            {
              DEBUG ("skip unsupported stream method: %s",
                  (const gchar *) l->data);
            }
        }
      else
        {
          /* We create the stream according the stream method chosen.
           * User has to accept it */
          DEBUG ("Receiver doesn't support multi bytestreams. He chose %s",
              (const gchar *) l->data);

          bytestream = gabble_bytestream_factory_create_from_method (self,
              l->data, peer_handle, stream_id, stream_init_id, peer_resource,
              self_jid, GABBLE_BYTESTREAM_STATE_LOCAL_PENDING);
          if (bytestream != NULL)
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

  if (multiple)
    {
      /* Is there at least one stream method? */
      if (!gabble_bytestream_multiple_has_stream_method (
            GABBLE_BYTESTREAM_MULTIPLE (bytestream)))
        {
          GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_SI_NO_VALID_STREAMS, "" };
          DEBUG ("No valid stream method in the multi bytestream. Closing");

          gabble_bytestream_iface_close (bytestream, &e);
          goto out;
        }
    }

  /* Now that we have a bytestream, it's responsible for declining the IQ
   * if needed. */

  /* We inform the right factory we received a SI request */
  if (!tp_strdiff (profile, NS_TUBES))
    {
      si_tube_received (self, msg, si, bytestream, peer_handle, room_handle,
          stream_id);
    }
  else if (!tp_strdiff (profile, NS_FILE_TRANSFER))
    {
      gabble_ft_manager_handle_si_request (priv->conn->ft_manager, bytestream,
          peer_handle, stream_id, msg);
    }
  else
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_SI_BAD_PROFILE, "" };
      DEBUG ("SI profile unsupported: %s", profile);

      gabble_bytestream_iface_close (bytestream, &e);
    }

out:
  g_slist_free (stream_methods);
  g_free (peer_resource);
  g_free (self_jid);
  if (peer_handle != 0)
    tp_handle_unref (contact_repo, peer_handle);

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
      DEBUG ("unknown stream: <%s> from <%s>", bsid.stream, bsid.jid);
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_ITEM_NOT_FOUND, NULL);
    }
  else
    {
      gabble_bytestream_ibb_close_received (bytestream, msg);
    }

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
      if (is_iq)
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

static gboolean
handle_socks5_query_iq (GabbleBytestreamFactory *self,
                        LmMessage *msg)
{
  GabbleBytestreamFactoryPrivate *priv =
    GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);
  GabbleBytestreamSocks5 *bytestream;
  LmMessageNode *query_node;
  ConstBytestreamIdentifier bsid = { NULL, NULL };
  const gchar *tmp;
  NodeIter i;

  if (lm_message_get_sub_type (msg) != LM_MESSAGE_SUB_TYPE_SET)
    return FALSE;

  query_node = lm_message_node_get_child_with_namespace (msg->node,
      "query", NS_BYTESTREAMS);
  if (query_node == NULL)
    return FALSE;

  bsid.jid = lm_message_node_get_attribute (msg->node, "from");
  if (bsid.jid == NULL)
    {
      DEBUG ("got a message without a from field");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, "SOCKS5 <query> has no 'from' attribute");
      return TRUE;
    }

  bsid.stream = lm_message_node_get_attribute (query_node, "sid");
  if (bsid.stream == NULL)
    {
      DEBUG ("SOCKS5 query stanza doesn't contain stream id");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST, "SOCKS5 <query> has no stream ID");
      return TRUE;
    }

  bytestream = g_hash_table_lookup (priv->socks5_bytestreams, &bsid);
  if (bytestream == NULL)
    {
      /* We don't accept streams not previously announced using SI */
      DEBUG ("unknown stream: <%s> from <%s>", bsid.stream, bsid.jid);
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_ITEM_NOT_FOUND,
          "SOCKS5 <query> has an unknown stream ID");
      return TRUE;
    }

  tmp = lm_message_node_get_attribute (query_node, "mode");
  /* If this attribute is missing, the default value of "tcp" MUST be assumed */
  if (tmp != NULL && tp_strdiff (tmp, "tcp"))
    {
      DEBUG ("non-TCP SOCKS5 bytestreams are not supported");
      _gabble_connection_send_iq_error (priv->conn, msg,
          XMPP_ERROR_BAD_REQUEST,
          "SOCKS5 non-TCP bytestreams are not supported");
      return TRUE;
    }

  for (i = node_iter (query_node); i; i = node_iter_next (i))
    {
      LmMessageNode *child_node = node_iter_data (i);

      if (!tp_strdiff (child_node->name, "streamhost"))
        gabble_bytestream_socks5_add_streamhost (bytestream, child_node);
    }

  gabble_bytestream_socks5_connect_to_streamhost (bytestream, msg);

  return TRUE;
}

/**
 * bytestream_factory_iq_socks5_cb:
 *
 * Called by loudmouth when we get an incoming <iq>.
 * This handler is concerned with SOCKS5 iq's.
 *
 */
static LmHandlerResult
bytestream_factory_iq_socks5_cb (LmMessageHandler *handler,
                                 LmConnection *lmconn,
                                 LmMessage *msg,
                                 gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (user_data);

  if (handle_socks5_query_iq (self, msg))
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
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);

  if (priv->dispose_has_run)
    return;

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

GabbleBytestreamIface *
gabble_bytestream_factory_create_from_method (GabbleBytestreamFactory *self,
                                              const gchar *stream_method,
                                              TpHandle peer_handle,
                                              const gchar *stream_id,
                                              const gchar *stream_init_id,
                                              const gchar *peer_resource,
                                              const gchar *self_jid,
                                              GabbleBytestreamState state)
{
  GabbleBytestreamIface *bytestream = NULL;

  if (!tp_strdiff (stream_method, NS_IBB))
    {
      bytestream = GABBLE_BYTESTREAM_IFACE (
          gabble_bytestream_factory_create_ibb (self, peer_handle,
            stream_id, stream_init_id, peer_resource, state));
    }
  else if (!tp_strdiff (stream_method, NS_BYTESTREAMS))
    {
      bytestream = GABBLE_BYTESTREAM_IFACE (
          gabble_bytestream_factory_create_socks5 (self, peer_handle,
            stream_id, stream_init_id, peer_resource, self_jid, state));
    }

  return bytestream;
}

static GabbleBytestreamIBB *
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

  gabble_signal_connect_weak (ibb, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), G_OBJECT (self));

  id = bytestream_id_new (GABBLE_BYTESTREAM_IFACE (ibb));
  DEBUG ("add IBB bytestream <%s> from <%s>", id->stream, id->jid);
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

  gabble_signal_connect_weak (bytestream, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), G_OBJECT (self));

  id = bytestream_id_new (GABBLE_BYTESTREAM_IFACE (bytestream));
  DEBUG ("add muc bytestream <%s> from <%s>", id->stream, id->jid);
  g_hash_table_insert (priv->muc_bytestreams, id, bytestream);

  return bytestream;
}

static GabbleBytestreamSocks5 *
gabble_bytestream_factory_create_socks5 (GabbleBytestreamFactory *self,
                                         TpHandle peer_handle,
                                         const gchar *stream_id,
                                         const gchar *stream_init_id,
                                         const gchar *peer_resource,
                                         const gchar *self_jid,
                                         GabbleBytestreamState state)
{
  GabbleBytestreamFactoryPrivate *priv;
  GabbleBytestreamSocks5 *socks5;
  BytestreamIdentifier *id;

  g_return_val_if_fail (GABBLE_IS_BYTESTREAM_FACTORY (self), NULL);
  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  socks5 = g_object_new (GABBLE_TYPE_BYTESTREAM_SOCKS5,
      "connection", priv->conn,
      "peer-handle", peer_handle,
      "stream-id", stream_id,
      "state", state,
      "stream-init-id", stream_init_id,
      "peer-resource", peer_resource,
      "self-jid", self_jid,
      NULL);

  gabble_signal_connect_weak (socks5, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), G_OBJECT (self));

  id = bytestream_id_new (GABBLE_BYTESTREAM_IFACE (socks5));
  DEBUG ("add SOCKS5 bytestream <%s> from <%s>", id->stream, id->jid);
  g_hash_table_insert (priv->socks5_bytestreams, id, socks5);

  return socks5;
}

static GabbleBytestreamMultiple *
gabble_bytestream_factory_create_multiple (GabbleBytestreamFactory *self,
                                           TpHandle peer_handle,
                                           const gchar *stream_id,
                                           const gchar *stream_init_id,
                                           const gchar *peer_resource,
                                           const gchar *self_jid,
                                           GabbleBytestreamState state)
{
  GabbleBytestreamFactoryPrivate *priv;
  GabbleBytestreamMultiple *multiple;
  BytestreamIdentifier *id;

  g_return_val_if_fail (GABBLE_IS_BYTESTREAM_FACTORY (self), NULL);
  priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (self);

  multiple = g_object_new (GABBLE_TYPE_BYTESTREAM_MULTIPLE,
      "connection", priv->conn,
      "peer-handle", peer_handle,
      "stream-id", stream_id,
      "state", state,
      "stream-init-id", stream_init_id,
      "peer-resource", peer_resource,
      "factory", self,
      "self-jid", self_jid,
      NULL);

  gabble_signal_connect_weak (multiple, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), G_OBJECT (self));

  id = bytestream_id_new (GABBLE_BYTESTREAM_IFACE (multiple));
  DEBUG ("add multi bytestream <%s> from <%s>", id->stream, id->jid);
  g_hash_table_insert (priv->multiple_bytestreams, id, multiple);

  return multiple;
}

static GabbleBytestreamIface *
streaminit_get_multiple_bytestream (GabbleBytestreamFactory *self,
                                    LmMessage *reply_msg,
                                    LmMessageNode *si,
                                    const gchar *stream_id,
                                    TpHandle peer_handle,
                                    const gchar *peer_resource,
                                    const gchar *self_jid)
{
  /* If the other client supports si-multiple we have directly a list of
   * supported methods inside <value/> tags */
  LmMessageNode *si_multi;
  const gchar *stream_method;
  GabbleBytestreamMultiple *bytestream = NULL;
  NodeIter i;

  si_multi = lm_message_node_get_child_with_namespace (si, "si-multiple",
      NS_SI_MULTIPLE);
  if (si_multi == NULL)
    return NULL;

  bytestream = gabble_bytestream_factory_create_multiple (self, peer_handle,
      stream_id, NULL, peer_resource, self_jid,
      GABBLE_BYTESTREAM_STATE_INITIATING);

  for (i = node_iter (si_multi); i; i = node_iter_next (i))
    {
      LmMessageNode *value = node_iter_data (i);

      if (tp_strdiff (value->name, "value"))
        continue;

      stream_method = lm_message_node_get_value (value);
      if (!stream_method_supported (stream_method))
        {
          DEBUG ("got a si-multiple reply with an unsupported "
              "stream method: %s", stream_method);
          continue;
        }

      gabble_bytestream_multiple_add_stream_method (bytestream, stream_method);
    }

  return GABBLE_BYTESTREAM_IFACE (bytestream);
}

static GabbleBytestreamIface *
streaminit_get_bytestream (GabbleBytestreamFactory *self,
                           LmMessage *reply_msg,
                           LmMessageNode *si,
                           const gchar *stream_id,
                           TpHandle peer_handle,
                           const gchar *peer_resource,
                           const gchar *self_jid)
{
  LmMessageNode *feature, *x, *value;
  GabbleBytestreamIface *bytestream = NULL;
  const gchar *stream_method;
  NodeIter i;

  feature = lm_message_node_get_child_with_namespace (si, "feature",
      NS_FEATURENEG);
  if (feature == NULL)
    {
      NODE_DEBUG (reply_msg->node,
          "got a SI reply without a feature field");
      return NULL;
    }

  x = lm_message_node_get_child_with_namespace (feature, "x", NS_X_DATA);
  if (x == NULL)
    {
      NODE_DEBUG (reply_msg->node, "got a SI reply without a x field");
      return NULL;
    }

  for (i = node_iter (x); i; i = node_iter_next (i))
    {
      LmMessageNode *field = node_iter_data (i);

      if (tp_strdiff (lm_message_node_get_attribute (field, "var"),
            "stream-method"))
        /* some future field, ignore it */
        continue;

      value = lm_message_node_get_child (field, "value");
      if (value == NULL)
        {
          NODE_DEBUG (reply_msg->node, "SI reply's stream-method field "
              "doesn't contain stream-method value");
          return NULL;
        }

      stream_method = lm_message_node_get_value (value);
      bytestream = gabble_bytestream_factory_create_from_method (self,
          stream_method, peer_handle, stream_id, NULL, peer_resource, self_jid,
          GABBLE_BYTESTREAM_STATE_INITIATING);

      /* no need to parse the rest of the fields, we've found the one we
       * wanted */
      break;
    }

  return bytestream;
}

struct _streaminit_reply_cb_data
{
  gchar *stream_id;
  GabbleBytestreamFactoryNegotiateReplyFunc func;
  gpointer user_data;
  GObject *object;
  gboolean object_alive;
};

static void
negotiate_stream_object_destroy_notify_cb (gpointer _data,
                                           GObject *where_the_object_was)
{
  struct _streaminit_reply_cb_data *data =
    (struct _streaminit_reply_cb_data*) _data;

  data->object = NULL;
  data->object_alive = FALSE;
}

/* Called when we receive the reply of a SI request */
static LmHandlerResult
streaminit_reply_cb (GabbleConnection *conn,
                     LmMessage *sent_msg,
                     LmMessage *reply_msg,
                     GObject *obj,
                     gpointer user_data)
{
  GabbleBytestreamFactory *self = GABBLE_BYTESTREAM_FACTORY (obj);
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);
  struct _streaminit_reply_cb_data *data =
    (struct _streaminit_reply_cb_data*) user_data;
  GabbleBytestreamIface *bytestream = NULL;
  gchar *peer_resource = NULL;
  LmMessageNode *si;
  const gchar *from;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_ROOM);
  TpHandle peer_handle = 0;
  TpHandle room_handle;
  gboolean success = FALSE;
  gchar *self_jid = NULL;

  if (data->object != NULL)
    {
      g_object_weak_unref (data->object,
          negotiate_stream_object_destroy_notify_cb, data);
    }

  if (!data->object_alive)
    {
      DEBUG ("Object which requested the bytestream was disposed. Ignoring");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

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
  room_handle = gabble_get_room_handle_from_jid (room_repo, from);

  if (room_handle == 0)
    {
      /* jid is not a muc jid so we need contact's resource */

      if (!gabble_decode_jid (from, NULL, NULL, &peer_resource))
        {
          DEBUG ("Got an SI request with a bad JID");
          goto END;
        }

      if (peer_resource == NULL)
        {
          DEBUG ("Got an SI request from a JID without a resource; ignoring");
          goto END;
        }

      /* we are not in a muc so our own jid is the one in the 'to' attribute */
      self_jid = g_strdup (lm_message_node_get_attribute (reply_msg->node,
            "to"));
    }
  else
    {
      /* we are in a muc so need to get our muc jid */
      GabbleMucChannel *muc;

      muc = gabble_muc_factory_find_text_channel (priv->conn->muc_factory,
          room_handle);

      if (muc == NULL)
        {
          DEBUG ("Got an IQ from a muc in which we are not. Ignoring");
          goto END;
        }

      g_object_get (muc, "self-jid", &self_jid, NULL);
    }

  si = lm_message_node_get_child_with_namespace (reply_msg->node, "si",
      NS_SI);
  if (si == NULL)
    {
      NODE_DEBUG (reply_msg->node, "got a SI reply without a si field");
      goto END;
    }

  /* Try to build a multiple bytestream with fallback methods */
  bytestream = streaminit_get_multiple_bytestream (self, reply_msg, si,
      data->stream_id, peer_handle, peer_resource, self_jid);
  /* FIXME: check if there is at least one stream method */

  if (bytestream == NULL)
    /* The other client doesn't suppport si-multiple, use the normal XEP-095
     * method */
    bytestream = streaminit_get_bytestream (self, reply_msg, si,
        data->stream_id, peer_handle, peer_resource, self_jid);

  if (bytestream == NULL)
    goto END;

  DEBUG ("stream %s accepted", data->stream_id);

  /* Let's start the initiation of the stream */
  if (gabble_bytestream_iface_initiate (bytestream))
    {
      /* FIXME: we should really only "succeed" when our <open> succeeds.
       * It only really matters from the point of view of the data->func */
      success = TRUE;
    }

END:
  if (!success && bytestream != NULL)
    {
      /* Initiation failed. We remove the stream */
      remove_bytestream (self, bytestream);
      bytestream = NULL;
    }

  /* user callback */
  data->func (bytestream, (const gchar*) data->stream_id, reply_msg,
      data->object, data->user_data);

  if (peer_resource != NULL)
    g_free (peer_resource);


  if (peer_handle != 0)
    tp_handle_unref (contact_repo, peer_handle);

  g_free (self_jid);
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
 * @object: if non-NULL the handler will follow the lifetime of that object,
 * which means that if the object is destroyed the callback will not be invoked.
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
                                            GObject *object,
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
  data->object_alive = TRUE;
  data->object = object;

  if (object != NULL)
    {
      g_object_weak_ref (object, negotiate_stream_object_destroy_notify_cb,
          data);
    }

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
            '@', "xmlns", NS_X_DATA,
            '@', "type", "submit",
            '(', "field", "",
              '@', "var", "stream-method",
              '(', "value", stream_method, ')',
            ')',
          ')',
        ')',
      ')', NULL);
}

/*
 * gabble_bytestream_factory_make_multi_accept_iq
 *
 * @full_jid: the full jid of the stream initiator
 * @stream_init_id: the id of the SI request
 * @stream_methods: a list of the accepted string methods
 *
 * Create an IQ stanza accepting a stream in response to
 * a si-multiple SI request.
 *
 */
LmMessage *
gabble_bytestream_factory_make_multi_accept_iq (const gchar *full_jid,
                                                const gchar *stream_init_id,
                                                GList *stream_methods)
{
  LmMessage *msg;
  LmMessageNode *multi_node;
  GList *l;

  msg = lm_message_build (full_jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "result",
      '@', "id", stream_init_id,
      '(', "si", "",
        '@', "xmlns", NS_SI,
        '(', "si-multiple", "",
          '@', "xmlns", NS_SI_MULTIPLE,
          '*', &multi_node,
        ')',
      ')', NULL);

  for (l = stream_methods; l != NULL; l = l->next)
    {
      lm_message_node_add_child (multi_node, "value", l->data);
    }

  return msg;
}

GSList *
gabble_bytestream_factory_get_socks5_proxies (GabbleBytestreamFactory *self)
{
  GabbleBytestreamFactoryPrivate *priv = GABBLE_BYTESTREAM_FACTORY_GET_PRIVATE (
      self);

  /* randomize fallback proxies to avoid to use always the same one */
  priv->socks5_fallback_proxies = randomize_g_slist (
      priv->socks5_fallback_proxies);

  return g_slist_concat (g_slist_copy (priv->socks5_proxies),
      g_slist_copy (priv->socks5_fallback_proxies));
}
