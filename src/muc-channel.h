/*
 * gabble-muc-channel.h - Header for GabbleMucChannel
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#ifndef __GABBLE_MUC_CHANNEL_H__
#define __GABBLE_MUC_CHANNEL_H__

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/base-channel.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/message-mixin.h>

#include "types.h"
#ifdef ENABLE_VOIP
#include "call-muc-channel.h"
#endif
#include "tube-iface.h"

G_BEGIN_DECLS

typedef enum {
    MUC_STATE_CREATED = 0,
    MUC_STATE_INITIATED,
    MUC_STATE_AUTH,
    MUC_STATE_JOINED,
    MUC_STATE_ENDED,
} GabbleMucState;

typedef struct _GabbleMucChannelClass GabbleMucChannelClass;
typedef struct _GabbleMucChannelPrivate GabbleMucChannelPrivate;

struct _GabbleMucChannelClass {
    TpBaseChannelClass parent_class;

    TpGroupMixinClass group_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleMucChannel {
    TpBaseChannel parent;

    TpGroupMixin group;
    TpMessageMixin message_mixin;

    GabbleMucChannelPrivate *priv;
};

GType gabble_muc_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_MUC_CHANNEL \
  (gabble_muc_channel_get_type ())
#define GABBLE_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MUC_CHANNEL, \
                              GabbleMucChannel))
#define GABBLE_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MUC_CHANNEL,\
                           GabbleMucChannelClass))
#define GABBLE_IS_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MUC_CHANNEL))
#define GABBLE_IS_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MUC_CHANNEL))
#define GABBLE_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MUC_CHANNEL,\
                              GabbleMucChannelClass))

gboolean _gabble_muc_channel_is_ready (GabbleMucChannel *chan);

void gabble_muc_channel_set_autoclose (GabbleMucChannel *chan,
    gboolean autoclose);

gboolean gabble_muc_channel_get_autoclose (GabbleMucChannel *chan);

gboolean gabble_muc_channel_can_be_closed (GabbleMucChannel *chan);

void gabble_muc_channel_send_presence (GabbleMucChannel *chan);

gboolean gabble_muc_channel_send_invite (GabbleMucChannel *self,
    const gchar *jid, const gchar *message, gboolean continue_, GError **error);

GabbleTubeIface * gabble_muc_channel_tube_request (GabbleMucChannel *self,
    gpointer request_token,
    GHashTable *request_properties,
    gboolean require_new);

void gabble_muc_channel_foreach_tubes (GabbleMucChannel *gmuc,
    TpExportableChannelFunc foreach, gpointer user_data);

void gabble_muc_channel_handle_si_stream_request (GabbleMucChannel *self,
    GabbleBytestreamIface *bytestream, const gchar *stream_id,
    WockyStanza *msg);

#ifdef ENABLE_VOIP
GabbleCallMucChannel * gabble_muc_channel_get_call (GabbleMucChannel *gmuc);
GList * gabble_muc_channel_get_call_channels (GabbleMucChannel *self);

void gabble_muc_channel_request_call (GabbleMucChannel *gmuc,
    GHashTable *request,
    gboolean require_new,
    gpointer token,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean gabble_muc_channel_request_call_finish (GabbleMucChannel *gmuc,
    GAsyncResult *result,
    GError **error);

gboolean gabble_muc_channel_handle_jingle_session (GabbleMucChannel *channel,
    GabbleJingleSession *session);
#endif

void gabble_muc_channel_update_configuration_async (
    GabbleMucChannel *self,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data);
gboolean gabble_muc_channel_update_configuration_finish (
    GabbleMucChannel *self,
    GAsyncResult *result,
    GError **error);

void gabble_muc_channel_teardown (GabbleMucChannel *gmuc);


G_END_DECLS

#endif /* #ifndef __GABBLE_MUC_CHANNEL_H__*/
