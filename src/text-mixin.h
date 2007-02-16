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

#include <telepathy-glib/text-mixin.h>
#include "handles.h"
#include "util.h"

G_BEGIN_DECLS

void gabble_text_mixin_send (GObject *obj, guint type, guint subtype,
    const char *recipient, const gchar *text, GabbleConnection *conn,
    gboolean emit_signal, DBusGMethodInvocation *context);

void gabble_text_mixin_set_chat_state (GObject *obj, guint state, guint subtype,
    const char *recipient, GabbleConnection *conn,
    DBusGMethodInvocation *context);

gboolean gabble_text_mixin_parse_incoming_message (LmMessage *message, const gchar **from, time_t *stamp, TpChannelTextMessageType *msgtype, const gchar **body, const gchar **body_offset, gint *state, TpChannelTextSendError *send_error);

G_END_DECLS

#endif /* #ifndef __GABBLE_TEXT_MIXIN_H__ */

