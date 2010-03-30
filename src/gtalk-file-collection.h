/*
 * gtalk-file-collection.h - Header for GTalkFileCollection
 * Copyright (C) 2010 Collabora Ltd.
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

#ifndef __GTALK_FILE_COLLECTION_H__
#define __GTALK_FILE_COLLECTION_H__

#include <glib-object.h>
#include "jingle-session.h"
#include "connection.h"

typedef struct _GTalkFileCollection GTalkFileCollection;

typedef enum {
  GTALK_FILE_COLLECTION_STATE_PENDING,
  GTALK_FILE_COLLECTION_STATE_ACCEPTED,
  GTALK_FILE_COLLECTION_STATE_OPEN,
  GTALK_FILE_COLLECTION_STATE_TERMINATED,
  GTALK_FILE_COLLECTION_STATE_CONNECTION_FAILED,
  GTALK_FILE_COLLECTION_STATE_ERROR,
  GTALK_FILE_COLLECTION_STATE_COMPLETED
} GTalkFileCollectionState;

#include "ft-channel.h"

G_BEGIN_DECLS

typedef struct _GTalkFileCollectionClass GTalkFileCollectionClass;

GType gtalk_file_collection_get_type (void);

/* TYPE MACROS */
#define GTALK_TYPE_FILE_COLLECTION \
  (gtalk_file_collection_get_type ())
#define GTALK_FILE_COLLECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GTALK_TYPE_FILE_COLLECTION, \
                              GTalkFileCollection))
#define GTALK_FILE_COLLECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GTALK_TYPE_FILE_COLLECTION, \
                           GTalkFileCollectionClass))
#define GTALK_IS_FILE_COLLECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTALK_TYPE_FILE_COLLECTION))
#define GTALK_IS_FILE_COLLECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GTALK_TYPE_FILE_COLLECTION))
#define GTALK_FILE_COLLECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTALK_TYPE_FILE_COLLECTION, \
                              GTalkFileCollectionClass))

struct _GTalkFileCollectionClass {
    GObjectClass parent_class;
};

typedef struct _GTalkFileCollectionPrivate GTalkFileCollectionPrivate;

struct _GTalkFileCollection {
    GObject parent;
    GTalkFileCollectionPrivate *priv;
};

GTalkFileCollection *gtalk_file_collection_new (
    GabbleFileTransferChannel *channel, GabbleJingleFactory *jingle_factory,
    TpHandle handle, const gchar *resource);

GTalkFileCollection *gtalk_file_collection_new_from_session (
    GabbleJingleFactory *jingle_factory, GabbleJingleSession *session);

void gtalk_file_collection_add_channel (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel);

void gtalk_file_collection_initiate (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel);
void gtalk_file_collection_accept (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel);
void gtalk_file_collection_terminate (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel);
void gtalk_file_collection_completed (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel);
void gtalk_file_collection_block_reading (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel, gboolean block);
gboolean gtalk_file_collection_send_data (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel, const gchar *data, guint length);


#endif /* __GTALK_FILE_COLLECTION_H__ */

