/*
 * util.c - Source for Gabble utility functions
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#include <glib.h>
#include <string.h>

#include "namespaces.h"
#include "gabble-connection.h"

#include "util.h"

gboolean
g_strdiff (const gchar *left, const gchar *right)
{
  if ((NULL == left) != (NULL == right))
    return TRUE;

  else if (left == right)
    return FALSE;

  else
    return (0 != strcmp (left, right));
}

static void
lm_message_node_add_nick (LmMessageNode *node, const gchar *nick)
{
  LmMessageNode *nick_node;

  nick_node = lm_message_node_add_child (node, "nick", nick);
  lm_message_node_set_attribute (nick_node, "xmlns", NS_NICK);
}

void
lm_message_node_add_own_nick (LmMessageNode *node,
                              GabbleConnection *connection)
{
  gchar *nick;
  GabbleConnectionAliasSource source;

  source = _gabble_connection_get_cached_alias (connection,
        connection->self_handle, &nick);

  if (source > GABBLE_CONNECTION_ALIAS_FROM_JID)
    lm_message_node_add_nick (node, nick);

  g_free (nick);
}

void
lm_message_node_steal_children (LmMessageNode *snatcher,
                                LmMessageNode *mum)
{
  LmMessageNode *baby;

  g_return_if_fail (snatcher->children == NULL);

  if (mum->children == NULL)
    return;

  snatcher->children = mum->children;
  mum->children = NULL;

  for (baby = snatcher->children;
       baby != NULL;
       baby = baby->next)
    baby->parent = snatcher;
}

gboolean
lm_message_node_has_namespace (LmMessageNode *node,
                               const gchar *ns,
                               const gchar *tag)
{
  gchar *attribute = NULL;
  const gchar *node_ns;
  gboolean ret;

  if (tag != NULL)
    attribute = g_strconcat ("xmlns:", tag, NULL);

  node_ns = lm_message_node_get_attribute (node,
      tag != NULL ? attribute : "xmlns");

  ret = !g_strdiff (node_ns, ns);

  g_free (attribute);

  return ret;
}

LmMessageNode *
lm_message_node_get_child_with_namespace (LmMessageNode *node,
                                          const gchar *name,
                                          const gchar *ns)
{
  LmMessageNode *tmp;

  for (tmp = node->children;
       tmp != NULL;
       tmp = tmp->next)
    {
      gchar *tag = NULL;
      gboolean found;

      if (g_strdiff (tmp->name, name))
        {
          const gchar *suffix;

          suffix = strchr (tmp->name, ':');

          if (suffix == NULL)
            continue;
          else
            suffix++;

          if (g_strdiff (suffix, name))
            continue;

          tag = g_strndup (tmp->name, suffix - tmp->name - 1);
        }

      found = lm_message_node_has_namespace (tmp, ns, tag);

      g_free (tag);

      if (found)
        return tmp;
    }

  return NULL;
}

/**
 * gabble_decode_jid
 *
 * Parses a JID which may be one of the following forms:
 *  server
 *  server/resource
 *  username@server
 *  username@server/resource
 *  room@service/nick
 * and sets the caller's username_room, server_service and resource_nick
 * pointers to the username/room, server/service and resource/nick parts
 * respectively, if available in the provided JID. The caller may set any of
 * the pointers to NULL if they are not interested in a certain component.
 *
 * The returned values may be NULL or zero-length if a component was either
 * not present or zero-length respectively in the given JID. The username/room
 * and server/service are lower-cased because the Jabber protocol treats them
 * case-insensitively.
 */
void
gabble_decode_jid (const gchar *jid,
                   gchar **username_room,
                   gchar **server_service,
                   gchar **resource_nick)
{
  char *tmp_jid, *tmp_username, *tmp_server, *tmp_resource;

  g_assert (jid != NULL);
  g_assert (*jid != '\0');

  if (username_room != NULL)
    *username_room = NULL;

  if (server_service != NULL)
    *server_service = NULL;

  if (resource_nick != NULL)
    *resource_nick = NULL;

  /* take a local copy so we don't modify the caller's string */
  tmp_jid = g_strdup (jid);

  /* find an @ in username, truncate username to that length, and point
   * 'server' to the byte afterwards */
  tmp_server = strchr (tmp_jid, '@');
  if (tmp_server)
    {
      tmp_username = tmp_jid;

      *tmp_server = '\0';
      tmp_server++;

      /* store the username if the user provided a pointer */
      if (username_room != NULL)
        *username_room = g_utf8_strdown (tmp_username, -1);
    }
  else
    {
      tmp_username = NULL;
      tmp_server = tmp_jid;
    }

  /* if we have a server, find a / in it, truncate it to that length, and point
   * 'resource' to the byte afterwards. otherwise, do the same to username to
   * find any resource there. */
  tmp_resource = strchr (tmp_server, '/');
  if (tmp_resource)
    {
      *tmp_resource = '\0';
      tmp_resource++;

      /* store the resource if the user provided a pointer */
      if (resource_nick != NULL)
        *resource_nick = g_strdup (tmp_resource);
    }

  /* the server must be stored after the resource, in case we truncated a
   * resource from it */
  if (server_service != NULL)
    *server_service = g_utf8_strdown (tmp_server, -1);

  /* free our working copy */
  g_free (tmp_jid);
}

/* extend a pointer by an offset, provided the offset is not 0 */
gpointer
gabble_mixin_offset_cast (gpointer instance,
                          guint offset)
{
  g_return_val_if_fail (offset != 0, NULL);

  return ((guchar *) instance + offset);
}

