/*
 * text-mixin.h - Header for GabbleTextMixin
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

#ifndef __GABBLE_TEXT_MIXIN_H__
#define __GABBLE_TEXT_MIXIN_H__

#include "handles.h"
#include "util.h"

#define GABBLE_CHANNEL_SEND_NO_ERROR ((TpChannelTextSendError)-1)

G_BEGIN_DECLS

typedef struct _TpTextMixinClass TpTextMixinClass;
typedef struct _TpTextMixin TpTextMixin;

struct _TpTextMixinClass {
  guint lost_message_signal_id;
  guint received_signal_id;
  guint send_error_signal_id;
  guint sent_signal_id;
};

struct _TpTextMixin {
  TpHandleRepoIface *contacts_repo;
  guint recv_id;
  gboolean send_nick;
  gboolean message_lost;

  GQueue *pending;

  GArray *msg_types;
};

GType tp_text_mixin_get_type(void);

/* TYPE MACROS */
#define TP_TEXT_MIXIN_CLASS_OFFSET_QUARK (tp_text_mixin_class_get_offset_quark())
#define TP_TEXT_MIXIN_CLASS_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), TP_TEXT_MIXIN_CLASS_OFFSET_QUARK)))
#define TP_TEXT_MIXIN_CLASS(o) ((TpTextMixinClass *) tp_mixin_offset_cast (o, TP_TEXT_MIXIN_CLASS_OFFSET (o)))

#define TP_TEXT_MIXIN_OFFSET_QUARK (tp_text_mixin_get_offset_quark())
#define TP_TEXT_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), TP_TEXT_MIXIN_OFFSET_QUARK)))
#define TP_TEXT_MIXIN(o) ((TpTextMixin *) tp_mixin_offset_cast (o, TP_TEXT_MIXIN_OFFSET (o)))

GQuark tp_text_mixin_class_get_offset_quark (void);
GQuark tp_text_mixin_get_offset_quark (void);

void tp_text_mixin_class_init (GObjectClass *obj_cls, glong offset);
void tp_text_mixin_init (GObject *obj, glong offset, TpHandleRepoIface *contacts_repo, gboolean send_nick);
void tp_text_mixin_set_message_types (GObject *obj, ...);
void tp_text_mixin_finalize (GObject *obj);

gboolean tp_text_mixin_receive (GObject *obj, TpChannelTextMessageType type, TpHandle sender, time_t timestamp, const char *text);
gboolean tp_text_mixin_acknowledge_pending_messages (GObject *obj, const GArray * ids, GError **error);
gboolean tp_text_mixin_list_pending_messages (GObject *obj, gboolean clear, GPtrArray ** ret, GError **error);
gboolean gabble_text_mixin_send (GObject *obj, guint type, guint subtype, const char * recipient, const gchar * text, GabbleConnection *conn, gboolean emit_signal, GError **error);
void tp_text_mixin_emit_sent (GObject *obj, time_t timestamp, guint type, const char *text);
gboolean tp_text_mixin_get_message_types (GObject *obj, GArray **ret, GError **error);
void tp_text_mixin_clear (GObject *obj);

gboolean gabble_text_mixin_parse_incoming_message (LmMessage *message, const gchar **from, time_t *stamp, TpChannelTextMessageType *msgtype, const gchar **body, const gchar **body_offset, TpChannelTextSendError *send_error);

void _tp_text_mixin_send_error_signal (GObject *obj, TpChannelTextSendError error, time_t timestamp, TpChannelTextMessageType type, const gchar *text);

G_END_DECLS

#endif /* #ifndef __GABBLE_TEXT_MIXIN_H__ */

