/*
 * lm-message-node.h - Loudmouth-Wocky compatibility layer
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

#ifndef __LM_MESSAGE_NODE_H__
#define __LM_MESSAGE_NODE_H__

#include <wocky/wocky-node.h>

G_BEGIN_DECLS

typedef WockyNode LmMessageNode;

LmMessageNode * lm_message_node_ref (LmMessageNode *node)
  G_GNUC_WARN_UNUSED_RESULT;
void lm_message_node_unref (LmMessageNode *node);

void lm_message_node_set_attribute (LmMessageNode *node,
    const gchar *name,
    const gchar *value);

const gchar * lm_message_node_get_attribute (LmMessageNode *node,
    const gchar *name);

void lm_message_node_set_attributes (LmMessageNode *node,
    const gchar *name,
    ...);

LmMessageNode * lm_message_node_add_child (LmMessageNode *node,
    const gchar *name,
    const gchar *value);

LmMessageNode * lm_message_node_get_child (LmMessageNode *node,
    const gchar *child_name);

LmMessageNode * lm_message_node_find_child (LmMessageNode *node,
    const gchar *child_name);

const gchar * lm_message_node_get_value (LmMessageNode *node);

void lm_message_node_set_value (LmMessageNode *node,
    const gchar *value);

gchar * lm_message_node_to_string (LmMessageNode *node);

#endif /* #ifndef __LM_MESSAGE_NODE_H__ */
