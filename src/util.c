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

#include <gobject/gvaluecollector.h>

#include <wocky/wocky.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>

#define DEBUG_FLAG GABBLE_DEBUG_JID

#include "base64.h"
#include "conn-aliasing.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"

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


/** gabble_generate_id:
 *
 * RFC4122 version 4 compliant random UUIDs generator.
 *
 * Returns: A string with RFC41122 version 4 random UUID, must be freed with
 *          g_free().
 */
gchar *
gabble_generate_id (void)
{
  GRand *grand;
  gchar *str;
  struct {
      guint32 time_low;
      guint16 time_mid;
      guint16 time_hi_and_version;
      guint8 clock_seq_hi_and_rsv;
      guint8 clock_seq_low;
      guint16 node_hi;
      guint32 node_low;
  } uuid;

  /* Fill with random. Every new GRand are seede with 128 bit read from
   * /dev/urandom (or the current time on non-unix systems). This makes the
   * random source good enough for our usage, but may not be suitable for all
   * situation outside Gabble. */
  grand = g_rand_new ();
  uuid.time_low = g_rand_int (grand);
  uuid.time_mid = (guint16) g_rand_int_range (grand, 0, G_MAXUINT16);
  uuid.time_hi_and_version = (guint16) g_rand_int_range (grand, 0, G_MAXUINT16);
  uuid.clock_seq_hi_and_rsv = (guint8) g_rand_int_range (grand, 0, G_MAXUINT8);
  uuid.clock_seq_low = (guint8) g_rand_int_range (grand, 0, G_MAXUINT8);
  uuid.node_hi = (guint16) g_rand_int_range (grand, 0, G_MAXUINT16);
  uuid.node_low = g_rand_int (grand);
  g_rand_free (grand);

  /* Set the two most significant bits (bits 6 and 7) of the
   * clock_seq_hi_and_rsv to zero and one, respectively. */
  uuid.clock_seq_hi_and_rsv = (uuid.clock_seq_hi_and_rsv & 0x3F) | 0x80;

  /* Set the four most significant bits (bits 12 through 15) of the
   * time_hi_and_version field to 4 */
  uuid.time_hi_and_version = (uuid.time_hi_and_version & 0x0fff) | 0x4000;

  str = g_strdup_printf ("%08x-%04x-%04x-%02x%02x-%04x%08x",
    uuid.time_low,
    uuid.time_mid,
    uuid.time_hi_and_version,
    uuid.clock_seq_hi_and_rsv,
    uuid.clock_seq_low,
    uuid.node_hi,
    uuid.node_low);

  return str;
}


static void
lm_message_node_add_nick (WockyNode *node, const gchar *nick)
{
  WockyNode *nick_node;

  nick_node = wocky_node_add_child_with_content (node, "nick", nick);
  nick_node->ns = g_quark_from_string (NS_NICK);
}

void
lm_message_node_add_own_nick (WockyNode *node,
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
  g_set_error (e, TP_ERROR, TP_ERROR_INVALID_HANDLE, f, ##__VA_ARGS__);\
  } G_STMT_END

gchar *
gabble_normalize_room (TpHandleRepoIface *repo,
                       const gchar *jid,
                       gpointer context,
                       GError **error)
{
  GabbleConnection *conn;
  gchar *qualified_name, *resource;

  /* Only look up the canonical room name if we got a GabbleConnection.
   * This should only happen in the test-handles test. */
  if (context != NULL)
    {
      conn = GABBLE_CONNECTION (context);
      qualified_name = gabble_connection_get_canonical_room_name (conn, jid);

      if (qualified_name == NULL)
        {
          INVALID_HANDLE (error,
              "requested room handle %s does not specify a server, but we "
              "have not discovered any local conference servers and no "
              "fallback was provided", jid);
          return NULL;
        }
    }
  else
    {
      qualified_name = g_strdup (jid);
    }

  if (!wocky_decode_jid (qualified_name, NULL, NULL, &resource))
    {
      INVALID_HANDLE (error, "room JID %s is invalid", qualified_name);
      return NULL;
    }

  if (resource != NULL)
    {
      INVALID_HANDLE (error,
          "invalid room JID %s: contains nickname part after '/' too",
          qualified_name);
      g_free (qualified_name);
      g_free (resource);
      return NULL;
    }

  return qualified_name;
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

/*
 * gabble_normalize_contact
 * @repo: The %TP_HANDLE_TYPE_ROOM handle repository or NULL
 * @jid: A JID
 * @context: One of %GabbleNormalizeContactJIDMode casted into gpointer
 * @error: pointer in which to return a GError in case of failure.
 *
 * Normalize contact JID. If @repo is provided and the context is not
 * clear (we don't know for sure whether it's global or room JID), it's
 * used to try and detect room JIDs.
 *
 * Returns: Normalized JID.
 */
gchar *
gabble_normalize_contact (TpHandleRepoIface *repo,
                          const gchar *jid,
                          gpointer context,
                          GError **error)
{
  guint mode = GPOINTER_TO_UINT (context);
  gchar *username = NULL, *server = NULL, *resource = NULL;
  gchar *ret = NULL;

  if (!wocky_decode_jid (jid, &username, &server, &resource) || !username)
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
lm_message_node_extract_properties (WockyNode *node,
                                    const gchar *prop)
{
  GHashTable *properties;
  WockyNodeIter i;
  WockyNode *child;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  if (node == NULL)
    return properties;

  wocky_node_iter_init (&i, node, prop, NULL);
  while (wocky_node_iter_next (&i, &child))
    {
      const gchar *name = wocky_node_get_attribute (child, "name");
      const gchar *type = wocky_node_get_attribute (child, "type");
      const gchar *value = child->content;
      GValue *gvalue;

      if (name == NULL || type == NULL || value == NULL)
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
  WockyNode *node;
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
  WockyNode *child;
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

  child = wocky_node_add_child_with_content (data->node, data->prop, "");

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      wocky_node_set_content (child,
        g_value_get_string (gvalue));
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      GArray *arr;
      gchar *str;

      type = "bytes";
      arr = g_value_get_boxed (gvalue);
      str = base64_encode (arr->len, arr->data, FALSE);
      wocky_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_int (gvalue));
      wocky_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      gchar *str;

      str = g_strdup_printf ("%u", g_value_get_uint (gvalue));
      wocky_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
    {
      /* we output as "0" or "1" despite the canonical representation for
       * xs:boolean being "false" or "true", for compatibility with older
       * Gabble versions (OLPC Trial-3) */
      wocky_node_set_content (child,
          g_value_get_boolean (gvalue) ? "1" : "0");
    }
  else
    {
      g_assert_not_reached ();
    }

  wocky_node_set_attribute (child, "name", key);
  wocky_node_set_attribute (child, "type", type);
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
lm_message_node_add_children_from_properties (WockyNode *node,
                                              GHashTable *properties,
                                              const gchar *prop)
{
  struct _set_child_from_property_data data;

  data.node = node;
  data.prop = prop;

  g_hash_table_foreach (properties, set_child_from_property, &data);
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

GPtrArray *
gabble_g_ptr_array_copy (GPtrArray *source)
{
  GPtrArray *ret = g_ptr_array_sized_new (source->len);
  guint i;

  for (i = 0; i < source->len; i++)
    g_ptr_array_add (ret, g_ptr_array_index (source, i));

  return ret;
}

WockyBareContact *
ensure_bare_contact_from_jid (GabbleConnection *conn,
    const gchar *jid)
{
  WockyContactFactory *contact_factory;

  contact_factory = wocky_session_get_contact_factory (conn->session);
  return wocky_contact_factory_ensure_bare_contact (contact_factory, jid);
}

TpHandle
ensure_handle_from_contact (
    GabbleConnection *conn,
    WockyContact *contact)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  gchar *jid = wocky_contact_dup_jid (contact);
  GError *error = NULL;
  TpHandle handle = tp_handle_ensure (contact_repo, jid, NULL, &error);

  if (handle == 0)
    {
      g_critical ("Contact %p has JID '%s' which is not valid: %s",
          contact, jid, error->message);
      g_clear_error (&error);
    }

  g_free (jid);
  return handle;
}

#ifdef ENABLE_VOIP

#define TWICE(x) x, x
static gboolean
jingle_pick_resource_or_bare_jid (GabblePresence *presence,
    GabbleCapabilitySet *caps, const gchar **resource)
{
  const gchar *ret;

  if (gabble_presence_has_resources (presence))
    {
      ret = gabble_presence_pick_resource_by_caps (presence,
          GABBLE_CLIENT_TYPE_PHONE,
          gabble_capability_set_predicate_at_least, caps);

      if (resource != NULL)
        *resource = ret;

      return (ret != NULL);
    }
  else if (gabble_capability_set_at_least (
        gabble_presence_peek_caps (presence), caps))
    {
      if (resource != NULL)
        *resource = NULL;

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

gboolean
jingle_pick_best_resource (GabbleConnection *conn,
    TpHandle peer,
    gboolean want_audio,
    gboolean want_video,
    const char **transport_ns,
    JingleDialect *dialect,
    const gchar **resource_out)
{
  /* We prefer gtalk-p2p to ice, because it can use tcp and https relays (if
   * available). */
  static const GabbleFeatureFallback transports[] = {
        { TRUE, TWICE (NS_GOOGLE_TRANSPORT_P2P) },
        { TRUE, TWICE (NS_JINGLE_TRANSPORT_ICEUDP) },
        { TRUE, TWICE (NS_JINGLE_TRANSPORT_RAWUDP) },
        { FALSE, NULL, NULL }
  };
  GabblePresence *presence;
  GabbleCapabilitySet *caps;
  const gchar *resource = NULL;
  gboolean success = FALSE;

  presence = gabble_presence_cache_get (conn->presence_cache, peer);

  if (presence == NULL)
    {
      DEBUG ("contact %d has no presence available", peer);
      return FALSE;
    }

  *dialect = JINGLE_DIALECT_ERROR;
  *transport_ns = NULL;

  g_return_val_if_fail (want_audio || want_video, FALSE);

  /* from here on, goto FINALLY to free this, instead of returning early */
  caps = gabble_capability_set_new ();

  /* Try newest Jingle standard */
  gabble_capability_set_add (caps, NS_JINGLE_RTP);

  if (want_audio)
    gabble_capability_set_add (caps, NS_JINGLE_RTP_AUDIO);
  if (want_video)
    gabble_capability_set_add (caps, NS_JINGLE_RTP_VIDEO);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_V032;
      goto CHOOSE_TRANSPORT;
    }

  /* Else try older Jingle draft */
  gabble_capability_set_clear (caps);

  if (want_audio)
    gabble_capability_set_add (caps, NS_JINGLE_DESCRIPTION_AUDIO);
  if (want_video)
    gabble_capability_set_add (caps, NS_JINGLE_DESCRIPTION_VIDEO);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_V015;
      goto CHOOSE_TRANSPORT;
    }

  /* The Google dialects can't do video alone. */
  if (!want_audio)
    {
      DEBUG ("No resource which supports video alone available");
      goto FINALLY;
    }

  /* Okay, let's try GTalk 0.3, possibly with video. */
  gabble_capability_set_clear (caps);
  gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VOICE);

  if (want_video)
    gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VIDEO);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_GTALK3;
      goto CHOOSE_TRANSPORT;
    }

  if (want_video)
    {
      DEBUG ("No resource which supports audio+video available");
      goto FINALLY;
    }

  /* Maybe GTalk 0.4 will save us all... ? */
  gabble_capability_set_clear (caps);
  gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VOICE);
  gabble_capability_set_add (caps, NS_GOOGLE_TRANSPORT_P2P);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_GTALK4;
      goto CHOOSE_TRANSPORT;
    }

  /* Nope, nothing we can do. */
  goto FINALLY;

CHOOSE_TRANSPORT:

  if (resource_out != NULL)
    *resource_out = resource;

  success = TRUE;

  if (*dialect == JINGLE_DIALECT_GTALK4 || *dialect == JINGLE_DIALECT_GTALK3)
    {
      /* the GTalk dialects only support google p2p as transport protocol. */
      *transport_ns = NS_GOOGLE_TRANSPORT_P2P;
    }
  else if (resource == NULL)
    {
      *transport_ns = gabble_presence_pick_best_feature (presence, transports,
          gabble_capability_set_predicate_has);
    }
  else
    {
      *transport_ns = gabble_presence_resource_pick_best_feature (presence,
        resource, transports, gabble_capability_set_predicate_has);
    }

  if (*transport_ns == NULL)
    success = FALSE;

FINALLY:
  gabble_capability_set_free (caps);
  return success;
}

const gchar *
jingle_pick_best_content_type (GabbleConnection *conn,
  TpHandle peer,
  const gchar *resource,
  JingleMediaType type)
{
  GabblePresence *presence;
  const GabbleFeatureFallback content_types[] = {
      /* if $thing is supported, then use it */
        { TRUE, TWICE (NS_JINGLE_RTP) },
        { type == JINGLE_MEDIA_TYPE_VIDEO,
            TWICE (NS_JINGLE_DESCRIPTION_VIDEO) },
        { type == JINGLE_MEDIA_TYPE_AUDIO,
            TWICE (NS_JINGLE_DESCRIPTION_AUDIO) },
      /* odd Google ones: if $thing is supported, use $other_thing */
        { type == JINGLE_MEDIA_TYPE_AUDIO,
          NS_GOOGLE_FEAT_VOICE, NS_GOOGLE_SESSION_PHONE },
        { type == JINGLE_MEDIA_TYPE_VIDEO,
          NS_GOOGLE_FEAT_VIDEO, NS_GOOGLE_SESSION_VIDEO },
        { FALSE, NULL, NULL }
  };

  presence = gabble_presence_cache_get (conn->presence_cache, peer);

  if (presence == NULL)
    {
      DEBUG ("contact %d has no presence available", peer);
      return NULL;
    }

  if (resource == NULL)
    {
      return gabble_presence_pick_best_feature (presence, content_types,
          gabble_capability_set_predicate_has);
    }
  else
    {
      return gabble_presence_resource_pick_best_feature (presence, resource,
          content_types, gabble_capability_set_predicate_has);
    }
}

static TpCallStreamCandidateType
tp_candidate_type_from_jingle (JingleCandidateType type)
{
  switch (type)
    {
    default:
      /* Consider UNKNOWN as LOCAL/HOST */
    case JINGLE_CANDIDATE_TYPE_LOCAL:
      return TP_CALL_STREAM_CANDIDATE_TYPE_HOST;
    case JINGLE_CANDIDATE_TYPE_STUN:
      return TP_CALL_STREAM_CANDIDATE_TYPE_SERVER_REFLEXIVE;
    case JINGLE_CANDIDATE_TYPE_RELAY:
      return TP_CALL_STREAM_CANDIDATE_TYPE_RELAY;
    }
}

/**
 * @candidates: (element-type JingleCandidate): candidates
 *
 * Returns: (transfer full): a GABBLE_ARRAY_TYPE_CANDIDATE_LIST, i.e.
 *  a(usqa{sv})
 */
GPtrArray *
gabble_call_candidates_to_array (GList *candidates)
{
  GPtrArray *arr;
  GList *c;

  arr = g_ptr_array_sized_new (g_list_length (candidates));

  for (c = candidates; c != NULL; c = g_list_next (c))
    {
        JingleCandidate *cand = (JingleCandidate *) c->data;
        GValueArray *a;
        GHashTable *info;

        info = tp_asv_new (
          "protocol", G_TYPE_UINT, cand->protocol,
          "type", G_TYPE_UINT, tp_candidate_type_from_jingle (cand->type),
          "foundation", G_TYPE_STRING, cand->id,
          "priority", G_TYPE_UINT, cand->preference,
          "username", G_TYPE_STRING, cand->username,
          "password", G_TYPE_STRING, cand->password,
          NULL);

         a = tp_value_array_build (4,
            G_TYPE_UINT, cand->component,
            G_TYPE_STRING, cand->address,
            G_TYPE_UINT, cand->port,
            TP_HASH_TYPE_CANDIDATE_INFO, info,
            G_TYPE_INVALID);

        g_ptr_array_add (arr, a);
  }

  return arr;
}

#endif

gchar *
gabble_peer_to_jid (GabbleConnection *conn,
    TpHandle peer,
    const gchar *resource)
{
  TpHandleRepoIface *repo = tp_base_connection_get_handles (
    TP_BASE_CONNECTION (conn), TP_HANDLE_TYPE_CONTACT);
  const gchar *target = tp_handle_inspect (repo, peer);

  if (resource == NULL)
    return g_strdup (target);

  return g_strdup_printf ("%s/%s", target, resource);
}

/* Like wocky_enum_from_nick, but for GFlagsValues instead. */
gboolean
gabble_flag_from_nick (GType flag_type,
    const gchar *nick,
    guint *value)
{
  GFlagsClass *klass = g_type_class_ref (flag_type);
  GFlagsValue *flag_value;

  g_return_val_if_fail (klass != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  flag_value = g_flags_get_value_by_nick (klass, nick);
  g_type_class_unref (klass);

  if (flag_value != NULL)
    {
      *value = flag_value->value;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

/**
 * gabble_simple_async_succeed_or_fail_in_idle:
 * @self: the source object for an asynchronous function
 * @callback: a callback to call when @todo things have been done
 * @user_data: user data for the callback
 * @source_tag: the source tag for a #GSimpleAsyncResult
 * @error: (allow-none): %NULL to indicate success, or an error on failure
 *
 * Create a new #GSimpleAsyncResult and schedule it to call its callback
 * in an idle. If @error is %NULL, report success with
 * tp_simple_async_report_success_in_idle(); if @error is non-%NULL,
 * use g_simple_async_report_gerror_in_idle().
 */
void
gabble_simple_async_succeed_or_fail_in_idle (gpointer self,
    GAsyncReadyCallback callback,
    gpointer user_data,
    gpointer source_tag,
    const GError *error)
{
  if (error == NULL)
    {
      tp_simple_async_report_success_in_idle (self, callback, user_data,
          source_tag);
    }
  else
    {
      /* not const-correct yet: GNOME #622004 */
      g_simple_async_report_gerror_in_idle (self, callback, user_data,
          (GError *) error);
    }
}

/**
 * gabble_simple_async_countdown_new:
 * @self: the source object for an asynchronous function
 * @callback: a callback to call when @todo things have been done
 * @user_data: user data for the callback
 * @source_tag: the source tag for a #GSimpleAsyncResult
 * @todo: number of things to do before calling @callback (at least 1)
 *
 * Create a new #GSimpleAsyncResult that will call its callback when a number
 * of asynchronous operations have happened.
 *
 * An internal counter is initialized to @todo, incremented with
 * gabble_simple_async_countdown_inc() or decremented with
 * gabble_simple_async_countdown_dec().
 *
 * When that counter reaches zero, if an error has been set with
 * g_simple_async_result_set_from_error() or similar, the operation fails;
 * otherwise, it succeeds.
 *
 * The caller must not use the operation result functions, such as
 * g_simple_async_result_get_op_res_gssize() - this async result is only
 * suitable for "void" async methods which return either success or a #GError,
 * i.e. the same signature as g_async_initable_init_async().
 *
 * Returns: (transfer full): a counter
 */
GSimpleAsyncResult *
gabble_simple_async_countdown_new (gpointer self,
    GAsyncReadyCallback callback,
    gpointer user_data,
    gpointer source_tag,
    gssize todo)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (todo >= 1, NULL);

  simple = g_simple_async_result_new (self, callback, user_data, source_tag);
  /* We (ab)use the op_res member as a count of things to do. When
   * it reaches zero, the operation completes with any error that has been
   * set, or with success. */
  g_simple_async_result_set_op_res_gssize (simple, todo);

  /* we keep one extra reference as long as the counter is nonzero */
  g_object_ref (simple);

  return simple;
}

/**
 * gabble_simple_async_countdown_inc:
 * @simple: a result created by gabble_simple_async_countdown_new()
 *
 * Increment the counter in @simple, indicating that an additional async
 * operation has been started. An additional call to
 * gabble_simple_async_countdown_dec() will be needed to make @simple
 * call its callback.
 */
void
gabble_simple_async_countdown_inc (GSimpleAsyncResult *simple)
{
  gssize todo = g_simple_async_result_get_op_res_gssize (simple);

  g_return_if_fail (todo >= 1);
  g_simple_async_result_set_op_res_gssize (simple, todo + 1);
}

/**
 * gabble_simple_async_countdown_dec:
 * @simple: a result created by gabble_simple_async_countdown_new()
 *
 * Decrement the counter in @simple. If the number of things to do has
 * reached zero, schedule @simple to call its callback in an idle, then
 * unref it.
 *
 * When one of the asynchronous operations needed for @simple succeeds,
 * this should be signalled by a call to this function.
 *
 * When one of the asynchronous operations needed for @simple fails,
 * this should be signalled by a call to g_simple_async_result_set_from_error()
 * (or one of the similar functions), followed by a call to this function.
 * If more than one async operation fails in this way, the #GError from the
 * last failure will be used.
 */
void
gabble_simple_async_countdown_dec (GSimpleAsyncResult *simple)
{
  gssize todo = g_simple_async_result_get_op_res_gssize (simple);

  g_simple_async_result_set_op_res_gssize (simple, --todo);

  if (todo <= 0)
    {
      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
    }
}
