/*
 * call-muc-channel.h - Header for GabbleCallMucChannel
 * Copyright (C) 2010 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#ifndef __GABBLE_CALL_MUC_CHANNEL_H__
#define __GABBLE_CALL_MUC_CHANNEL_H__

#include <glib-object.h>

#include "base-call-channel.h"
#include "jingle-session.h"

G_BEGIN_DECLS

typedef struct _GabbleCallMucChannel GabbleCallMucChannel;
typedef struct _GabbleCallMucChannelPrivate GabbleCallMucChannelPrivate;
typedef struct _GabbleCallMucChannelClass GabbleCallMucChannelClass;

struct _GabbleCallMucChannelClass {
    GabbleBaseCallChannelClass parent_class;
};

struct _GabbleCallMucChannel {
    GabbleBaseCallChannel parent;

    GabbleCallMucChannelPrivate *priv;
};

GType gabble_call_muc_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_MUC_CHANNEL \
  (gabble_call_muc_channel_get_type ())
#define GABBLE_CALL_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CALL_MUC_CHANNEL, \
    GabbleCallMucChannel))
#define GABBLE_CALL_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CALL_MUC_CHANNEL, \
    GabbleCallMucChannelClass))
#define GABBLE_IS_CALL_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_MUC_CHANNEL))
#define GABBLE_IS_CALL_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_MUC_CHANNEL))
#define GABBLE_CALL_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CALL_MUC_CHANNEL, \
    GabbleCallMucChannelClass))

void gabble_call_muc_channel_new_async (GabbleConnection *connection,
    const gchar *object_path,
    GabbleMucChannel *muc,
    TpHandle target,
    GHashTable *request,
    GAsyncReadyCallback callback,
    gpointer user_data);

GabbleCallMucChannel * gabble_call_muc_channel_new_finish (GObject *source,
    GAsyncResult *result,
    GError **error);

void gabble_call_muc_channel_incoming_session (GabbleCallMucChannel *self,
    GabbleJingleSession *session);

G_END_DECLS

#endif /* #ifndef __GABBLE_CALL_MUC_CHANNEL_H__*/
