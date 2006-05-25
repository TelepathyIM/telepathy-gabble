/*
 * gabble-muc-channel.h - Header for GabbleMucChannel
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
 */

#ifndef __GABBLE_MUC_CHANNEL_H__
#define __GABBLE_MUC_CHANNEL_H__

#include <glib-object.h>

#include "group-mixin.h"
#include "text-mixin.h"
#include "handles.h"

G_BEGIN_DECLS

typedef struct _GabbleMucChannel GabbleMucChannel;
typedef struct _GabbleMucChannelClass GabbleMucChannelClass;

struct _GabbleMucChannelClass {
    GObjectClass parent_class;

    GabbleGroupMixinClass group_class;
    GabbleTextMixinClass text_class;
};

struct _GabbleMucChannel {
    GObject parent;

    GabbleGroupMixin group;
    GabbleTextMixin text;
};

GType gabble_muc_channel_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_MUC_CHANNEL \
  (gabble_muc_channel_get_type())
#define GABBLE_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannel))
#define GABBLE_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelClass))
#define GABBLE_IS_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MUC_CHANNEL))
#define GABBLE_IS_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MUC_CHANNEL))
#define GABBLE_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelClass))

#define TP_TYPE_PROPERTY_INFO_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_INFO_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_INFO_STRUCT))

#define TP_TYPE_PROPERTY_VALUE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_VALUE, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_VALUE_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_VALUE_STRUCT))

#define TP_TYPE_PROPERTY_FLAGS_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_FLAGS_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_FLAGS_STRUCT))

void _gabble_muc_channel_presence_error (GabbleMucChannel *chan, const gchar *jid, LmMessageNode *pres_node);
void _gabble_muc_channel_member_presence_updated (GabbleMucChannel *chan, GabbleHandle handle, LmMessage *message, LmMessageNode *x_node);
gboolean _gabble_muc_channel_receive (GabbleMucChannel *chan, TpChannelTextMessageType type, GabbleHandle sender, time_t timestamp, const gchar *text, LmMessage *msg);

void _gabble_muc_channel_handle_invited (GabbleMucChannel *chan, GabbleHandle inviter, const gchar *message);

gboolean gabble_muc_channel_acknowledge_pending_message (GabbleMucChannel *obj, guint id, GError **error);
gboolean gabble_muc_channel_add_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error);
gboolean gabble_muc_channel_close (GabbleMucChannel *obj, GError **error);
gboolean gabble_muc_channel_get_channel_type (GabbleMucChannel *obj, gchar ** ret, GError **error);
gboolean gabble_muc_channel_get_group_flags (GabbleMucChannel *obj, guint* ret, GError **error);
gboolean gabble_muc_channel_get_handle (GabbleMucChannel *obj, guint* ret, guint* ret1, GError **error);
gboolean gabble_muc_channel_get_interfaces (GabbleMucChannel *obj, gchar *** ret, GError **error);
gboolean gabble_muc_channel_get_local_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error);
gboolean gabble_muc_channel_get_members (GabbleMucChannel *obj, GArray ** ret, GError **error);
gboolean gabble_muc_channel_get_message_types (GabbleMucChannel *obj, GArray ** ret, GError **error);
gboolean gabble_muc_channel_get_password_flags (GabbleMucChannel *obj, guint* ret, GError **error);
gboolean gabble_muc_channel_get_properties (GabbleMucChannel *obj, const GArray * properties, GPtrArray ** ret, GError **error);
gboolean gabble_muc_channel_get_remote_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error);
gboolean gabble_muc_channel_get_self_handle (GabbleMucChannel *obj, guint* ret, GError **error);
gboolean gabble_muc_channel_list_pending_messages (GabbleMucChannel *obj, GPtrArray ** ret, GError **error);
gboolean gabble_muc_channel_list_properties (GabbleMucChannel *obj, GPtrArray ** ret, GError **error);
gboolean gabble_muc_channel_provide_password (GabbleMucChannel *obj, const gchar * password, DBusGMethodInvocation *context);
gboolean gabble_muc_channel_remove_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error);
gboolean gabble_muc_channel_send (GabbleMucChannel *obj, guint type, const gchar * text, GError **error);
gboolean gabble_muc_channel_set_properties (GabbleMucChannel *obj, const GPtrArray * properties, DBusGMethodInvocation *context);

G_END_DECLS

#endif /* #ifndef __GABBLE_MUC_CHANNEL_H__*/
