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

#include <glib-object.h>

#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/properties-mixin.h>
#include "text-mixin.h"
#include "handles.h"

G_BEGIN_DECLS

typedef struct _GabbleMucChannel GabbleMucChannel;
typedef struct _GabbleMucChannelClass GabbleMucChannelClass;

struct _GabbleMucChannelClass {
    GObjectClass parent_class;

    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
    TpTextMixinClass text_class;
};

struct _GabbleMucChannel {
    GObject parent;

    TpGroupMixin group;
    TpPropertiesMixin properties;
    TpTextMixin text;

    gpointer priv;
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

gboolean _gabble_muc_channel_is_ready (GabbleMucChannel *chan);
void _gabble_muc_channel_presence_error (GabbleMucChannel *chan, const gchar *jid, LmMessageNode *pres_node);
void _gabble_muc_channel_member_presence_updated (GabbleMucChannel *chan, TpHandle handle, LmMessage *message, LmMessageNode *x_node);
gboolean _gabble_muc_channel_receive (GabbleMucChannel *chan, TpChannelTextMessageType msg_type, TpHandleType handle_type, TpHandle sender, time_t timestamp, const gchar *text, LmMessage *msg);

void _gabble_muc_channel_handle_invited (GabbleMucChannel *chan, TpHandle inviter, const gchar *message);

gboolean
gabble_muc_channel_acknowledge_pending_messages (GabbleMucChannel *self,
                                                 const GArray *ids,
                                                 GError **error);

gboolean
gabble_muc_channel_add_members (GabbleMucChannel *self,
                                const GArray *contacts,
                                const gchar *message,
                                GError **error);

gboolean
gabble_muc_channel_close (GabbleMucChannel *self,
                          GError **error);

gboolean
gabble_muc_channel_get_all_members (GabbleMucChannel *self,
                                    GArray **ret,
                                    GArray **ret1,
                                    GArray **ret2,
                                    GError **error);

gboolean
gabble_muc_channel_get_channel_type (GabbleMucChannel *self,
                                     gchar **ret,
                                     GError **error);

gboolean
gabble_muc_channel_get_group_flags (GabbleMucChannel *self,
                                    guint *ret,
                                    GError **error);

gboolean
gabble_muc_channel_get_handle (GabbleMucChannel *self,
                               guint *ret,
                               guint *ret1,
                               GError **error);

gboolean
gabble_muc_channel_get_handle_owners (GabbleMucChannel *self,
                                      const GArray *handles,
                                      GArray **ret,
                                      GError **error);

gboolean
gabble_muc_channel_get_interfaces (GabbleMucChannel *self,
                                   gchar ***ret,
                                   GError **error);

gboolean
gabble_muc_channel_get_local_pending_members (GabbleMucChannel *self,
                                              GArray **ret,
                                              GError **error);

gboolean
gabble_muc_channel_get_members (GabbleMucChannel *self,
                                GArray **ret,
                                GError **error);

gboolean
gabble_muc_channel_get_message_types (GabbleMucChannel *self,
                                      GArray **ret,
                                      GError **error);

gboolean
gabble_muc_channel_get_password_flags (GabbleMucChannel *self,
                                       guint *ret,
                                       GError **error);

gboolean
gabble_muc_channel_get_properties (GabbleMucChannel *self,
                                   const GArray *properties,
                                   GPtrArray **ret,
                                   GError **error);

gboolean
gabble_muc_channel_get_remote_pending_members (GabbleMucChannel *self,
                                               GArray **ret,
                                               GError **error);

gboolean
gabble_muc_channel_get_self_handle (GabbleMucChannel *self,
                                    guint *ret,
                                    GError **error);

gboolean
gabble_muc_channel_list_pending_messages (GabbleMucChannel *self,
                                          gboolean clear,
                                          GPtrArray **ret,
                                          GError **error);

gboolean
gabble_muc_channel_list_properties (GabbleMucChannel *self,
                                    GPtrArray **ret,
                                    GError **error);

void
gabble_muc_channel_provide_password (GabbleMucChannel *self,
                                     const gchar *password,
                                     DBusGMethodInvocation *context);

gboolean
gabble_muc_channel_remove_members (GabbleMucChannel *self,
                                   const GArray *contacts,
                                   const gchar *message,
                                   GError **error);

gboolean
gabble_muc_channel_send (GabbleMucChannel *self,
                         guint type,
                         const gchar *text,
                         GError **error);

void
gabble_muc_channel_set_properties (GabbleMucChannel *self,
                                   const GPtrArray *properties,
                                   DBusGMethodInvocation *context);


G_END_DECLS

#endif /* #ifndef __GABBLE_MUC_CHANNEL_H__*/
