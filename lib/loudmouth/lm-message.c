/*
 * lm-message.c - Loudmouth-Wocky compatibility layer
 * Copyright (C) 2009 Collabora Ltd.
 * @author Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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

#include "lm-message.h"

LmMessage *
lm_message_new (const gchar *to,
    LmMessageType type)
{
  return wocky_stanza_build (type, WOCKY_STANZA_SUB_TYPE_NONE,
      NULL, to, NULL);
}

LmMessage *
lm_message_new_with_sub_type (const gchar *to,
    LmMessageType type,
    LmMessageSubType sub_type)
{
  return wocky_stanza_build (type, sub_type,
      NULL, to, NULL);
}

LmMessage *
lm_message_ref (LmMessage *message)
{
  return g_object_ref (message);
}

void
lm_message_unref (LmMessage *message)
{
  g_object_unref (message);
}

LmMessageType
lm_message_get_type (LmMessage *message)
{
  WockyStanzaType type;

  wocky_stanza_get_type_info (message, &type, NULL);
  return type;
}

LmMessageSubType
lm_message_get_sub_type (LmMessage *message)
{
  WockyStanzaSubType sub_type;

  wocky_stanza_get_type_info (message, NULL, &sub_type);
  return sub_type;
}

LmMessageNode *
lm_message_get_node (LmMessage *message)
{
  return wocky_stanza_get_top_node (message);
}
