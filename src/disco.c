/*
 * disco.c - Source for Gabble service discovery
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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
 *
 * -- LET'S DISCO!!!  \o/ \o_ _o/ /\o/\ _/o/- -\o\_ --
 */

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_FLAG GABBLE_DEBUG_DISCO

#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "gabble-error.h"
#include "namespaces.h"
#include "telepathy-helpers.h"
#include "util.h"

#define DEFAULT_REQUEST_TIMEOUT 20000
#define DISCO_PIPELINE_SIZE 10

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

G_DEFINE_TYPE(GabbleDisco, gabble_disco, G_TYPE_OBJECT);

typedef struct _GabbleDiscoPrivate GabbleDiscoPrivate;
struct _GabbleDiscoPrivate
{
  GabbleConnection *connection;
  GSList *service_cache;
  GList *requests;
  gboolean dispose_has_run;
};

struct _GabbleDiscoRequest
{
  GabbleDisco *disco;
  guint timer_id;

  GabbleDiscoType type;
  gchar *jid;
  gchar *node;
  GabbleDiscoCb callback;
  gpointer user_data;
  GObject *bound_object;
};

GQuark
gabble_disco_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-disco-error");
  return quark;
}

#define GABBLE_DISCO_GET_PRIVATE(o)     ((GabbleDiscoPrivate*)((o)->priv));

static void
gabble_disco_init (GabbleDisco *obj)
{
  GabbleDiscoPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_DISCO, GabbleDiscoPrivate);
  obj->priv = priv;
}

static GObject *gabble_disco_constructor (GType type, guint n_props,
    GObjectConstructParam *props);
static void gabble_disco_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gabble_disco_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);
static void gabble_disco_dispose (GObject *object);
static void gabble_disco_finalize (GObject *object);

static void
gabble_disco_class_init (GabbleDiscoClass *gabble_disco_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_disco_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_disco_class, sizeof (GabbleDiscoPrivate));

  object_class->constructor = gabble_disco_constructor;

  object_class->get_property = gabble_disco_get_property;
  object_class->set_property = gabble_disco_set_property;

  object_class->dispose = gabble_disco_dispose;
  object_class->finalize = gabble_disco_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "XMPP Discovery object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
gabble_disco_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleDisco *chan = GABBLE_DISCO (object);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_disco_set_property (GObject     *object,
                           guint        property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GabbleDisco *chan = GABBLE_DISCO (object);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_disco_conn_status_changed_cb (GabbleConnection *conn,
    TpConnectionStatus status, TpConnectionStatusReason reason, gpointer data);

static GObject *
gabble_disco_constructor (GType type, guint n_props,
                          GObjectConstructParam *props)
{
  GObject *obj;
  GabbleDisco *disco;
  GabbleDiscoPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_disco_parent_class)-> constructor (type,
      n_props, props);
  disco = GABBLE_DISCO (obj);
  priv = GABBLE_DISCO_GET_PRIVATE (disco);

  g_signal_connect (priv->connection, "status-changed",
      G_CALLBACK (gabble_disco_conn_status_changed_cb), disco);

  return obj;
}

static void cancel_request (GabbleDiscoRequest *request);

void
gabble_disco_dispose (GObject *object)
{
  GabbleDisco *self = GABBLE_DISCO (object);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (self);
  GSList *l;
  DBusGProxy *bus_proxy;
  bus_proxy = tp_get_bus_proxy ();

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  DEBUG ("dispose called");

  /* cancel request removes the element from the list after cancelling */
  while (priv->requests)
    cancel_request (priv->requests->data);

  for (l = priv->service_cache; l; l = g_slist_next (l))
    {
      GabbleDiscoItem *item = (GabbleDiscoItem *) l->data;
      g_free ((char *) item->jid);
      g_free ((char *) item->name);
      g_free ((char *) item->type);
      g_free ((char *) item->category);
      g_hash_table_destroy (item->features);
      g_free (item);
    }

  g_slist_free (priv->service_cache);
  priv->service_cache = NULL;

  if (G_OBJECT_CLASS (gabble_disco_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_disco_parent_class)->dispose (object);
}

void
gabble_disco_finalize (GObject *object)
{
  DEBUG ("called with %p", object);

  G_OBJECT_CLASS (gabble_disco_parent_class)->finalize (object);
}

/**
 * gabble_disco_new:
 * @conn: The #GabbleConnection to use for service discovery
 *
 * Creates an object to use for Jabber service discovery (DISCO)
 * There should be one of these per connection
 */
GabbleDisco *
gabble_disco_new (GabbleConnection *conn)
{
  GabbleDisco *disco;

  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  disco = GABBLE_DISCO (g_object_new (GABBLE_TYPE_DISCO,
        "connection", conn,
        NULL));

  return disco;
}


static void notify_delete_request (gpointer data, GObject *obj);

static void
delete_request (GabbleDiscoRequest *request)
{
  GabbleDisco *disco = request->disco;
  GabbleDiscoPrivate *priv;

  g_assert (NULL != request);
  g_assert (GABBLE_IS_DISCO (disco));

  priv = GABBLE_DISCO_GET_PRIVATE (disco);

  g_assert (NULL != g_list_find (priv->requests, request));

  priv->requests = g_list_remove (priv->requests, request);

  if (NULL != request->bound_object)
    {
      g_object_weak_unref (request->bound_object, notify_delete_request, request);
    }

  if (0 != request->timer_id)
    {
      g_source_remove (request->timer_id);
    }

  g_free (request->jid);
  g_free (request);
}

static gboolean
timeout_request (gpointer data)
{
  GabbleDiscoRequest *request = (GabbleDiscoRequest*) data;
  GError *err;
  g_return_val_if_fail (data != NULL, FALSE);

  err = g_error_new (GABBLE_DISCO_ERROR, GABBLE_DISCO_ERROR_TIMEOUT,
      "Request for %s on %s timed out",
      (request->type == GABBLE_DISCO_TYPE_INFO)?"info":"items",
      request->jid);
  (request->callback)(request->disco, request, request->jid, request->node,
                      NULL, err, request->user_data);
  g_error_free (err);

  request->timer_id = 0;
  delete_request (request);
  return FALSE;
}

static void
cancel_request (GabbleDiscoRequest *request)
{
  GError *err;

  g_assert (request != NULL);

  err = g_error_new (GABBLE_DISCO_ERROR, GABBLE_DISCO_ERROR_CANCELLED,
      "Request for %s on %s cancelled",
      (request->type == GABBLE_DISCO_TYPE_INFO)?"info":"items",
      request->jid);
  (request->callback)(request->disco, request, request->jid, request->node,
                      NULL, err, request->user_data);
  g_error_free (err);

  delete_request (request);
}

static LmHandlerResult
request_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                  LmMessage *reply_msg, GObject *object, gpointer user_data)
{
  GabbleDiscoRequest *request = (GabbleDiscoRequest*) user_data;
  GabbleDisco *disco = GABBLE_DISCO (object);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (disco);
  LmMessageNode *query_node;
  GError *err = NULL;

  g_assert (request);

  if (!g_list_find (priv->requests, request))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  query_node = lm_message_node_get_child (reply_msg->node, "query");

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      error_node = lm_message_node_get_child (reply_msg->node, "error");
      if (error_node)
        {
          err = gabble_xmpp_error_to_g_error (
              gabble_xmpp_error_from_node (error_node));
        }

      if (err == NULL)
        {
          err = g_error_new (GABBLE_DISCO_ERROR,
                             GABBLE_DISCO_ERROR_UNKNOWN,
                             "an unknown error occurred");
        }
    }
  else if (NULL == query_node)
    {
      err = g_error_new (GABBLE_DISCO_ERROR, GABBLE_DISCO_ERROR_UNKNOWN,
          "disco response contained no <query> node");
    }

  request->callback (request->disco, request, request->jid, request->node,
                     query_node, err, request->user_data);
  delete_request (request);

  if (err)
    g_error_free (err);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
notify_delete_request (gpointer data, GObject *obj)
{
  GabbleDiscoRequest *request = (GabbleDiscoRequest *) data;
  request->bound_object = NULL;
  delete_request (request);
}

/**
 * gabble_disco_request:
 * @self: #GabbleDisco object to use for request
 * @type: type of request
 * @jid: Jabber ID to request on
 * @node: node to request on @jid, or NULL
 * @callback: #GabbleDiscoCb to call on request fullfilment
 * @object: GObject to bind request to. the callback will not be
 *          called if this object has been unrefed. NULL if not needed
 * @error: #GError to return a telepathy error in if unable to make
 *         request, NULL if unneeded.
 *
 * Make a disco request on the given jid with the default timeout.
 */
GabbleDiscoRequest *
gabble_disco_request (GabbleDisco *self, GabbleDiscoType type,
                      const gchar *jid, const char *node,
                      GabbleDiscoCb callback, gpointer user_data,
                      GObject *object, GError **error)
{
  return gabble_disco_request_with_timeout (self, type, jid, node,
                                            DEFAULT_REQUEST_TIMEOUT,
                                            callback, user_data,
                                            object, error);
}

/**
 * gabble_disco_request_with_timeout:
 * @self: #GabbleDisco object to use for request
 * @type: type of request
 * @jid: Jabber ID to request on
 * @node: node to request on @jid, or NULL
 * @timeout: the time until the request fails, in milliseconds (1/1000ths of a second)
 * @callback: #GabbleDiscoCb to call on request fullfilment
 * @object: GObject to bind request to. the callback will not be
 *          called if this object has been unrefed. NULL if not needed
 * @error: #GError to return a telepathy error in if unable to make
 *         request, NULL if unneeded.
 *
 * Make a disco request on the given jid, which will fail unless a reply
 * is received within the given timeout interval.
 */
GabbleDiscoRequest *
gabble_disco_request_with_timeout (GabbleDisco *self, GabbleDiscoType type,
                                   const gchar *jid, const char *node,
                                   guint timeout, GabbleDiscoCb callback,
                                   gpointer user_data, GObject *object,
                                   GError **error)
{
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (self);
  GabbleDiscoRequest *request;
  LmMessage *msg;
  LmMessageNode *lm_node;
  const gchar *xmlns;

  request = g_new0 (GabbleDiscoRequest, 1);
  request->disco = self;
  request->type = type;
  request->jid = g_strdup (jid);
  if (node)
    request->node = g_strdup (node);
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  DEBUG ("Creating disco request %p for %s",
           request, request->jid);

  priv->requests = g_list_prepend (priv->requests, request);
  msg = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
                                           LM_MESSAGE_SUB_TYPE_GET);
  lm_node = lm_message_node_add_child (msg->node, "query", NULL);

  switch (type) {
    case GABBLE_DISCO_TYPE_INFO:
      xmlns = NS_DISCO_INFO;
      break;
    case GABBLE_DISCO_TYPE_ITEMS:
      xmlns = NS_DISCO_ITEMS;
      break;
    default:
      g_assert_not_reached ();
      return NULL;
  }

  lm_message_node_set_attribute (lm_node, "xmlns", xmlns);

  if (node)
    {
      lm_message_node_set_attribute (lm_node, "node", node);
    }

  if (! _gabble_connection_send_with_reply (priv->connection, msg,
        request_reply_cb, G_OBJECT(self), request, error))
    {
      delete_request (request);
      lm_message_unref (msg);
      return NULL;
    }
  else
    {
      request->timer_id =
          g_timeout_add (timeout, timeout_request, request);
      lm_message_unref (msg);
      return request;
    }
}

void
gabble_disco_cancel_request (GabbleDisco *disco, GabbleDiscoRequest *request)
{
  GabbleDiscoPrivate *priv;

  g_return_if_fail (GABBLE_IS_DISCO (disco));
  g_return_if_fail (NULL != request);

  priv = GABBLE_DISCO_GET_PRIVATE (disco);

  g_return_if_fail (NULL != g_list_find (priv->requests, request));

  cancel_request (request);
}

/* Disco pipeline */


typedef struct _GabbleDiscoPipeline GabbleDiscoPipeline;
struct _GabbleDiscoPipeline {
    GabbleDisco *disco;
    gpointer user_data;
    GabbleDiscoPipelineCb callback;
    GabbleDiscoEndCb end_callback;
    GPtrArray *disco_pipeline;
    GHashTable *remaining_items;
    gboolean running;
};

static void
gabble_disco_fill_pipeline (GabbleDisco *disco, GabbleDiscoPipeline *pipeline);

static void
item_info_cb (GabbleDisco *disco,
              GabbleDiscoRequest *request,
              const gchar *jid,
              const gchar *node,
              LmMessageNode *result,
              GError *error,
              gpointer user_data)
{
  LmMessageNode *identity, *feature, *field, *value_node;
  const char *category, *type, *var, *name, *value;
  GHashTable *keys;
  GabbleDiscoItem item;

  GabbleDiscoPipeline *pipeline = (GabbleDiscoPipeline *) user_data;

  g_ptr_array_remove_fast (pipeline->disco_pipeline, request);

  if (error)
    {
      DEBUG ("got error %s", error->message);
      goto done;
    }

  identity = lm_message_node_get_child (result, "identity");
  if (NULL == identity)
    goto done;

  name = lm_message_node_get_attribute (identity, "name");
  if (NULL == name)
    goto done;

  category = lm_message_node_get_attribute (identity, "category");
  if (NULL == category)
    goto done;

  type = lm_message_node_get_attribute (identity, "type");
  if (NULL == type)
    goto done;

  DEBUG ("got item identity, jid=%s, name=%s, category=%s, type=%s",
      jid, name, category, type);

  keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (feature = result->children; feature; feature = feature->next)
    {
      if (0 == strcmp (feature->name, "feature"))
        {
          var = lm_message_node_get_attribute (feature, "var");
          if (var)
            g_hash_table_insert (keys, g_strdup (var), NULL);
        }
      else if (0 == strcmp (feature->name, "x"))
        {
          if (lm_message_node_has_namespace (feature, NS_X_DATA, NULL))
            {
              for (field = feature->children;
                   field; field = field->next)
                {
                  if (0 != strcmp (field->name, "field"))
                    continue;

                  var = lm_message_node_get_attribute (field, "var");
                  if (NULL == var)
                    continue;

                  value_node = lm_message_node_get_child (field, "value");
                  if (NULL == value_node)
                    continue;

                  value = lm_message_node_get_value (value_node);
                  if (NULL == value)
                    continue;

                  g_hash_table_insert (keys, g_strdup (var), g_strdup (value));
                }
            }
        }
    }

  item.jid = jid;
  item.name = name;
  item.category = category;
  item.type = type;
  item.features = keys;

  pipeline->callback (pipeline, &item, pipeline->user_data);
  g_hash_table_destroy (keys);

done:
  gabble_disco_fill_pipeline (disco, pipeline);

  return;
}


static gboolean
return_true (gpointer key, gpointer value, gpointer data)
{
  return TRUE;
}

static void
gabble_disco_fill_pipeline (GabbleDisco *disco, GabbleDiscoPipeline *pipeline)
{
  if (!pipeline->running)
    {
      DEBUG ("pipeline not running, not refilling");
    }
  else
    {
      /* send disco requests for the JIDs in the remaining_items hash table
       * until there are DISCO_PIPELINE_SIZE requests in progress */
      while (pipeline->disco_pipeline->len < DISCO_PIPELINE_SIZE)
        {
          gchar *jid;
          GabbleDiscoRequest *request;

          jid = (gchar *) g_hash_table_find (pipeline->remaining_items,
              return_true, NULL);
          if (NULL == jid)
            break;

          request = gabble_disco_request (disco,
              GABBLE_DISCO_TYPE_INFO, jid, NULL, item_info_cb, pipeline,
              G_OBJECT(disco), NULL);

          g_ptr_array_add (pipeline->disco_pipeline, request);

          /* frees jid */
          g_hash_table_remove (pipeline->remaining_items, jid);
        }

      if (0 == pipeline->disco_pipeline->len)
        {
          /* signal that the pipeline has finished */
          pipeline->running = FALSE;
          pipeline->end_callback (pipeline, pipeline->user_data);
        }
    }
}


static void
disco_items_cb (GabbleDisco *disco,
          GabbleDiscoRequest *request,
          const gchar *jid,
          const gchar *node,
          LmMessageNode *result,
          GError *error,
          gpointer user_data)
{
  LmMessageNode *iter;
  const char *item_jid;
  gpointer key, value;
  GabbleDiscoPipeline *pipeline = (GabbleDiscoPipeline *) user_data;

  if (error)
    {
      DEBUG ("Got error on items request: %s", error->message);
      goto out;
    }

  iter = result->children;

  for (; iter; iter = iter->next)
    {
      if (0 != strcmp (iter->name, "item"))
        continue;

      item_jid = lm_message_node_get_attribute (iter, "jid");

      if (NULL != item_jid &&
          !g_hash_table_lookup_extended (pipeline->remaining_items, item_jid, &key, &value))
        {
          gchar *tmp = g_strdup (item_jid);
          DEBUG ("discovered service item: %s", tmp);
          g_hash_table_insert (pipeline->remaining_items, tmp, tmp);
        }
    }

out:
  gabble_disco_fill_pipeline (disco, pipeline);
}

/**
 * gabble_disco_pipeline_init:
 * @disco: disco object to use in the pipeline
 * @callback: GFunc to call on request fullfilment
 * @user_data: the usual
 *
 * Prepares the pipeline for making the ITEM request on the server and
 * subsequent INFO elements on returned items.
 *
 * GabbleDiscoPipeline is opaque structure for the user.
 */
gpointer gabble_disco_pipeline_init (GabbleDisco *disco,
                                     GabbleDiscoPipelineCb callback,
                                     GabbleDiscoEndCb end_callback,
                                     gpointer user_data)
{
  GabbleDiscoPipeline *pipeline = g_new (GabbleDiscoPipeline, 1);
  pipeline->user_data = user_data;
  pipeline->callback = callback;
  pipeline->end_callback = end_callback;
  pipeline->disco_pipeline = g_ptr_array_sized_new (DISCO_PIPELINE_SIZE);
  pipeline->remaining_items = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
  pipeline->running = TRUE;
  pipeline->disco = disco;

  return pipeline;
}

/**
 * gabble_disco_pipeline_run:
 * @self: reference to the pipeline structure
 * @server: server to query
 *
 * Makes ITEMS request on the server, and afterwards queries for INFO
 * on each item. INFO queries are pipelined. The item properties are stored
 * in hash table parameter to the callback function. The user is responsible
 * for destroying the hash table after it's done with.
 *
 * Upon returning all the results, the end_callback is called with
 * reference to the pipeline.
 */
void
gabble_disco_pipeline_run (gpointer self, const char *server)
{
  GabbleDiscoPipeline *pipeline = (GabbleDiscoPipeline *) self;

  pipeline->running = TRUE;

  gabble_disco_request (pipeline->disco, GABBLE_DISCO_TYPE_ITEMS, server, NULL,
      disco_items_cb, pipeline, G_OBJECT (pipeline->disco), NULL);
}


/**
 * gabble_disco_pipeline_cancel:
 * @pipeline: pipeline to cancel
 *
 * Flushes the pipeline (cancels all pending disco requests) and
 * destroys it.
 */
void
gabble_disco_pipeline_destroy (gpointer self)
{
  GabbleDiscoPipeline *pipeline = (GabbleDiscoPipeline *) self;

  pipeline->running = FALSE;

  /* iterate using a while loop otherwise we're modifying
   * the array as we iterate it, and miss things! */
  while (pipeline->disco_pipeline->len > 0)
    {
      GabbleDiscoRequest *request =
        g_ptr_array_index (pipeline->disco_pipeline, 0);
      gabble_disco_cancel_request (pipeline->disco, request);
    }

  g_hash_table_destroy (pipeline->remaining_items);
  g_ptr_array_free (pipeline->disco_pipeline, TRUE);
  g_free (pipeline);
}


static void
service_feature_copy_one (gpointer k, gpointer v, gpointer user_data)
{
  char *key = (char *) k;
  char *value = (char *) v;

  GHashTable *target = (GHashTable *) user_data;
  g_hash_table_insert (target, g_strdup (key), g_strdup (value));
}

/* Service discovery */
static void
services_cb (gpointer pipeline, GabbleDiscoItem *item, gpointer user_data)
{
  GabbleDisco *disco = GABBLE_DISCO (user_data);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (disco);
  GabbleDiscoItem *my_item = g_new0 (GabbleDiscoItem, 1);

  my_item->jid = g_strdup (item->jid);
  my_item->name = g_strdup (item->name);
  my_item->type = g_strdup (item->type);
  my_item->category = g_strdup (item->category);

  my_item->features = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_hash_table_foreach  (item->features, service_feature_copy_one, my_item->features);

  priv->service_cache = g_slist_prepend (priv->service_cache, my_item);
}

static void
end_cb (gpointer pipeline, gpointer user_data)
{
  GabbleDisco *disco = GABBLE_DISCO (user_data);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (disco);

  gabble_disco_pipeline_destroy (pipeline);
  priv->service_cache = g_slist_reverse (priv->service_cache);

  /* FIXME - service discovery done - signal that somehow */
}

static void
gabble_disco_conn_status_changed_cb (GabbleConnection *conn,
                                     TpConnectionStatus status,
                                     TpConnectionStatusReason reason,
                                     gpointer data)
{
  GabbleDisco *disco = GABBLE_DISCO (data);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (disco);

  if (status == TP_CONN_STATUS_CONNECTED)
    {
      char *server;

      g_object_get (priv->connection, "stream-server", &server, NULL);

      g_assert (server != NULL);

      DEBUG ("connected, initiating service discovery on %s", server);
      gpointer pipeline = gabble_disco_pipeline_init (disco, services_cb,
          end_cb, disco);
      gabble_disco_pipeline_run (pipeline, server);

      g_free (server);
    }
}

const GabbleDiscoItem *
gabble_disco_service_find (GabbleDisco *disco,
                           const char *type,
                           const char *category,
                           const char *feature)
{
  GabbleDiscoPrivate *priv;
  GSList *l;

  g_assert (GABBLE_IS_DISCO (disco));
  priv = GABBLE_DISCO_GET_PRIVATE (disco);

  for (l = priv->service_cache; l; l = g_slist_next (l))
    {
      GabbleDiscoItem *item = (GabbleDiscoItem *) l->data;
      gboolean selected = TRUE;

      if (type != NULL && g_strdiff (type, item->type))
        selected = FALSE;

      if (category != NULL && g_strdiff (category, item->category))
        selected = FALSE;

      if (feature != NULL)
        {
          gpointer k, v;
          if (!g_hash_table_lookup_extended (item->features, feature, &k, &v))
            selected = FALSE;
        }

      if (selected)
        return item;
    }

  return NULL;
}
