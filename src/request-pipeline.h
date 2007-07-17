/*
 * request-pipeline.h - Pipeline logic for XMPP requests
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

#ifndef __GABBLE_REQUEST_PIPELINE_H__
#define __GABBLE_REQUEST_PIPELINE_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include "gabble-types.h"

G_BEGIN_DECLS

typedef struct _GabbleRequestPipelineClass GabbleRequestPipelineClass;
typedef struct _GabbleRequestPipelineItem GabbleRequestPipelineItem;
typedef void (*GabbleRequestPipelineCb) (GabbleConnection *conn,
    LmMessage *msg, gpointer user_data, GError *error);

/**
 * GabbleRequestPipelineError:
 * @GABBLE_REQUEST_PIPELINE_ERROR_CANCELLED: The request was cancelled
 * @GABBLE_REQUEST_PIPELINE_ERROR_TIMEOUT: The request timed out
 */
typedef enum
{
  GABBLE_REQUEST_PIPELINE_ERROR_CANCELLED,
  GABBLE_REQUEST_PIPELINE_ERROR_TIMEOUT
} GabbleRequestPipelineError;

GQuark gabble_request_pipeline_error_quark (void);
#define GABBLE_REQUEST_PIPELINE_ERROR gabble_request_pipeline_error_quark ()

GType gabble_request_pipeline_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_REQUEST_PIPELINE \
  (gabble_request_pipeline_get_type ())
#define GABBLE_REQUEST_PIPELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_REQUEST_PIPELINE, \
                               GabbleRequestPipeline))
#define GABBLE_REQUEST_PIPELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_REQUEST_PIPELINE, \
                            GabbleRequestPipelineClass))
#define GABBLE_IS_REQUEST_PIPELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_REQUEST_PIPELINE))
#define GABBLE_IS_REQUEST_PIPELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_REQUEST_PIPELINE))
#define GABBLE_REQUEST_PIPELINE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_REQUEST_PIPELINE, \
                              GabbleRequestPipelineClass))

struct _GabbleRequestPipelineClass {
    GObjectClass parent_class;
};

struct _GabbleRequestPipeline {
    GObject parent;
    gpointer priv;
};

GabbleRequestPipeline *gabble_request_pipeline_new (GabbleConnection *conn);
GabbleRequestPipelineItem *gabble_request_pipeline_enqueue
    (GabbleRequestPipeline *pipeline, LmMessage *msg, guint timeout,
     GabbleRequestPipelineCb callback, gpointer user_data);
void gabble_request_pipeline_item_cancel (GabbleRequestPipelineItem *req);
void gabble_request_pipeline_go (GabbleRequestPipeline *pipeline);

G_END_DECLS

#endif
