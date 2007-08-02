/*
 * request-pipeline.c - Pipeline logic for XMPP requests
 *
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

#include "request-pipeline.h"

#include <telepathy-glib/dbus.h>

#define DEBUG_FLAG GABBLE_DEBUG_PIPELINE
#include "debug.h"
#include "gabble-connection.h"

#define DEFAULT_REQUEST_TIMEOUT 20000
#define REQUEST_PIPELINE_SIZE 1

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

G_DEFINE_TYPE (GabbleRequestPipeline, gabble_request_pipeline, G_TYPE_OBJECT);

struct _GabbleRequestPipelineItem
{
  GabbleRequestPipeline *pipeline;
  LmMessage *message;
  guint timer_id;
  guint timeout;
  gboolean in_flight;

  GabbleRequestPipelineCb callback;
  gpointer user_data;
};

typedef struct _GabbleRequestPipelinePrivate GabbleRequestPipelinePrivate;
struct _GabbleRequestPipelinePrivate
{
  GabbleConnection *connection;
  GSList *pending_items;
  GSList *items_in_flight;

  gboolean dispose_has_run;
};

GQuark
gabble_request_pipeline_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-request-pipeline-error");
  return quark;
}

GQuark
gabble_request_pipeline_cache_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-request-pipeline-cache");
  return quark;
}

#define GABBLE_REQUEST_PIPELINE_GET_PRIVATE(o) \
  ((GabbleRequestPipelinePrivate *) ((o)->priv));

static void
gabble_request_pipeline_init (GabbleRequestPipeline *obj)
{
  GabbleRequestPipelinePrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      GABBLE_TYPE_REQUEST_PIPELINE, GabbleRequestPipelinePrivate);
  obj->priv = priv;
}

static void gabble_request_pipeline_set_property (GObject *object,
    guint property_id, const GValue *value, GParamSpec *pspec);
static void gabble_request_pipeline_get_property (GObject *object,
    guint property_id, GValue *value, GParamSpec *pspec);
static void gabble_request_pipeline_dispose (GObject *object);
static void gabble_request_pipeline_finalize (GObject *object);

static void
gabble_request_pipeline_class_init (GabbleRequestPipelineClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleRequestPipelinePrivate));

  object_class->get_property = gabble_request_pipeline_get_property;
  object_class->set_property = gabble_request_pipeline_set_property;

  object_class->dispose = gabble_request_pipeline_dispose;
  object_class->finalize = gabble_request_pipeline_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this request pipeline helper "
      "object.", GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static void
gabble_request_pipeline_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  GabbleRequestPipeline *chan = GABBLE_REQUEST_PIPELINE (object);
  GabbleRequestPipelinePrivate *priv =
      GABBLE_REQUEST_PIPELINE_GET_PRIVATE (chan);

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
gabble_request_pipeline_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  GabbleRequestPipeline *chan = GABBLE_REQUEST_PIPELINE (object);
  GabbleRequestPipelinePrivate *priv =
      GABBLE_REQUEST_PIPELINE_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

GabbleRequestPipeline *
gabble_request_pipeline_new (GabbleConnection *conn)
{
  GabbleRequestPipeline *self;

  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  self = GABBLE_REQUEST_PIPELINE (g_object_new (GABBLE_TYPE_REQUEST_PIPELINE,
      "connection", conn, NULL));

  return self;
}

static void
delete_item (GabbleRequestPipelineItem *item)
{
  GabbleRequestPipelinePrivate *priv;

  g_assert (GABBLE_IS_REQUEST_PIPELINE (item->pipeline));
  priv = GABBLE_REQUEST_PIPELINE_GET_PRIVATE (item->pipeline);

  DEBUG ("deleting item %p", item);

  if (item->in_flight)
    {
      priv->items_in_flight = g_slist_remove (priv->items_in_flight, item);
    }
  else
    {
      priv->pending_items = g_slist_remove (priv->pending_items, item);
    }

  if (item->timer_id)
      g_source_remove (item->timer_id);

  g_slice_free (GabbleRequestPipelineItem, item);
}

void
gabble_request_pipeline_item_cancel (GabbleRequestPipelineItem *item)
{
  GError cancelled = { GABBLE_REQUEST_PIPELINE_ERROR,
      GABBLE_REQUEST_PIPELINE_ERROR_CANCELLED,
      "Request cancelled" };
  GabbleRequestPipelinePrivate *priv =
      GABBLE_REQUEST_PIPELINE_GET_PRIVATE (item->pipeline);

  (item->callback) (priv->connection, NULL, item, &cancelled);
  delete_item (item);
}

static void
gabble_request_pipeline_dispose (GObject *object)
{
  GabbleRequestPipeline *self = GABBLE_REQUEST_PIPELINE (object);
  GabbleRequestPipelinePrivate *priv =
      GABBLE_REQUEST_PIPELINE_GET_PRIVATE (self);
  GError disconnected = { TP_ERRORS, TP_ERROR_DISCONNECTED,
      "Request failed because connection became disconnected" };
  GabbleRequestPipelineItem *item;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  DEBUG ("disposing request-pipeline");

  while (priv->items_in_flight)
    {
      item = priv->items_in_flight->data;
      (item->callback) (priv->connection, NULL, item, &disconnected);
      delete_item (item);
    }

  while (priv->pending_items)
    {
      item = priv->pending_items->data;
      (item->callback) (priv->connection, NULL, item, &disconnected);
      delete_item (item);
    }

  if (G_OBJECT_CLASS (gabble_request_pipeline_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_request_pipeline_parent_class)->dispose (object);
}

static void
gabble_request_pipeline_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_request_pipeline_parent_class)->finalize (object);
}

static LmHandlerResult
response_cb (GabbleConnection *conn,
             LmMessage *sent,
             LmMessage *reply,
             GObject *object,
             gpointer user_data)
{
  GabbleRequestPipelineItem *item = (GabbleRequestPipelineItem *) user_data;
  GabbleRequestPipeline *pipeline = item->pipeline;
  GabbleRequestPipelinePrivate *priv;

  g_assert (GABBLE_IS_REQUEST_PIPELINE (pipeline));
  priv = GABBLE_REQUEST_PIPELINE_GET_PRIVATE (pipeline);

  DEBUG ("got reply for request %p", item);

  if (NULL == g_slist_find (priv->items_in_flight, item))
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  g_assert (item->in_flight);

  priv->items_in_flight = g_slist_remove (priv->items_in_flight, item);

  item->callback (priv->connection, reply, user_data, NULL);

  delete_item (item);

  gabble_request_pipeline_go (pipeline);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
send_next_request (GabbleRequestPipeline *pipeline)
{
  GabbleRequestPipelinePrivate *priv =
      GABBLE_REQUEST_PIPELINE_GET_PRIVATE (pipeline);
  GabbleRequestPipelineItem *item;
  GError *error = NULL;

  if (priv->pending_items == NULL)
      return;

  item = priv->pending_items->data;

  DEBUG ("processing request %p", item);

  g_assert (item->in_flight == FALSE);

  priv->pending_items = g_slist_remove (priv->pending_items, item);

  if (!_gabble_connection_send_with_reply (priv->connection, item->message,
      response_cb, G_OBJECT (pipeline), item, &error))
    {
      item->callback (priv->connection, NULL, item->user_data, error);
      delete_item (item);
      send_next_request (pipeline);
    }
  else
    {
      priv->items_in_flight = g_slist_prepend (priv->items_in_flight, item);
      item->in_flight = TRUE;
    }
}

void
gabble_request_pipeline_go (GabbleRequestPipeline *pipeline)
{
  GabbleRequestPipelinePrivate *priv =
      GABBLE_REQUEST_PIPELINE_GET_PRIVATE (pipeline);

  while (priv->pending_items &&
      (g_slist_length (priv->items_in_flight) < REQUEST_PIPELINE_SIZE))
    {
      send_next_request (pipeline);
    }
}

static gboolean
timeout_cb (gpointer data)
{
  GabbleRequestPipelineItem *item = (GabbleRequestPipelineItem *) data;
  GabbleRequestPipeline *pipeline = item->pipeline;
  GabbleRequestPipelinePrivate *priv;
  GError *error = NULL;

  g_assert (GABBLE_IS_REQUEST_PIPELINE (pipeline));
  priv = GABBLE_REQUEST_PIPELINE_GET_PRIVATE (item->pipeline);

  error = g_error_new (GABBLE_REQUEST_PIPELINE_ERROR,
      GABBLE_REQUEST_PIPELINE_ERROR_TIMEOUT,
      "Request timed out");

  item->callback (priv->connection, NULL, item->user_data, error);

  item->timer_id = 0;
  delete_item (item);

  gabble_request_pipeline_go (pipeline);

  return FALSE;
}

static gboolean
delayed_run_pipeline (gpointer user_data)
{
  GabbleRequestPipeline *pipeline = (GabbleRequestPipeline *) user_data;
  gabble_request_pipeline_go (pipeline);
  return FALSE;
}

GabbleRequestPipelineItem *
gabble_request_pipeline_enqueue (GabbleRequestPipeline *pipeline,
                                 LmMessage *msg,
                                 guint timeout,
                                 GabbleRequestPipelineCb callback,
                                 gpointer user_data)
{
  GabbleRequestPipelinePrivate *priv =
      GABBLE_REQUEST_PIPELINE_GET_PRIVATE (pipeline);
  GabbleRequestPipelineItem *item = g_slice_new0 (GabbleRequestPipelineItem);

  g_return_val_if_fail (callback != NULL, NULL);

  item->pipeline = pipeline;
  item->message = msg;
  if (timeout == 0)
      timeout = DEFAULT_REQUEST_TIMEOUT;
  item->timer_id = g_timeout_add (timeout, timeout_cb, item);
  item->in_flight = FALSE;
  item->callback = callback;
  item->user_data = user_data;

  priv->pending_items = g_slist_append (priv->pending_items, item);

  DEBUG ("enqueued new request as item %p", item);

  /* if this is the first request in pipeline, run it delayed so in the
   * case of errors the callback will be called after this function returns
   */
  if (NULL == priv->items_in_flight)
      g_idle_add (delayed_run_pipeline, pipeline);

  return item;
}
