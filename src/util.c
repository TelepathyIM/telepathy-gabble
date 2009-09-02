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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/handle-repo-dynamic.h>

#ifdef HAVE_UUID
# include <uuid.h>
#endif

#define DEBUG_FLAG GABBLE_DEBUG_JID

#include "base64.h"
#include "conn-aliasing.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"

gchar *
sha1_hex (const gchar *bytes,
          guint len)
{
  gchar *hex = g_compute_checksum_for_string (G_CHECKSUM_SHA1, bytes, len);
  guint i;

  for (i = 0; i < SHA1_HASH_SIZE * 2; i++)
    {
      g_assert (hex[i] != '\0');
      hex[i] = g_ascii_tolower (hex[i]);
    }

  g_assert (hex[SHA1_HASH_SIZE * 2] == '\0');

  return hex;
}

void
sha1_bin (const gchar *bytes,
          guint len,
          guchar out[SHA1_HASH_SIZE])
{
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA1);
  gsize out_len = SHA1_HASH_SIZE;

  g_assert (g_checksum_type_get_length (G_CHECKSUM_SHA1) == SHA1_HASH_SIZE);
  g_checksum_update (checksum, (const guchar *) bytes, len);
  g_checksum_get_digest (checksum, out, &out_len);
  g_assert (out_len == SHA1_HASH_SIZE);
  g_checksum_free (checksum);
}

gchar *
gabble_generate_id (void)
{
#ifdef HAVE_UUID
  /* generate random UUIDs */
  uuid_t uu;
  gchar *str;

  str = g_new0 (gchar, 37);
  uuid_generate_random (uu);
  uuid_unparse_lower (uu, str);
  return str;
#else
  /* generate from the time, a counter, and a random integer */
  static gulong last = 0;
  GTimeVal tv;

  g_get_current_time (&tv);
  return g_strdup_printf ("%lx.%lx/%lx/%x", tv.tv_sec, tv.tv_usec,
      last++, g_random_int ());
#endif
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
  TpBaseConnection *base = (TpBaseConnection *) connection;

  source = _gabble_connection_get_cached_alias (connection,
        base->self_handle, &nick);

  if (source > GABBLE_CONNECTION_ALIAS_FROM_JID)
    lm_message_node_add_nick (node, nick);

  g_free (nick);
}

void
lm_message_node_unlink (LmMessageNode *orphan,
    LmMessageNode *parent)
{
  if (parent && orphan == parent->children)
    parent->children = orphan->next;
  if (orphan->prev)
    orphan->prev->next = orphan->next;
  if (orphan->next)
    orphan->next->prev = orphan->prev;
}

void
lm_message_node_steal_children (LmMessageNode *snatcher,
                                LmMessageNode *mum)
{
  NodeIter i;

  g_return_if_fail (snatcher->children == NULL);

  if (mum->children == NULL)
    return;

  snatcher->children = mum->children;
  mum->children = NULL;

  for (i = node_iter (snatcher); i; i = node_iter_next (i))
    {
      LmMessageNode *baby = node_iter_data (i);
      baby->parent = snatcher;
    }
}

/* variant of lm_message_node_get_child() which ignores node namespace
 * prefix */
LmMessageNode *
lm_message_node_get_child_any_ns (LmMessageNode *node, const gchar *name)
{
  NodeIter i;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *child = node_iter_data (i);

      if (!tp_strdiff (lm_message_node_get_name (child), name))
          return child;
    }

  return NULL;
}

static const gchar *
find_namespace_of_prefix (LmMessageNode *node,
    const gchar *prefix)
{
  gchar *attr = g_strdup_printf ("xmlns:%s", prefix);
  const gchar *node_ns = NULL;

  /* find the namespace in this node or its parents */
  for (; (node != NULL) && (node_ns == NULL); node = node->parent)
    {
      node_ns = lm_message_node_get_attribute (node, attr);
    }

  g_free (attr);
  return node_ns;
}

const gchar *
lm_message_node_get_namespace (LmMessageNode *node)
{
  const gchar *node_ns = NULL;
  gchar *x = strchr (node->name, ':');

  if (x != NULL)
    {
      gchar *prefix = g_strndup (node->name, (x - node->name));

      node_ns = find_namespace_of_prefix (node, prefix);
      g_free (prefix);
    }
  else
    {
      node_ns = lm_message_node_get_attribute (node, "xmlns");
    }

  return node_ns;
}

const gchar *
lm_message_node_get_name (LmMessageNode *node)
{
  gchar *x = strchr (node->name, ':');

  if (x != NULL)
    return x + 1;
  else
    return node->name;
}

gboolean
lm_message_node_has_namespace (LmMessageNode *node,
                               const gchar *ns,
                               const gchar *tag)
{
  return (!tp_strdiff (lm_message_node_get_namespace (node), ns));
}

LmMessageNode *
lm_message_node_get_child_with_namespace (LmMessageNode *node,
                                          const gchar *name,
                                          const gchar *ns)
{
  NodeIter i;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *tmp = node_iter_data (i);
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
            LmMessageNode **assign_to = va_arg (ap, LmMessageNode **);

            g_return_if_fail (assign_to != NULL);
            *assign_to = stack->data;
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

static gboolean
validate_jid_node (const gchar *node)
{
  /* See RFC 3920 §3.3. */
  const gchar *c;

  for (c = node; *c; c++)
    if (strchr ("\"&'/:<>@", *c))
      /* RFC 3920 §A.5 */
      return FALSE;

  return TRUE;
}

static gboolean
validate_jid_domain (const gchar *domain)
{
  /* XXX: This doesn't do proper validation, it just checks the character
   * range. In theory, we check that the domain is a well-formed IDN or
   * an IPv4/IPv6 address literal.
   *
   * See RFC 3920 §3.2.
   */

  const gchar *c;

  for (c = domain; *c; c++)
    if (!g_ascii_isalnum (*c) && !strchr (":-.", *c))
      return FALSE;

  return TRUE;
}

/**
 * gabble_decode_jid
 *
 * Parses a JID which may be one of the following forms:
 *
 *  domain
 *  domain/resource
 *  node@domain
 *  node@domain/resource
 *
 * If the JID is valid, returns TRUE and sets the caller's
 * node/domain/resource pointers if they are not NULL. The node and resource
 * pointers will be set to NULL if the respective part is not present in the
 * JID. The node and domain are lower-cased because the Jabber protocol treats
 * them case-insensitively.
 *
 * XXX: Do nodeprep/resourceprep and length checking.
 *
 * See RFC 3920 §3.
 */
gboolean
gabble_decode_jid (const gchar *jid,
                   gchar **node,
                   gchar **domain,
                   gchar **resource)
{
  char *tmp_jid, *tmp_node, *tmp_domain, *tmp_resource;

  g_assert (jid != NULL);

  if (node != NULL)
    *node = NULL;

  if (domain != NULL)
    *domain = NULL;

  if (resource != NULL)
    *resource = NULL;

  /* Take a local copy so we don't modify the caller's string. */
  tmp_jid = g_strdup (jid);

  /* If there's a slash in tmp_jid, split it in two and take the second part as
   * the resource.
   */
  tmp_resource = strchr (tmp_jid, '/');

  if (tmp_resource)
    {
      *tmp_resource = '\0';
      tmp_resource++;
    }
  else
    {
      tmp_resource = NULL;
    }

  /* If there's an at sign in tmp_jid, split it in two and set tmp_node and
   * tmp_domain appropriately. Otherwise, tmp_node is NULL and the domain is
   * the whole string.
   */
  tmp_domain = strchr (tmp_jid, '@');

  if (tmp_domain)
    {
      *tmp_domain = '\0';
      tmp_domain++;
      tmp_node = tmp_jid;
    }
  else
    {
      tmp_domain = tmp_jid;
      tmp_node = NULL;
    }

  /* Domain must be non-empty and not contain invalid characters. If the node
   * or the resource exist, they must be non-empty and the node must not
   * contain invalid characters.
   */
  if (*tmp_domain == '\0' ||
      !validate_jid_domain (tmp_domain) ||
      (tmp_node != NULL &&
         (*tmp_node == '\0' || !validate_jid_node (tmp_node))) ||
      (tmp_resource != NULL && *tmp_resource == '\0'))
    {
      g_free (tmp_jid);
      return FALSE;
    }

  /* the server must be stored after we find the resource, in case we
   * truncated a resource from it */
  if (domain != NULL)
    *domain = g_utf8_strdown (tmp_domain, -1);

  /* store the username if the user provided a pointer */
  if (tmp_node != NULL && node != NULL)
    *node = g_utf8_strdown (tmp_node, -1);

  /* store the resource if the user provided a pointer */
  if (tmp_resource != NULL && resource != NULL)
    *resource = g_strdup (tmp_resource);

  /* free our working copy */
  g_free (tmp_jid);
  return TRUE;
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

#define INVALID_HANDLE(e, f, ...) \
  G_STMT_START { \
  DEBUG (f, ##__VA_ARGS__); \
  g_set_error (e, TP_ERRORS, TP_ERROR_INVALID_HANDLE, f, ##__VA_ARGS__);\
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
      INVALID_HANDLE (error,
          "invalid room JID %s: does not contain '@'", jid);
      return NULL;
    }
  if (at == jid)
    {
      INVALID_HANDLE (error,
          "invalid room JID %s: room name before '@' may not be empty", jid);
      return NULL;
    }

  /* room names can't contain the nick part */
  if (slash != NULL)
    {
      INVALID_HANDLE (error,
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
gabble_encode_jid (
    const gchar *node,
    const gchar *domain,
    const gchar *resource)
{
  gchar *tmp, *ret;

  g_return_val_if_fail (domain != NULL, NULL);

  if (node != NULL && resource != NULL)
    tmp = g_strdup_printf ("%s@%s/%s", node, domain, resource);
  else if (node != NULL)
    tmp = g_strdup_printf ("%s@%s", node, domain);
  else if (resource != NULL)
    tmp = g_strdup_printf ("%s/%s", domain, resource);
  else
    tmp = g_strdup (domain);

  ret = g_utf8_normalize (tmp, -1, G_NORMALIZE_NFKC);
  g_free (tmp);
  return ret;
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

  if (!gabble_decode_jid (jid, &username, &server, &resource) || !username)
    {
      INVALID_HANDLE (error,
          "JID %s is invalid or has no node part", jid);
      goto OUT;
    }

  if (mode == GABBLE_JID_ROOM_MEMBER && resource == NULL)
    {
      INVALID_HANDLE (error,
          "JID %s can't be a room member - it has no resource", jid);
      goto OUT;
    }

  if (mode != GABBLE_JID_GLOBAL && resource != NULL)
    {
      ret = gabble_encode_jid (username, server, resource);

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
  ret = gabble_encode_jid (username, server, NULL);

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
  NodeIter i;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  if (node == NULL)
    return properties;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *child = node_iter_data (i);
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
              DEBUG ("invalid boolean value: %s", value);
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
      DEBUG ("property with unknown type \"%s\"",
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

/**
 * lm_iq_message_make_result:
 * @iq_message: A LmMessage containing an IQ stanza to acknowledge
 *
 * Creates a result IQ stanza to acknowledge @iq_message.
 *
 * Returns: A newly-created LmMessage containing the result IQ stanza.
 */
LmMessage *
lm_iq_message_make_result (LmMessage *iq_message)
{
  LmMessage *result;
  LmMessageNode *iq, *result_iq;
  const gchar *from_jid, *id;

  g_assert (lm_message_get_type (iq_message) == LM_MESSAGE_TYPE_IQ);
  g_assert (lm_message_get_sub_type (iq_message) == LM_MESSAGE_SUB_TYPE_GET ||
            lm_message_get_sub_type (iq_message) == LM_MESSAGE_SUB_TYPE_SET);

  iq = lm_message_get_node (iq_message);
  id = lm_message_node_get_attribute (iq, "id");

  if (id == NULL)
    {
      NODE_DEBUG (iq, "can't acknowledge IQ with no id");
      return NULL;
    }

  from_jid = lm_message_node_get_attribute (iq, "from");

  result = lm_message_new_with_sub_type (from_jid, LM_MESSAGE_TYPE_IQ,
                                         LM_MESSAGE_SUB_TYPE_RESULT);
  result_iq = lm_message_get_node (result);
  lm_message_node_set_attribute (result_iq, "id", id);

  return result;
}

typedef struct {
    GObject *instance;
    GObject *user_data;
    gulong handler_id;
} WeakHandlerCtx;

static WeakHandlerCtx *
whc_new (GObject *instance,
         GObject *user_data)
{
  WeakHandlerCtx *ctx = g_slice_new0 (WeakHandlerCtx);

  ctx->instance = instance;
  ctx->user_data = user_data;

  return ctx;
}

static void
whc_free (WeakHandlerCtx *ctx)
{
  g_slice_free (WeakHandlerCtx, ctx);
}

static void user_data_destroyed_cb (gpointer, GObject *);

static void
instance_destroyed_cb (gpointer ctx_,
                       GObject *where_the_instance_was)
{
  WeakHandlerCtx *ctx = ctx_;

  DEBUG ("instance for %p destroyed; cleaning up", ctx);

  /* No need to disconnect the signal here, the instance has gone away. */
  g_object_weak_unref (ctx->user_data, user_data_destroyed_cb, ctx);
  whc_free (ctx);
}

static void
user_data_destroyed_cb (gpointer ctx_,
                        GObject *where_the_user_data_was)
{
  WeakHandlerCtx *ctx = ctx_;

  DEBUG ("user_data for %p destroyed; disconnecting", ctx);

  g_signal_handler_disconnect (ctx->instance, ctx->handler_id);
  g_object_weak_unref (ctx->instance, instance_destroyed_cb, ctx);
  whc_free (ctx);
}

/**
 * gabble_signal_connect_weak:
 * @instance: the instance to connect to.
 * @detailed_signal: a string of the form "signal-name::detail".
 * @c_handler: the GCallback to connect.
 * @user_data: an object to pass as data to c_handler calls.
 *
 * Connects a #GCallback function to a signal for a particular object, as if
 * with g_signal_connect(). Additionally, arranges for the signal handler to be
 * disconnected if @user_data is destroyed.
 *
 * This is intended to be a convenient way for objects to use themselves as
 * user_data for callbacks without having to explicitly disconnect all the
 * handlers in their finalizers.
 */
void
gabble_signal_connect_weak (gpointer instance,
                            const gchar *detailed_signal,
                            GCallback c_handler,
                            GObject *user_data)
{
  GObject *instance_obj = G_OBJECT (instance);
  WeakHandlerCtx *ctx = whc_new (instance_obj, user_data);

  DEBUG ("connecting to %p:%s with context %p", instance, detailed_signal, ctx);

  ctx->handler_id = g_signal_connect (instance, detailed_signal, c_handler,
      user_data);

  g_object_weak_ref (instance_obj, instance_destroyed_cb, ctx);
  g_object_weak_ref (user_data, user_data_destroyed_cb, ctx);
}

typedef struct {
    GSourceFunc function;
    GObject *object;
    guint source_id;
} WeakIdleCtx;

static void
idle_weak_ref_notify (gpointer data,
                      GObject *dead_object)
{
  g_source_remove (GPOINTER_TO_UINT (data));
}

static void
idle_removed (gpointer data)
{
  WeakIdleCtx *ctx = (WeakIdleCtx *) data;

  g_slice_free (WeakIdleCtx, ctx);
}

static gboolean
idle_callback (gpointer data)
{
  WeakIdleCtx *ctx = (WeakIdleCtx *) data;

  if (ctx->function ((gpointer) ctx->object))
    {
      return TRUE;
    }
  else
    {
      g_object_weak_unref (
          ctx->object, idle_weak_ref_notify, GUINT_TO_POINTER (ctx->source_id));
      return FALSE;
    }
}

/* Like g_idle_add(), but cancel the callback if the provided object is
 * finalized.
 */
guint
gabble_idle_add_weak (GSourceFunc function,
                      GObject *object)
{
  WeakIdleCtx *ctx;

  ctx = g_slice_new0 (WeakIdleCtx);
  ctx->function = function;
  ctx->object = object;
  ctx->source_id = g_idle_add_full (
      G_PRIORITY_DEFAULT_IDLE, idle_callback, ctx, idle_removed);

  g_object_weak_ref (
      object, idle_weak_ref_notify, GUINT_TO_POINTER (ctx->source_id));
  return ctx->source_id;
}

typedef struct {
    gchar *key;
    gchar *value;
} Attribute;

const gchar *
lm_message_node_get_attribute_with_namespace (LmMessageNode *node,
    const gchar *attribute,
    const gchar *ns)
{
  GSList *l;
  const gchar *result = NULL;

  g_return_val_if_fail (node != NULL, NULL);
  g_return_val_if_fail (attribute != NULL, NULL);
  g_return_val_if_fail (ns != NULL, NULL);

  for (l = node->attributes; l != NULL && result == NULL; l = g_slist_next (l))
    {
      /* This is NOT part of loudmouth API; it depends LM internals */
      Attribute *attr = (Attribute *) l->data;
      gchar **pair;

      pair = g_strsplit (attr->key, ":", 2);

      if (tp_strdiff (pair[1], attribute))
        /* no prefix (pair[1] == NULL) or the local-name is not the
         * attribute we are looking for */
        goto next_attribute;

      if (tp_strdiff (find_namespace_of_prefix (node, pair[0]), ns))
        /* wrong namespace */
        goto next_attribute;

      result = attr->value;

next_attribute:
      g_strfreev (pair);
      continue;
    }

  return result;
}

GPtrArray *
gabble_g_ptr_array_copy (GPtrArray *source)
{
  GPtrArray *ret = g_ptr_array_sized_new (source->len);
  guint i;

  for (i = 0; i < source->len; i++)
    g_ptr_array_add (ret, g_ptr_array_index (source, i));

  return ret;
}
