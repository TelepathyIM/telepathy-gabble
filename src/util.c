/*
 * util.c - Source for Gabble utility functions
 * Copyright (C) 2006-2007 Collabora Ltd.
 * Copyright (C) 2006-2007 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
 *   @author Simon McVittie <simon.mcvittie@collabora.co.uk>
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

#include "util.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <telepathy-glib/handle-repo-dynamic.h>

#include "conn-aliasing.h"
#include "sha1/sha1.h"
#include "namespaces.h"
#include "gabble-connection.h"
#include "base64.h"

#define DEBUG_FLAG GABBLE_DEBUG_JID
#include "debug.h"

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

void
sha1_bin (const gchar *bytes,
          guint len,
          guchar out[SHA1_HASH_SIZE])
{
  SHA1Context sc;

  SHA1Init (&sc);
  SHA1Update (&sc, bytes, len);
  SHA1Final (&sc, (uint8_t *) out);
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

static LmMessageNode *
message_node_last_child (LmMessageNode *node)
{
  LmMessageNode *l;

  g_return_val_if_fail (node != NULL, NULL);

  if (!node->children)
    {
      return NULL;
    }

  l = node->children;

  while (l->next)
    {
      l = l->next;
    }

  return l;
}

void
lm_message_node_add_child_node (LmMessageNode *node, LmMessageNode *child)
{
  LmMessageNode *prev;

  g_return_if_fail (node != NULL);

  prev = message_node_last_child (node);
  lm_message_node_ref (child);

  if (prev)
    {
      prev->next = child;
      child->prev = prev;
    }
  else
    {
      node->children = child;
    }

  child->parent = node;
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

/* lm_message_node_add_build_va
 *
 * Used to implement lm_message_build and lm_message_build_with_sub_type.
 */
static void
lm_message_node_add_build_va (LmMessageNode *node, guint spec, va_list ap)
{
  GSList *stack = NULL;
  guint arg = spec;

  stack = g_slist_prepend (stack, node);

  while (arg != BUILD_END)
    {
      switch (arg)
        {
        case BUILD_END:
          goto END;

        case BUILD_ATTRIBUTE:
          {
            gchar *key = va_arg (ap, gchar *);
            gchar *value = va_arg (ap, gchar *);

            g_return_if_fail (key != NULL);
            g_return_if_fail (value != NULL);
            lm_message_node_set_attribute (stack->data, key, value);
          }
          break;

        case BUILD_CHILD:
          {
            gchar *name = va_arg (ap, gchar *);
            gchar *value = va_arg (ap, gchar *);
            LmMessageNode *child;

            g_return_if_fail (name != NULL);
            g_return_if_fail (value != NULL);
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

            g_return_if_fail (node != NULL);
            *node = stack->data;
          }
          break;

        default:
          g_assert_not_reached ();
        }

      /* Note that we pull out an int-sized value here, whereas our sentinel,
       * NULL, is pointer-sized. However, sizeof (void *) should always be >=
       * sizeof (uint), so this shouldn't cause a problem.
       */
      arg = va_arg (ap, guint);
    }

END:
  g_slist_free (stack);
}

/**
 * lm_message_build:
 *
 * Build an LmMessage from a list of arguments employing an S-expression-like
 * notation. Example:
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
lm_message_build (const gchar *to, LmMessageType type, guint spec, ...)
{
  LmMessage *msg;
  va_list ap;

  msg = lm_message_new (to, type);
  va_start (ap, spec);
  lm_message_node_add_build_va (msg->node, spec, ap);
  va_end (ap);
  return msg;
}

/**
 * lm_message_build_with_sub_type:
 *
 * As lm_message_build (), but creates a message with an LmMessageSubType.
 */
G_GNUC_NULL_TERMINATED
LmMessage *
lm_message_build_with_sub_type (const gchar *to, LmMessageType type,
    LmMessageSubType sub_type, guint spec, ...)
{
  LmMessage *msg;
  va_list ap;

  msg = lm_message_new_with_sub_type (to, type, sub_type);
  va_start (ap, spec);
  lm_message_node_add_build_va (msg->node, spec, ap);
  va_end (ap);
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

/**
 * gabble_get_room_handle_from_jid:
 * @room_repo: The %TP_HANDLE_TYPE_ROOM handle repository
 * @jid: A JID
 *
 * Given a JID seen in the from="" attribute on a stanza, work out whether
 * it's something to do with a MUC, and if so, return its handle.
 *
 * Returns: The handle of the MUC, if the JID refers to either a MUC
 *    we're in, or a contact's channel-specific JID inside a MUC.
 *    Returns 0 if the JID is either invalid, or nothing to do with a
 *    known MUC (typically this will mean it's the global JID of a contact).
 */
TpHandle
gabble_get_room_handle_from_jid (TpHandleRepoIface *room_repo,
                                 const gchar *jid)
{
  TpHandle handle;
  gchar *room;

  room = gabble_remove_resource (jid);
  if (room == NULL)
    return 0;

  handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  g_free (room);
  return handle;
}

#define INVALID_ARGUMENT(e, f, ...) \
  G_STMT_START { \
  DEBUG (f, ##__VA_ARGS__); \
  g_set_error (e, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, f, ##__VA_ARGS__);\
  } G_STMT_END

gchar *
gabble_normalize_room (TpHandleRepoIface *repo,
                       const gchar *jid,
                       gpointer context,
                       GError **error)
{
  char *at = strchr (jid, '@');
  char *slash = strchr (jid, '/');

  /* there'd better be an @ somewhere after the first character */
  if (at == NULL)
    {
      INVALID_ARGUMENT (error,
          "invalid room JID %s: does not contain '@'", jid);
      return NULL;
    }
  if (at == jid)
    {
      INVALID_ARGUMENT (error,
          "invalid room JID %s: room name before '@' may not be empty", jid);
      return NULL;
    }

  /* room names can't contain the nick part */
  if (slash != NULL)
    {
      INVALID_ARGUMENT (error,
          "invalid room JID %s: contains nickname part after '/' too", jid);
      return NULL;
    }

  /* the room and service parts are both case-insensitive, so lowercase
   * them both; gabble_decode_jid is overkill here
   */
  return g_utf8_strdown (jid, -1);
}

gchar *
gabble_remove_resource (const gchar *jid)
{
  char *slash = strchr (jid, '/');
  gchar *buf;

  if (slash == NULL)
    return g_strdup (jid);

  /* The user and domain parts can't contain '/', assuming it's valid */
  buf = g_malloc (slash - jid + 1);
  strncpy (buf, jid, slash - jid);
  buf[slash - jid] = '\0';

  return buf;
}

gchar *
gabble_normalize_contact (TpHandleRepoIface *repo,
                          const gchar *jid,
                          gpointer context,
                          GError **error)
{
  guint mode = GPOINTER_TO_UINT (context);
  gchar *username = NULL, *server = NULL, *resource = NULL;
  gchar *ret = NULL;

  gabble_decode_jid (jid, &username, &server, &resource);

  if (!username || !server || !username[0] || !server[0])
    {
      INVALID_ARGUMENT (error,
          "jid %s has invalid username or server", jid);
      goto OUT;
    }

  if (mode == GABBLE_JID_ROOM_MEMBER && resource == NULL)
    {
      INVALID_ARGUMENT (error,
          "jid %s can't be a room member - it has no resource", jid);
      goto OUT;
    }

  if (mode != GABBLE_JID_GLOBAL && resource != NULL)
    {
      ret = g_strdup_printf ("%s@%s/%s", username, server, resource);

      if (mode == GABBLE_JID_ROOM_MEMBER
          || (repo != NULL
              && tp_dynamic_handle_repo_lookup_exact (repo, ret)))
        {
          /* either we know from context that it's a room member, or we
           * already saw that contact in a room. Use ret as our answer
           */
          goto OUT;
        }
      else
        {
          g_free (ret);
        }
    }

  /* if we get here, we suspect it's a global JID, either because the context
   * says it is, or because the context isn't sure and we haven't seen it in
   * use as a room member
   */
  ret = g_strdup_printf ("%s@%s", username, server);

OUT:
  g_free (username);
  g_free (server);
  g_free (resource);
  return ret;
}

/**
 * lm_message_node_extract_properties
 *
 * Map a XML node to a properties hash table
 * (used to parse a subset of the OLPC and tubes protocol)
 *
 * Example:
 *
 * <node>
 *   <prop name="prop1" type="str">prop1_value</prop>
 *   <prop name="prop2" type="uint">7</prop>
 * </node>
 *
 * lm_message_node_extract_properties (node, "prop");
 *
 * --> { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * Returns a hash table mapping names to GValue of the specified type.
 * Valid types are: str, int, uint, bytes.
 *
 */
GHashTable *
lm_message_node_extract_properties (LmMessageNode *node,
                                    const gchar *prop)
{
  GHashTable *properties;
  LmMessageNode *child;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  if (node == NULL)
    return properties;

  for (child = node->children; child; child = child->next)
    {
      const gchar *name;
      const gchar *type;
      const gchar *value;
      GValue *gvalue;

      if (0 != strcmp (child->name, prop))
        continue;

      name = lm_message_node_get_attribute (child, "name");

      if (!name)
        continue;

      type = lm_message_node_get_attribute (child, "type");
      value = lm_message_node_get_value (child);

      if (type == NULL || value == NULL)
        continue;

      if (0 == strcmp (type, "bytes"))
        {
          GArray *arr;
          GString *decoded;

          decoded = base64_decode (value);
          if (!decoded)
            continue;

          arr = g_array_new (FALSE, FALSE, sizeof (guchar));
          g_array_append_vals (arr, decoded->str, decoded->len);
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, DBUS_TYPE_G_UCHAR_ARRAY);
          g_value_take_boxed (gvalue, arr);
          g_hash_table_insert (properties, g_strdup (name), gvalue);
          g_string_free (decoded, TRUE);
        }
      else if (0 == strcmp (type, "str"))
        {
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_STRING);
          g_value_set_string (gvalue, value);
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
      else if (0 == strcmp (type, "int"))
        {
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_INT);
          g_value_set_int (gvalue, strtol (value, NULL, 10));
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
      else if (0 == strcmp (type, "uint"))
        {
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_UINT);
          g_value_set_uint (gvalue, strtoul (value, NULL, 10));
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
      else if (0 == strcmp (type, "bool"))
        {
          gboolean val;

          if (!tp_strdiff (value, "0") || !tp_strdiff (value, "false"))
            {
              val = FALSE;
            }
          else if (!tp_strdiff (value, "1") || !tp_strdiff (value, "true"))
            {
              val = TRUE;
            }
          else
            {
              g_debug ("invalid boolean value: %s", value);
              continue;
            }

          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_BOOLEAN);
          g_value_set_boolean (gvalue, val);
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
    }

  return properties;
}

struct _set_child_from_property_data
{
  LmMessageNode *node;
  const gchar *prop;
};

static void
set_child_from_property (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GValue *gvalue = value;
  struct _set_child_from_property_data *data =
    (struct _set_child_from_property_data *) user_data;
  LmMessageNode *child;
  const char *type = NULL;

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      type = "str";
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      type = "bytes";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      type = "int";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      type = "uint";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
    {
      type = "bool";
    }
  else
    {
      /* a type we don't know how to handle: ignore it */
      g_debug ("property with unknown type \"%s\"",
          g_type_name (G_VALUE_TYPE (gvalue)));
      return;
    }

  child = lm_message_node_add_child (data->node, data->prop, "");

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      lm_message_node_set_value (child,
        g_value_get_string (gvalue));
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      GArray *arr;
      gchar *str;

      type = "bytes";
      arr = g_value_get_boxed (gvalue);
      str = base64_encode (arr->len, arr->data, FALSE);
      lm_message_node_set_value (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_int (gvalue));
      lm_message_node_set_value (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      gchar *str;

      str = g_strdup_printf ("%u", g_value_get_uint (gvalue));
      lm_message_node_set_value (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
    {
      /* we output as "0" or "1" despite the canonical representation for
       * xs:boolean being "false" or "true", for compatibility with older
       * Gabble versions (OLPC Trial-3) */
      lm_message_node_set_value (child,
          g_value_get_boolean (gvalue) ? "1" : "0");
    }
  else
    {
      g_assert_not_reached ();
    }

  lm_message_node_set_attribute (child, "name", key);
  lm_message_node_set_attribute (child, "type", type);
}

/**
 *
 * lm_message_node_set_children_from_properties
 *
 * Map a properties hash table to a XML node.
 *
 * Example:
 *
 * properties = { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * lm_message_node_add_children_from_properties (node, properties, "prop");
 *
 * --> <node>
 *       <prop name="prop1" type="str">prop1_value</prop>
 *       <prop name="prop2" type="uint">7</prop>
 *     </node>
 *
 */
void
lm_message_node_add_children_from_properties (LmMessageNode *node,
                                              GHashTable *properties,
                                              const gchar *prop)
{
  struct _set_child_from_property_data data;

  data.node = node;
  data.prop = prop;

  g_hash_table_foreach (properties, set_child_from_property, &data);
}

#ifndef HAVE_TP_G_HASH_TABLE_UPDATE
struct _gabble_g_hash_table_update
{
  GHashTable *target;
  GBoxedCopyFunc key_dup, value_dup;
};

static void
_gabble_g_hash_table_update_helper (gpointer key,
                                    gpointer value,
                                    gpointer user_data)
{
  struct _gabble_g_hash_table_update *data = user_data;
  gpointer new_key = data->key_dup ? (data->key_dup) (key) : key;
  gpointer new_value = data->value_dup ? (data->value_dup) (value) : value;

  g_hash_table_replace (data->target, new_key, new_value);
}

/* To be added to tp-glib util.c with s/gabble/tp/ ? */

/**
 * gabble_g_hash_table_update:
 * @target: The hash table to be updated
 * @source: The hash table to update it with (read-only)
 * @key_dup: function to duplicate a key from @source so it can be be stored
 *           in @target. If NULL, the key is not copied, but is used as-is
 * @value_dup: function to duplicate a value from @source so it can be stored
 *             in @target. If NULL, the value is not copied, but is used as-is
 *
 * Add each item in @source to @target, replacing any existing item with the
 * same key. @key_dup and @value_dup are used to duplicate the items; in
 * principle they could also be used to convert between types.
 */
void
gabble_g_hash_table_update (GHashTable *target,
                            GHashTable *source,
                            GBoxedCopyFunc key_dup,
                            GBoxedCopyFunc value_dup)
{
  struct _gabble_g_hash_table_update data = { target, key_dup,
      value_dup };

  g_return_if_fail (target != NULL);
  g_return_if_fail (source != NULL);

  g_hash_table_foreach (source, _gabble_g_hash_table_update_helper, &data);
}
#endif
