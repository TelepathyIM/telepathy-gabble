/*
 * lm-message-node.c - Loudmouth-Wocky compatibility layer
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

#include "lm-message-node.h"

#include <wocky/wocky-utils.h>

static gboolean
copy_attribute (const gchar *key,
    const gchar *value,
    const gchar *prefix,
    const gchar *ns,
    gpointer user_data)
{
  WockyNode *copy = (WockyNode *) user_data;

  wocky_node_set_attribute_ns (copy, key, value, ns);
  return TRUE;
}

static WockyNode *
copy_node (WockyNode *node)
{
  WockyNode *copy;
  GSList *l;

  copy = wocky_node_new (node->name, wocky_node_get_ns (node));
  wocky_node_set_content (copy, node->content);
  wocky_node_set_language (copy, wocky_node_get_language (node));

  wocky_node_each_attribute (node, copy_attribute, copy);

  for (l = node->children; l != NULL; l = g_slist_next (l))
    {
      WockyNode *child = l->data;

      copy->children = g_slist_prepend (copy->children, copy_node (child));
    }
  copy->children = g_slist_reverse (copy->children);

  return copy;
}

LmMessageNode *
lm_message_node_ref (LmMessageNode *node)
{
  /* WockyNode is not ref counted. Return a copy of the node */
  return copy_node (node);
}

void
lm_message_node_unref (LmMessageNode *node)
{
  wocky_node_free (node);
}

void
lm_message_node_set_attribute (LmMessageNode *node,
    const gchar *name,
    const gchar *value)
{
  if (!wocky_strdiff (name, "xmlns"))
    node->ns = g_quark_from_string (value);
  else if (!wocky_strdiff (name, "xml:lang"))
    wocky_node_set_language (node, value);
  else
    wocky_node_set_attribute (node, name, value);
}

const gchar *
lm_message_node_get_attribute (LmMessageNode *node,
    const gchar *name)
{
  if (!wocky_strdiff (name, "xmlns"))
    return wocky_node_get_ns (node);
  else if (!wocky_strdiff (name, "xml:lang"))
    return wocky_node_get_language (node);

  return wocky_node_get_attribute (node, name);
}

void
lm_message_node_set_attributes (LmMessageNode *node,
    const gchar *name,
    ...)
{
  va_list args;

  g_return_if_fail (node != NULL);

  va_start (args, name);
  while (name != NULL)
    {
      const gchar *value;
      value = (const gchar *) va_arg (args, gchar *);

      lm_message_node_set_attribute (node, name, value);

      name = (const gchar *) va_arg (args, gchar *);
    }
  va_end (args);
}

LmMessageNode *
lm_message_node_add_child (LmMessageNode *node,
    const gchar *name,
    const gchar *value)
{
  return wocky_node_add_child_with_content (node, name, value);
}

LmMessageNode *
lm_message_node_get_child (LmMessageNode *node,
    const gchar *child_name)
{
  return wocky_node_get_child (node, child_name);
}

LmMessageNode *
lm_message_node_find_child (LmMessageNode *node,
    const gchar *child_name)
{
  WockyNode *found;
  GSList *l;

  found = wocky_node_get_child (node, child_name);
  if (found != NULL)
    return found;

  for (l = node->children; l != NULL; l = g_slist_next (l))
    {
      WockyNode *child = l->data;

      found = lm_message_node_find_child (child, child_name);
      if (found != NULL)
        return found;
    }

  return NULL;
}

const gchar *
lm_message_node_get_value (LmMessageNode *node)
{
  return node->content;
}

void
lm_message_node_set_value (LmMessageNode *node,
    const gchar *value)
{
  wocky_node_set_content (node, value);
}

gchar *
lm_message_node_to_string (LmMessageNode *node)
{
  return wocky_node_to_string (node);
}
