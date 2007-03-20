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

#include "config.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "sha1/sha1.h"
#include "namespaces.h"
#include "gabble-connection.h"

#include "util.h"

gchar *
sha1_hex (const gchar *bytes, guint len)
{
  SHA1Context sc;
  uint8_t hash[SHA1_HASH_SIZE];
  gchar *hex_hash = g_malloc (SHA1_HASH_SIZE*2 + 1);
  int i;

  SHA1Init (&sc);
  SHA1Update (&sc, bytes, len);
  SHA1Final (&sc, hash);

  for (i = 0; i < SHA1_HASH_SIZE; i++)
    {
      sprintf (hex_hash + 2 * i, "%02x", (unsigned int) hash[i]);
    }

  return hex_hash;
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
  TpBaseConnection *base = (TpBaseConnection *)connection;

  source = _gabble_connection_get_cached_alias (connection,
        base->self_handle, &nick);

  if (source > GABBLE_CONNECTION_ALIAS_FROM_JID)
    lm_message_node_add_nick (node, nick);

  g_free (nick);
}

void
lm_message_node_unlink (LmMessageNode *orphan)
{
  if (orphan->parent && orphan == orphan->parent->children)
    orphan->parent->children = orphan->next;
  if (orphan->prev)
    orphan->prev->next = orphan->next;
  if (orphan->next)
    orphan->next->prev = orphan->prev;
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

  ret = !tp_strdiff (node_ns, ns);

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

      if (tp_strdiff (tmp->name, name))
        {
          const gchar *suffix;

          suffix = strchr (tmp->name, ':');

          if (suffix == NULL)
            continue;
          else
            suffix++;

          if (tp_strdiff (suffix, name))
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

/* note: these are only used internally for readability, not part of the API
 */
enum {
    BUILD_END = '\0',
    BUILD_ATTRIBUTE = '@',
    BUILD_CHILD = '(',
    BUILD_CHILD_END = ')',
    BUILD_POINTER = '*',
};

/**
 * lm_message_build:
 *
 * Build an LmMessageNode from a list of arguments employing an
 * S-expression-like notation. Example:
 *
 * lm_message_build ("bob@jabber.org", LM_MESSAGE_TYPE_IQ,
 *   '(', 'query', 'lala',
 *      '@', 'xmlns', 'http://jabber.org/protocol/foo',
 *   ')',
 *   NULL);
 *
 * --> <iq to="bob@jabber.org">
 *        <query xmlns="http://jabber.org/protocol/foo">lala</query>
 *     </iq>
 */
G_GNUC_NULL_TERMINATED
LmMessage *
lm_message_build (const gchar *to, LmMessageType type, ...)
{
  LmMessage *msg;
  va_list ap;
  GSList *stack = NULL;

  msg = lm_message_new (to, type);
  stack = g_slist_prepend (stack, msg->node);

  va_start (ap, type);

  for (;;)
    {
      guint arg;

      /* Note that we pull out an int-sized value here, whereas our sentinel,
       * NULL, is pointer-sized. However, sizeof (void *) should always be >=
       * sizeof (uint), so this shouldn't cause a problem.
       */
      arg = va_arg (ap, guint);

      switch (arg)
        {
        case '\0':
          goto END;

        case BUILD_ATTRIBUTE:
          {
            gchar *key = va_arg (ap, gchar *);
            gchar *value = va_arg (ap, gchar *);

            lm_message_node_set_attribute (stack->data, key, value);
          }
          break;

        case BUILD_CHILD:
          {
            gchar *name = va_arg (ap, gchar *);
            gchar *value = va_arg (ap, gchar *);
            LmMessageNode *child;

            child = lm_message_node_add_child (stack->data, name, value);
            stack = g_slist_prepend (stack, child);
          }
          break;

        case BUILD_CHILD_END:
          {
            GSList *tmp;

            tmp = stack;
            stack = stack->next;
            tmp->next = NULL;
            g_slist_free (tmp);
          }
          break;

        case BUILD_POINTER:
          {
            LmMessageNode **node = va_arg (ap, LmMessageNode **);

            *node = stack->data;
          }
          break;

        default:
          g_assert_not_reached ();
        }
    }

  va_end (ap);

END:
  g_slist_free (stack);

  return msg;
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
