/*
 * vcard-manager.c - Source for Gabble vCard lookup helper
 *
 * Copyright (C) 2007-2010 Collabora Ltd.
 * Copyright (C) 2006-2010 Nokia Corporation
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
#include "vcard-manager.h"

#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/heap.h>
#include <wocky/wocky-utils.h>

#define DEBUG_FLAG GABBLE_DEBUG_VCARD

#include "base64.h"
#include "conn-aliasing.h"
#include "conn-contact-info.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "request-pipeline.h"
#include "util.h"

static guint default_request_timeout = 180;
#define VCARD_CACHE_ENTRY_TTL 60

/* When the server reply with XMPP_ERROR_RESOURCE_CONSTRAINT, wait
 * request_wait_delay seconds before allowing a vCard request to be sent to
 * the same recipient */
static guint request_wait_delay = 5 * 60;

static const gchar *NO_ALIAS = "none";

typedef struct {
    gchar *key;
    gchar *value;
} GabbleVCardChild;

static GabbleVCardChild *
gabble_vcard_child_new (const gchar *key,
    const gchar *value)
{
  GabbleVCardChild *child = g_slice_new (GabbleVCardChild);

  child->key = g_strdup (key);
  child->value = g_strdup (value);
  return child;
}

static void
gabble_vcard_child_free (GabbleVCardChild *child)
{
  g_free (child->key);
  g_free (child->value);
  g_slice_free (GabbleVCardChild, child);
}

struct _GabbleVCardManagerEditInfo {
    /* name of element to edit */
    gchar *element_name;

    /* value of element to edit or NULL if no value should be used */
    gchar *element_value;

    /* list of GabbleVCardChild */
    GList *children;

    /* If REPLACE, the first element with this name (if any) will be updated;
     * if APPEND, an element with this name will be added;
     * if DELETE, all elements with this name will be removed;
     * if CLEAR, everything except PHOTO and NICKNAME will be deleted, in
     *    preparation for a SetContactInfo operation
     * if SET_ALIAS and element_value is NULL, set the best alias we have
     *    as the NICKNAME or FN (as appropriate) if that field doesn't already
     *    have a value
     * if SET_ALIAS and element_value is non-NULL, set that
     *    as the NICKNAME or FN (as appropriate), overriding anything already
     *    there
     */
    GabbleVCardEditType edit_type;
};

/* signal enum */
enum
{
    NICKNAME_UPDATE,
    VCARD_UPDATE,
    GOT_SELF_INITIAL_AVATAR,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_HAVE_SELF_AVATAR,
  LAST_PROPERTY
};

G_DEFINE_TYPE(GabbleVCardManager, gabble_vcard_manager, G_TYPE_OBJECT);

typedef struct _GabbleVCardCacheEntry GabbleVCardCacheEntry;
struct _GabbleVCardManagerPrivate
{
  gboolean dispose_has_run;
  GabbleConnection *connection;

  /* TpHandle borrowed from the entry => owned (GabbleVCardCacheEntry *) */
  GHashTable *cache;

  /* Those (GabbleVCardCacheEntry *) s that have not expired, ordered by
   * increasing expiry time; borrowed from @cache */
  TpHeap *timed_cache;

  /* Timer which runs out when the first item in the @timed_cache expires */
  guint cache_timer;

  /* Things to do with my own vCard, which is somewhat special - mainly because
   * we can edit it. There's only one self_handle, so there's no point
   * bloating every cache entry with these fields. */

  gboolean have_self_avatar;

  /* list of pending edits (GabbleVCardManagerEditInfo structures) */
  GList *edits;

  /* Contains RequestPipelineItem for our SET vCard request, or NULL if we
   * don't have SET request in the pipeline already. At most one SET request
   * can be in pipeline at any given time. */
  GabbleRequestPipelineItem *edit_pipeline_item;

  /* List of all pending edit requests that we got. */
  GList *edit_requests;

  /* Patched vCard that we sent to the server to update, but haven't
   * got confirmation yet. We don't want to store it in cache (visible
   * to others) before we're sure the server accepts it. */
  WockyNode *patched_vcard;
};

struct _GabbleVCardManagerRequest
{
  GabbleVCardManager *manager;
  GabbleVCardCacheEntry *entry;
  guint timer_id;
  guint timeout;

  GabbleVCardManagerCb callback;
  gpointer user_data;
  GObject *bound_object;
};

struct _GabbleVCardManagerEditRequest
{
  GabbleVCardManager *manager;
  GabbleVCardManagerEditCb callback;
  gpointer user_data;
  GObject *bound_object;

  /* Set if we have already patched vCard with data from this request,
   * and sent a SET request to the server to replace the vCard. */
  gboolean set_in_pipeline;
};

/* An entry in the vCard cache. These exist only as long as:
 *
 * 1) the cached message which has not yet expired; and/or
 * 2) a network request is in the pipeline; and/or
 * 3) there are requests pending.
 */
struct _GabbleVCardCacheEntry
{
  /* Parent object */
  GabbleVCardManager *manager;

  /* Referenced handle */
  TpHandle handle;

  /* Pipeline item for our <iq type="get"> if one is in progress */
  GabbleRequestPipelineItem *pipeline_item;

  /* List of (GabbleVCardManagerRequest *) borrowed from priv->requests */
  GSList *pending_requests;

  /* When requests for this entry receive an error of type "wait", we suspend
   * further requests and retry again after request_wait_delay seconds.
   * 0 if not suspended.
   */
  guint suspended_timer_id;

  /* VCard node for this entry (owned reference), or NULL if there's no node */
  WockyNode *vcard_node;

  /* If @vcard_node is not NULL, the time the message will expire */
  time_t expires;
};

GQuark
gabble_vcard_manager_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-vcard-manager-error");
  return quark;
}

GQuark
gabble_vcard_manager_cache_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-vcard-manager-cache");
  return quark;
}

static void cache_entry_free (void *data);
static gint cache_entry_compare (gconstpointer a, gconstpointer b);
static void manager_patch_vcard (
    GabbleVCardManager *self, WockyNode *vcard_node);
static void request_send (GabbleVCardManagerRequest *request,
    guint timeout);

static void
gabble_vcard_manager_init (GabbleVCardManager *obj)
{
  GabbleVCardManagerPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_VCARD_MANAGER,
         GabbleVCardManagerPrivate);
  obj->priv = priv;

  priv->cache = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      cache_entry_free);
  /* no destructor here - the hash table is responsible for freeing it */
  priv->timed_cache = tp_heap_new (cache_entry_compare, NULL);
  priv->cache_timer = 0;

  priv->have_self_avatar = FALSE;
  priv->edits = NULL;
}

static void gabble_vcard_manager_set_property (GObject *object,
    guint property_id, const GValue *value, GParamSpec *pspec);
static void gabble_vcard_manager_get_property (GObject *object,
    guint property_id, GValue *value, GParamSpec *pspec);
static void gabble_vcard_manager_dispose (GObject *object);
static void gabble_vcard_manager_finalize (GObject *object);

static void
gabble_vcard_manager_class_init (GabbleVCardManagerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleVCardManagerPrivate));

  object_class->get_property = gabble_vcard_manager_get_property;
  object_class->set_property = gabble_vcard_manager_set_property;

  object_class->dispose = gabble_vcard_manager_dispose;
  object_class->finalize = gabble_vcard_manager_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this vCard lookup helper object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boolean ("have-self-avatar", "Have our own avatar",
      "TRUE after the local user's own vCard has been retrieved in order to "
      "get their initial avatar.", FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HAVE_SELF_AVATAR,
      param_spec);

  /* signal definitions */

  signals[NICKNAME_UPDATE] = g_signal_new ("nickname-update",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
        G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[VCARD_UPDATE] = g_signal_new ("vcard-update",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
        G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[GOT_SELF_INITIAL_AVATAR] = g_signal_new ("got-self-initial-avatar",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
gabble_vcard_manager_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_HAVE_SELF_AVATAR:
      g_value_set_boolean (value, priv->have_self_avatar);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_vcard_manager_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

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

static void delete_request (GabbleVCardManagerRequest *request);
static void cancel_request (GabbleVCardManagerRequest *request);
static void cancel_all_edit_requests (GabbleVCardManager *manager);

static gint
cache_entry_compare (gconstpointer a, gconstpointer b)
{
  const GabbleVCardCacheEntry *foo = a;
  const GabbleVCardCacheEntry *bar = b;
  return foo->expires - bar->expires;
}

static void
cache_entry_free (gpointer data)
{
  GabbleVCardCacheEntry *entry = data;
  GabbleVCardManagerPrivate *priv = entry->manager->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_assert (entry != NULL);

  while (entry->pending_requests)
    {
      cancel_request (entry->pending_requests->data);
    }

  if (entry->pipeline_item)
    {
      gabble_request_pipeline_item_cancel (entry->pipeline_item);
    }

  tp_clear_pointer (&entry->vcard_node, wocky_node_free);

  tp_handle_unref (contact_repo, entry->handle);

  g_slice_free (GabbleVCardCacheEntry, entry);
}

static GabbleVCardCacheEntry *
cache_entry_get (GabbleVCardManager *manager, TpHandle handle)
{
  GabbleVCardManagerPrivate *priv = manager->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);
  GabbleVCardCacheEntry *entry;

  entry = g_hash_table_lookup (priv->cache, GUINT_TO_POINTER (handle));
  if (entry)
     return entry;

  entry  = g_slice_new0 (GabbleVCardCacheEntry);

  entry->manager = manager;
  entry->handle = handle;
  tp_handle_ref (contact_repo, handle);
  g_hash_table_insert (priv->cache, GUINT_TO_POINTER (handle), entry);

  return entry;
}

static gboolean
cache_entry_timeout (gpointer data)
{
  GabbleVCardManager *manager = data;
  GabbleVCardManagerPrivate *priv = manager->priv;
  GabbleVCardCacheEntry *entry;

  time_t now = time (NULL);

  while (NULL != (entry = tp_heap_peek_first (priv->timed_cache)))
    {
      if (entry->expires > now)
          break;

      /* shouldn't have in-flight request nor any pending requests */
      g_assert (entry->pipeline_item == NULL);

      gabble_vcard_manager_invalidate_cache (manager, entry->handle);
    }

  priv->cache_timer = 0;

  if (entry)
    {
      priv->cache_timer = g_timeout_add_seconds (
          entry->expires - time (NULL), cache_entry_timeout, manager);
    }

  return FALSE;
}


static void
cache_entry_attempt_to_free (GabbleVCardCacheEntry *entry)
{
  GabbleVCardManagerPrivate *priv = entry->manager->priv;
  TpBaseConnection *base = (TpBaseConnection *) priv->connection;

  if (entry->vcard_node != NULL)
    {
      DEBUG ("Not freeing vCard cache entry %p: it has a cached vCard %p",
          entry, entry->vcard_node);
      return;
    }

  if (entry->pipeline_item != NULL)
    {
      DEBUG ("Not freeing vCard cache entry %p: it has a pipeline_item %p",
          entry, entry->pipeline_item);
      return;
    }

  if (entry->pending_requests != NULL)
    {
      DEBUG ("Not freeing vCard cache entry %p: it has pending requests",
          entry);
      return;
    }

  /* If there is a suspended request, it must be in entry-> pending_requests
   */
  g_assert (entry->suspended_timer_id == 0);

  if (entry->handle == base->self_handle)
    {
      /* if we do have some pending edits, we should also have
       * some pipeline items or pending requests */
      g_assert (priv->edit_pipeline_item || priv->edits == NULL);
    }

  tp_heap_remove (priv->timed_cache, entry);

  g_hash_table_remove (priv->cache, GUINT_TO_POINTER (entry->handle));
}

void
gabble_vcard_manager_invalidate_cache (GabbleVCardManager *manager,
                                       TpHandle handle)
{
  GabbleVCardManagerPrivate *priv = manager->priv;
  GabbleVCardCacheEntry *entry = g_hash_table_lookup (priv->cache,
      GUINT_TO_POINTER (handle));
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  if (!entry)
      return;

  tp_heap_remove (priv->timed_cache, entry);

  tp_clear_pointer (&entry->vcard_node, wocky_node_free);

  cache_entry_attempt_to_free (entry);
}

static void complete_one_request (GabbleVCardManagerRequest *request,
    WockyNode *vcard_node, GError *error);

static void
cache_entry_complete_requests (GabbleVCardCacheEntry *entry, GError *error)
{
  GSList *cur, *tmp;

  tmp = g_slist_copy (entry->pending_requests);

  for (cur = tmp; cur != NULL; cur = cur->next)
    {
      GabbleVCardManagerRequest *request = cur->data;

      complete_one_request (request, error ? NULL : entry->vcard_node, error);
    }

  g_slist_free (tmp);
}

static void
complete_one_request (GabbleVCardManagerRequest *request,
                      WockyNode *vcard_node,
                      GError *error)
{
  if (request->callback)
    {
      (request->callback) (request->manager, request, request->entry->handle,
          vcard_node, error, request->user_data);
    }

  delete_request (request);
}

static void
disconnect_entry_foreach (gpointer handle, gpointer value, gpointer unused)
{
  GError err = { TP_ERRORS, TP_ERROR_DISCONNECTED, "Connection closed" };
  GabbleVCardCacheEntry *entry = value;

  if (entry->suspended_timer_id)
    {
      g_source_remove (entry->suspended_timer_id);
      entry->suspended_timer_id = 0;
    }

  cache_entry_complete_requests (entry, &err);

  if (entry->pipeline_item)
    {
      gabble_request_pipeline_item_cancel (entry->pipeline_item);
      entry->pipeline_item = NULL;
    }
}

static void
gabble_vcard_manager_dispose (GObject *object)
{
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  DEBUG ("%p", object);

  if (priv->edits != NULL)
    {
      g_list_foreach (priv->edits,
          (GFunc) gabble_vcard_manager_edit_info_free, NULL);
      g_list_free (priv->edits);
    }

  priv->edits = NULL;

  if (priv->cache_timer)
      g_source_remove (priv->cache_timer);

  g_hash_table_foreach (priv->cache, disconnect_entry_foreach, NULL);

  tp_heap_destroy (priv->timed_cache);
  g_hash_table_unref (priv->cache);

  if (priv->edit_pipeline_item)
      gabble_request_pipeline_item_cancel (priv->edit_pipeline_item);

  cancel_all_edit_requests (self);

  if (G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->dispose (object);
}

static void
gabble_vcard_manager_finalize (GObject *object)
{
  DEBUG ("%p", object);
  G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->finalize (object);
}

gchar *
vcard_get_avatar_sha1 (WockyNode *vcard)
{
  gchar *sha1;
  const gchar *binval_value;
  GString *avatar;
  WockyNode *node;
  WockyNode *binval;

  node = wocky_node_get_child (vcard, "PHOTO");

  if (!node)
    return g_strdup ("");

  DEBUG ("Our vCard has a PHOTO %p", node);
  binval = wocky_node_get_child (node, "BINVAL");

  if (!binval)
    return g_strdup ("");

  binval_value = binval->content;

  if (!binval_value)
    return g_strdup ("");

  avatar = base64_decode (binval_value);

  if (avatar)
    {
      sha1 = sha1_hex (avatar->str, avatar->len);
      g_string_free (avatar, TRUE);
      DEBUG ("Successfully decoded PHOTO.BINVAL, SHA-1 %s", sha1);
    }
  else
    {
      DEBUG ("Avatar is in garbled Base64, ignoring it!");
      sha1 = g_strdup ("");
    }

  return sha1;
}

/* Called during connection. */
static void
initial_request_cb (GabbleVCardManager *self,
                    GabbleVCardManagerRequest *request,
                    TpHandle handle,
                    WockyNode *vcard,
                    GError *error,
                    gpointer user_data)
{
  GabbleVCardManagerPrivate *priv = self->priv;
  gchar *alias = user_data;
  gchar *sha1;

  if (vcard)
    {
      /* We now have our own avatar (or lack thereof) so can answer
       * GetAvatarTokens([self_handle])
       */
      priv->have_self_avatar = TRUE;

      /* Do we have an avatar already? If so, the presence cache ought to be
       * told (anyone else's avatar SHA-1 we'd get from their presence,
       * but unless we have another XEP-0153 resource connected, we never
       * see our own presence)
       */
      sha1 = vcard_get_avatar_sha1 (vcard);
      g_signal_emit (self, signals[GOT_SELF_INITIAL_AVATAR], 0, sha1);
      g_free (sha1);
    }

  g_free (alias);
}

static void
status_changed_cb (GObject *object,
                   guint status,
                   guint reason,
                   gpointer user_data)
{
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (user_data);
  GabbleVCardManagerPrivate *priv = self->priv;
  GabbleConnection *conn = GABBLE_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
      gchar *alias;
      GabbleConnectionAliasSource alias_src;

      /* if we have a better alias, patch it into our vCard on the server */
      alias_src = _gabble_connection_get_cached_alias (conn,
                                                       base->self_handle,
                                                       &alias);

      if (alias_src >= GABBLE_CONNECTION_ALIAS_FROM_VCARD)
        {
          priv->edits = g_list_append (priv->edits,
              gabble_vcard_manager_edit_info_new (NULL, alias,
                  GABBLE_VCARD_EDIT_SET_ALIAS, NULL));
        }

      g_free (alias);

      /* FIXME: we happen to know that synchronous errors can't happen */
      gabble_vcard_manager_request (self, base->self_handle, 0,
          initial_request_cb, NULL, (GObject *) self);
    }
}

/**
 * gabble_vcard_manager_new:
 * @conn: The #GabbleConnection to use for vCard lookup
 *
 * Creates an object to use for Jabber vCard lookup (JEP 0054).
 * There should be one of these per connection
 */
GabbleVCardManager *
gabble_vcard_manager_new (GabbleConnection *conn)
{
  GabbleVCardManager *self;

  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  self = GABBLE_VCARD_MANAGER (g_object_new (GABBLE_TYPE_VCARD_MANAGER,
        "connection", conn, NULL));
  g_signal_connect (conn, "status-changed",
                    G_CALLBACK (status_changed_cb), self);
  return self;
}

static void notify_delete_request (gpointer data, GObject *obj);
static void notify_delete_edit_request (gpointer data, GObject *obj);

static void
delete_request (GabbleVCardManagerRequest *request)
{
  GabbleVCardManager *manager = request->manager;

  DEBUG ("Discarding request %p", request);

  g_assert (NULL != request);
  g_assert (NULL != manager);
  g_assert (NULL != request->entry);
  g_assert (GABBLE_IS_VCARD_MANAGER (manager));

  /* poison the request, so assertions about it will fail if there's a
   * dangling reference */
  request->manager = NULL;

  request->entry->pending_requests = g_slist_remove
      (request->entry->pending_requests, request);
  cache_entry_attempt_to_free (request->entry);

  if (NULL != request->bound_object)
    {
      g_object_weak_unref (request->bound_object, notify_delete_request,
          request);
    }

  if (0 != request->timer_id)
    {
      g_source_remove (request->timer_id);
    }

  g_slice_free (GabbleVCardManagerRequest, request);
}

static gboolean
timeout_request (gpointer data)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest *) data;

  g_return_val_if_fail (data != NULL, FALSE);
  DEBUG ("Request %p timed out, notifying callback %p",
         request, request->callback);

  request->timer_id = 0;

  /* The pipeline machinery will call our callback with the error "canceled"
   */
  gabble_request_pipeline_item_cancel (request->entry->pipeline_item);

  return FALSE;
}

static void
cancel_request (GabbleVCardManagerRequest *request)
{
  GError err = { GABBLE_VCARD_MANAGER_ERROR,
      GABBLE_VCARD_MANAGER_ERROR_CANCELLED, "Request cancelled" };

  g_assert (request != NULL);

  DEBUG ("Request %p cancelled, notifying callback %p",
         request, request->callback);

  complete_one_request (request, NULL, &err);
}

static gchar *
extract_nickname (WockyNode *vcard_node)
{
  WockyNode *node;
  const gchar *nick;

  node = wocky_node_get_child (vcard_node, "NICKNAME");

  if (node == NULL)
    return NULL;

  nick = node->content;

  return g_strdup (nick);
}

static void
observe_vcard (GabbleConnection *conn,
               GabbleVCardManager *manager,
               TpHandle handle,
               WockyNode *vcard_node)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *field = "<NICKNAME>";
  gchar *alias;
  const gchar *old_alias;

  alias = extract_nickname (vcard_node);

  if (alias == NULL)
    {
      WockyNode *fn_node = wocky_node_get_child (vcard_node, "FN");

      if (fn_node != NULL)
        {
          const gchar *fn = fn_node->content;

          if (!tp_str_empty (fn))
            {
              field = "<FN>";
              alias = g_strdup (fn);
            }
        }
    }

  g_signal_emit (G_OBJECT (manager), signals[VCARD_UPDATE], 0, handle);

  old_alias = gabble_vcard_manager_get_cached_alias (manager, handle);

  if (old_alias != NULL && !tp_strdiff (old_alias, alias))
    {
      DEBUG ("no change to vCard alias \"%s\" for handle %u", alias, handle);

      g_free (alias);
      return;
    }

  if (alias != NULL)
    {
      DEBUG ("got vCard alias \"%s\" for handle %u from %s", alias,
          handle, field);

      /* takes ownership of alias */
      tp_handle_set_qdata (contact_repo, handle,
          gabble_vcard_manager_cache_quark (), alias, g_free);
    }
  else
    {
      DEBUG ("got no vCard alias for handle %u", handle);

      tp_handle_set_qdata (contact_repo, handle,
          gabble_vcard_manager_cache_quark (), (gchar *) NO_ALIAS, NULL);
    }

  if ((old_alias != NULL) || (alias != NULL))
      g_signal_emit (G_OBJECT (manager), signals[NICKNAME_UPDATE], 0, handle);
}

/* Called when a pre-set get request failed, or when a set request succeeded
 * or failed.
 */
static void
replace_reply_cb (GabbleConnection *conn,
                  WockyStanza *reply_msg,
                  gpointer user_data,
                  GError *error)
{
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (user_data);
  GabbleVCardManagerPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GList *li;
  WockyNode *node = NULL;

  /* If we sent a SET request, it's dead now. */
  priv->edit_pipeline_item = NULL;

  DEBUG ("called: %s error", (error) ? "some" : "no");

  if (error)
    {
      /* We won't need our patched vcard after all */
      tp_clear_pointer (&priv->patched_vcard, wocky_node_free);
    }
  else
    {
      GabbleVCardCacheEntry *entry = cache_entry_get (self, base->self_handle);

      /* We must have patched vcard by now */
      g_assert (priv->patched_vcard != NULL);

      /* Finally we may put the new vcard in the cache. */
      tp_clear_pointer (&entry->vcard_node, wocky_node_free);

      entry->vcard_node = priv->patched_vcard;
      priv->patched_vcard = NULL;

      /* observe it so we pick up alias updates */
      observe_vcard (conn, self, base->self_handle, entry->vcard_node);

      node = entry->vcard_node;
    }

  /* Scan all edit requests, call and remove ones whose data made it
   * into SET request that just returned. */
  li = priv->edit_requests;
  while (li)
    {
      GabbleVCardManagerEditRequest *req = li->data;
      li = g_list_next (li);
      if (req->set_in_pipeline || error)
        {
          if (req->callback)
            {
              (req->callback) (req->manager, req, node, error, req->user_data);
            }

          gabble_vcard_manager_remove_edit_request (req);
        }
    }

  if (error != NULL)
    {
      if (priv->edits != NULL)
        {
          /* All the requests for these edits have just been cancelled. */
          g_list_foreach (priv->edits,
              (GFunc) gabble_vcard_manager_edit_info_free, NULL);
          g_list_free (priv->edits);
          priv->edits = NULL;
        }
    }
  else
    {
      /* If we've received more edit requests in the meantime, send them off.
       */
      manager_patch_vcard (self, node);
    }
}

/* This function must return TRUE for any significant change, but may also
 * return TRUE for insignificant changes, as long as they aren't commonly done
 * (NICKNAME, PHOTO and in future FN are the problematic ones). */
static gboolean
gabble_vcard_manager_replace_is_significant (GabbleVCardManagerEditInfo *info,
    WockyNode *old_vcard)
{
  gboolean seen = FALSE;
  WockyNodeIter i;
  WockyNode *node;

  /* Find the first node matching the one we want to edit */
  wocky_node_iter_init (&i, old_vcard, info->element_name, NULL);
  while (wocky_node_iter_next (&i, &node))
    {
      const gchar *value;
      const gchar *new_value;

      /* if there are >= 2 copies of this field, we're going to reduce that
       * to 1 */
      if (seen)
        return TRUE;

      /* consider NULL and "" to be different representations for the
       * same thing */
      value = node->content;
      new_value = info->element_value;

      if (value == NULL)
        value = "";

      if (new_value == NULL)
        new_value = "";

      if (tp_strdiff (value, new_value))
        return TRUE;

      /* we assume that a change to child nodes is always significant,
       * unless it's the <PHOTO/> */
      if (!tp_strdiff (node->name, "PHOTO"))
        {
          /* For the special case of PHOTO, we know that the child nodes
           * are only meant to appear once, so we can be more aggressive
           * about avoiding unnecessary edits: assume that the PHOTO on
           * the server doesn't have extra children, and that one matching
           * child is enough. */
          GList *child_iter;

          for (child_iter = info->children;
              child_iter != NULL;
              child_iter = child_iter->next)
            {
              GabbleVCardChild *child = child_iter->data;
              WockyNode *child_node = wocky_node_get_child (node,
                  child->key);

              if (child_node == NULL ||
                  tp_strdiff (child_node->content,
                    child->value))
                {
                  return TRUE;
                }
            }
        }
      else
        {
          if (info->children != NULL)
            return TRUE;
        }
    }

  /* if there are no copies of this field, we're going to add one; otherwise,
   * seen == TRUE implies we've seen exactly one copy, and it matched what
   * we want */
  return !seen;
}

static WockyNode *vcard_copy (WockyNode *parent, WockyNode *src,
    const gchar *exclude, gboolean *exclude_mattered);

static WockyStanza *
gabble_vcard_manager_edit_info_apply (GabbleVCardManagerEditInfo *info,
    WockyNode *old_vcard,
    GabbleVCardManager *vcard_manager)
{
  WockyStanza *msg;
  WockyNode *vcard_node;
  WockyNode *node;
  GList *iter;
  gboolean maybe_changed = FALSE;
  GabbleConnection *conn = vcard_manager->priv->connection;
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (info->edit_type == GABBLE_VCARD_EDIT_SET_ALIAS)
    {
      /* SET_ALIAS is shorthand for a REPLACE operation or nothing */

      g_assert (info->element_name == NULL);

      if (gabble_vcard_manager_can_use_vcard_field (vcard_manager, "NICKNAME"))
        {
          info->element_name = g_strdup ("NICKNAME");
        }
      else
        {
          /* Google Talk servers won't let us set a NICKNAME; recover by
           * setting the FN */
          info->element_name = g_strdup ("FN");
        }

      if (info->element_value == NULL)
        {
          /* We're just trying to fix a possibly-incomplete SetContactInfo() -
           * */
          gchar *alias;

          node = wocky_node_get_child (old_vcard, info->element_name);

          /* If the user has set this field explicitly via SetContactInfo(),
           * that takes precedence */
          if (node != NULL)
            return NULL;

          if (_gabble_connection_get_cached_alias (conn, base->self_handle,
                &alias) < GABBLE_CONNECTION_ALIAS_FROM_VCARD)
            {
              /* not good enough to want to put it in the vCard */
              g_free (alias);
              return NULL;
            }

          info->element_value = alias;
        }

      info->edit_type = GABBLE_VCARD_EDIT_REPLACE;
    }

  if (info->edit_type == GABBLE_VCARD_EDIT_APPEND ||
      info->edit_type == GABBLE_VCARD_EDIT_REPLACE)
    {
      if (!gabble_vcard_manager_can_use_vcard_field (vcard_manager,
            info->element_name))
        {
          DEBUG ("ignoring vcard node %s because this server doesn't "
              "support it", info->element_name);
          return NULL;
        }
    }

  /* A special case for replacing one field with another: we detect no-op
   * changes more actively, because we make changes of this type quite
   * frequently (on every login), and as well as wasting bandwidth, setting
   * the vCard too often can cause a memory leak in OpenFire (see fd.o#25341).
   */
  if (info->edit_type == GABBLE_VCARD_EDIT_REPLACE &&
      ! gabble_vcard_manager_replace_is_significant (info, old_vcard))
    {
      DEBUG ("ignoring no-op vCard %s replacement", info->element_name);
      return NULL;
    }

  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      NULL, NULL, NULL);

  if (info->edit_type == GABBLE_VCARD_EDIT_CLEAR)
    {
      /* start from a clean slate... */
      vcard_node = wocky_node_add_child_with_content (
          wocky_stanza_get_top_node (msg), "vCard", "");
      vcard_node->ns = g_quark_from_string ("vcard-temp");

      /* ... but as a special case, the photo gets copied in from the old
       * vCard, because SetContactInfo doesn't touch photos */
      node = wocky_node_get_child (old_vcard, "PHOTO");

      if (node != NULL)
        vcard_copy (vcard_node, node, NULL, NULL);

      /* Yes, we can do this: "WockyNode" is really a WockyNode */
      if (wocky_node_equal (old_vcard, vcard_node))
        {
          /* nothing actually happened, forget it */
          g_object_unref (msg);
          return NULL;
        }

      return msg;
    }

  if (info->edit_type == GABBLE_VCARD_EDIT_APPEND)
    {
      /* appending: keep all child nodes */
      vcard_node = vcard_copy (
          wocky_stanza_get_top_node (msg), old_vcard, NULL, NULL);
    }
  else
    {
      /* replacing or deleting: exclude all matching child nodes from
       * copying */
      vcard_node = vcard_copy (
          wocky_stanza_get_top_node (msg), old_vcard, info->element_name,
          &maybe_changed);
    }

  if (info->edit_type != GABBLE_VCARD_EDIT_DELETE)
    {
      maybe_changed = TRUE;

      node = wocky_node_add_child_with_content (vcard_node,
          info->element_name, info->element_value);

      for (iter = info->children; iter != NULL; iter = iter->next)
        {
          GabbleVCardChild *child = iter->data;

          wocky_node_add_child_with_content (node, child->key, child->value);
        }
    }

  if ((!maybe_changed) || wocky_node_equal (old_vcard, vcard_node))
    {
      /* nothing actually happened, forget it */
      g_object_unref (msg);
      return NULL;
    }

  return msg;
}

/* Loudmouth hates me. The feelings are mutual.
 *
 * Note that this function doesn't copy any attributes other than
 * xmlns, because LM provides no way to iterate over attributes. Thanks, LM. */
static WockyNode *
vcard_copy (WockyNode *parent,
    WockyNode *src,
    const gchar *exclude,
    gboolean *exclude_mattered)
{
    WockyNode *new = wocky_node_add_child_with_content (parent, src->name,
        src->content);
    const gchar *xmlns;
    WockyNodeIter i;
    WockyNode *child;

    xmlns = wocky_node_get_ns (src);
    if (xmlns != NULL)
      new->ns = g_quark_from_string (xmlns);

    wocky_node_iter_init (&i, src, NULL, NULL);
    while (wocky_node_iter_next (&i, &child))
      {

        if (tp_strdiff (child->name, exclude))
          {
            vcard_copy (new, child, NULL, NULL);
          }
        else
          {
            if (exclude_mattered != NULL)
              *exclude_mattered = TRUE;
          }
      }

    return new;
}

static void
manager_patch_vcard (GabbleVCardManager *self,
                     WockyNode *vcard_node)
{
  GabbleVCardManagerPrivate *priv = self->priv;
  WockyStanza *msg = NULL;
  GList *li;

  /* Bail out if we don't have outstanding edits to make, or if we already
   * have a set request in progress.
   */
  if (priv->edits == NULL || priv->edit_pipeline_item != NULL)
      return;

  /* Apply any unsent edits to the patched vCard */
  for (li = priv->edits; li != NULL; li = li->next)
    {
      WockyStanza *new_msg = gabble_vcard_manager_edit_info_apply (
          li->data, vcard_node, self);

      /* edit_info_apply returns NULL if nothing happened */
      if (new_msg == NULL)
        continue;

      tp_clear_pointer (&msg, g_object_unref);

      msg = new_msg;
      /* gabble_vcard_manager_edit_info_apply always returns an IQ message
       * with one vCard child */
      vcard_node = wocky_node_get_child (
          wocky_stanza_get_top_node (msg), "vCard");
      g_assert (vcard_node != NULL);
    }

  if (msg == NULL)
    {
      DEBUG ("nothing really changed, not updating vCard");
      goto out;
    }

  DEBUG("patching vcard");

  /* We'll save the patched vcard, and if the server says
   * we're ok, put it into the cache. But we want to leave the
   * original vcard in the cache until that happens. */
  priv->patched_vcard = copy_node (vcard_node);

  priv->edit_pipeline_item = gabble_request_pipeline_enqueue (
      priv->connection->req_pipeline, msg, default_request_timeout,
      replace_reply_cb, self);

  g_object_unref (msg);

out:
  /* We've applied those, forget about them */
  g_list_foreach (priv->edits, (GFunc) gabble_vcard_manager_edit_info_free,
      NULL);
  g_list_free (priv->edits);
  priv->edits = NULL;

  /* Current edit requests are in the pipeline, remember it so we
   * know which ones we may complete when the SET returns */
  for (li = priv->edit_requests; li; li = g_list_next (li))
    {
      GabbleVCardManagerEditRequest *edit = (GabbleVCardManagerEditRequest *) li->data;
      edit->set_in_pipeline = TRUE;
    }
}

static gboolean
suspended_request_timeout_cb (gpointer data)
{
  GabbleVCardManagerRequest *request = data;

  /* Send the request again */
  request->entry->suspended_timer_id = 0;
  request_send (request, request->timeout);

  return FALSE;
}

static gboolean
is_item_not_found (const GError *error)
{
  return (error->domain == WOCKY_XMPP_ERROR &&
      error->code == WOCKY_XMPP_ERROR_ITEM_NOT_FOUND);
}

/* Called when a GET request in the pipeline has either succeeded or failed. */
static void
pipeline_reply_cb (GabbleConnection *conn,
                   WockyStanza *reply_msg,
                   gpointer user_data,
                   GError *error)
{
  GabbleVCardManagerRequest *request = user_data;
  GabbleVCardCacheEntry *entry = request->entry;
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (entry->manager);
  GabbleVCardManagerPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  WockyNode *vcard_node = NULL;

  DEBUG("called for entry %p", entry);

  g_assert (tp_handle_is_valid (contact_repo, entry->handle, NULL));

  g_assert (entry->pipeline_item != NULL);
  g_assert (entry->suspended_timer_id == 0);

  entry->pipeline_item = NULL;

  /* XEP-0054 says that the server MUST return <item-not-found/> if you have no
   * vCard set, so we should treat that case identically to the server
   * returning success but with no <vCard/> node.
   */
  if (error != NULL && !is_item_not_found (error))
    {
      /* First, handle the error "wait": suspend the request and replay it
       * later */
      WockyXmppErrorType error_type = WOCKY_XMPP_ERROR_TYPE_CANCEL;
      GError *stanza_error = NULL;

      if (reply_msg != NULL &&
          wocky_stanza_extract_errors (reply_msg, &error_type, &stanza_error,
              NULL, NULL))
        {
          if (error_type == WOCKY_XMPP_ERROR_TYPE_WAIT)
            {
              DEBUG ("%s", g_quark_to_string (stanza_error->domain));
              DEBUG ("Retrieving %u's vCard returned a temporary <%s/> error; "
                  "trying againg in %u seconds", entry->handle,
                  wocky_xmpp_stanza_error_to_string (stanza_error),
                  request_wait_delay);

              g_source_remove (request->timer_id);
              request->timer_id = 0;

              entry->suspended_timer_id = g_timeout_add_seconds (
                  request_wait_delay, suspended_request_timeout_cb, request);

              g_error_free (stanza_error);
              return;
            }

          g_error_free (stanza_error);
        }

      /* If request for our own vCard failed, and we do have
       * pending edits to make, cancel those and return error
       * to the user */
      if (entry->handle == base->self_handle && priv->edits != NULL)
        {
          /* We won't have a chance to apply those, might as well forget them */
          g_list_foreach (priv->edits,
              (GFunc) gabble_vcard_manager_edit_info_free, NULL);
          g_list_free (priv->edits);
          priv->edits = NULL;

          replace_reply_cb (conn, reply_msg, self, error);
        }

      /* Complete pending GET requests */
      cache_entry_complete_requests (entry, error);
      return;
    }

  g_assert (reply_msg != NULL);

  vcard_node = wocky_node_get_child (
      wocky_stanza_get_top_node (reply_msg), "vCard");

  if (NULL == vcard_node)
    {
      /* We need a vCard node for the current API */
      DEBUG ("successful lookup response contained no <vCard> node, "
          "creating an empty one");

      vcard_node = wocky_node_add_child_with_content (
          wocky_stanza_get_top_node (reply_msg), "vCard",
          NULL);
      vcard_node->ns = g_quark_from_string (NS_VCARD_TEMP);
    }

  /* Put the message in the cache */
  entry->vcard_node = copy_node (vcard_node);

  entry->expires = time (NULL) + VCARD_CACHE_ENTRY_TTL;
  tp_heap_add (priv->timed_cache, entry);
  if (priv->cache_timer == 0)
    {
      GabbleVCardCacheEntry *first =
          tp_heap_peek_first (priv->timed_cache);

      priv->cache_timer = g_timeout_add_seconds (
          first->expires - time (NULL), cache_entry_timeout, self);
    }

  /* We have freshly updated cache for our vCard, edit it if
   * there are any pending edits and no outstanding set request.
   */
  if (entry->handle == base->self_handle)
    {
      manager_patch_vcard (self, vcard_node);
    }

  /* Observe the vCard as it goes past */
  observe_vcard (priv->connection, self, entry->handle, vcard_node);

  /* Complete all pending requests successfully */
  cache_entry_complete_requests (entry, NULL);
}

static void
notify_delete_request (gpointer data, GObject *obj)
{
  GabbleVCardManagerRequest *request = data;

  request->bound_object = NULL;
  delete_request (request);
}

static void
request_send (GabbleVCardManagerRequest *request, guint timeout)
{
  GabbleVCardCacheEntry *entry = request->entry;
  GabbleConnection *conn = entry->manager->priv->connection;
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  g_assert (request->timer_id == 0);

  if (entry->pipeline_item)
    {
      DEBUG ("adding to cache entry %p with <iq> already pending", entry);
    }
  else if (entry->suspended_timer_id != 0)
    {
      DEBUG ("adding to cache entry %p with <iq> suspended", entry);
    }
  else
    {
      const char *jid;
      WockyStanza *msg;

      request->timer_id =
          g_timeout_add_seconds (request->timeout, timeout_request, request);

      if (entry->handle == base->self_handle)
        {
          DEBUG ("Cache entry %p is my own, not setting @to", entry);
          jid = NULL;
        }
      else
        {
          jid = tp_handle_inspect (contact_repo, entry->handle);
          DEBUG ("Cache entry %p is not mine, @to = %s", entry, jid);
        }

      msg = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
          NULL, jid,
          '(', "vCard",
              ':', NS_VCARD_TEMP,
          ')',
          NULL);

      entry->pipeline_item = gabble_request_pipeline_enqueue (
          conn->req_pipeline, msg, timeout, pipeline_reply_cb, request);

      g_object_unref (msg);

      DEBUG ("adding request to cache entry %p and queueing the <iq>", entry);
    }
}

/* Request the vCard for the given handle. When it arrives, call the given
 * callback.
 *
 * The callback may be NULL if you just want the side-effect of this
 * operation, which is to update the cached alias.
 *
 * Note: this method assumes that vCard for the given handle is not available
 * already. Before using it either check that the vCard is not available
 * using gabble_vcard_manager_get_cached(), or explicitly invalidate the
 * cache using gabble_vcard_manager_invalidate_cache() to request cache
 * refresh.
 *
 * FIXME: the timeout is not always obeyed when there is already a request
 *        on the same handle. It should perhaps be removed.
 *
 * The connection must be connected.
 */
GabbleVCardManagerRequest *
gabble_vcard_manager_request (GabbleVCardManager *self,
                              TpHandle handle,
                              guint timeout,
                              GabbleVCardManagerCb callback,
                              gpointer user_data,
                              GObject *object)
{
  GabbleVCardManagerPrivate *priv = self->priv;
  TpBaseConnection *connection = (TpBaseConnection *) priv->connection;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      connection, TP_HANDLE_TYPE_CONTACT);
  GabbleVCardManagerRequest *request;
  GabbleVCardCacheEntry *entry = cache_entry_get (self, handle);

  g_return_val_if_fail (connection->status == TP_CONNECTION_STATUS_CONNECTED,
      NULL);
  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL), NULL);
  g_assert (entry->vcard_node == NULL);

  if (timeout == 0)
    timeout = default_request_timeout;

  request = g_slice_new0 (GabbleVCardManagerRequest);
  DEBUG ("Created request %p to retrieve <%u>'s vCard", request, handle);
  request->timeout = timeout;
  request->manager = self;
  request->entry = entry;
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  request->entry->pending_requests = g_slist_prepend
      (request->entry->pending_requests, request);

  request_send (request, timeout);
  return request;
}

/* Add a pending request to edit the vCard. When it finishes, call the given
 * callback. The callback may be NULL.
 *
 * The method takes over the ownership of the callers reference to \a edits and
 * its contents.
 *
 * The connection must be connected to call this method.
 */
GabbleVCardManagerEditRequest *
gabble_vcard_manager_edit (GabbleVCardManager *self,
                           guint timeout,
                           GabbleVCardManagerEditCb callback,
                           gpointer user_data,
                           GObject *object,
                           GList *edits)
{
  GabbleVCardManagerPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) priv->connection;
  GabbleVCardManagerEditRequest *req;
  GabbleVCardCacheEntry *entry;

  g_return_val_if_fail (base->status == TP_CONNECTION_STATUS_CONNECTED, NULL);

  /* Invalidate our current vCard and ensure that we're going to get
   * it in the near future */
  DEBUG ("called; invalidating cache");
  gabble_vcard_manager_invalidate_cache (self, base->self_handle);
  DEBUG ("checking if we have pending requests already");
  entry = cache_entry_get (self, base->self_handle);
  if (!priv->edit_pipeline_item && !entry->pending_requests)
    {
      DEBUG ("we don't, create one");
      /* create dummy GET request if neccessary */
      gabble_vcard_manager_request (self, base->self_handle, 0, NULL,
          NULL, NULL);
    }

  priv->edits = g_list_concat (priv->edits, edits);

  req = g_slice_new (GabbleVCardManagerEditRequest);
  req->manager = self;
  req->callback = callback;
  req->user_data = user_data;
  req->set_in_pipeline = FALSE;
  req->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_edit_request, req);

  priv->edit_requests = g_list_append (priv->edit_requests, req);
  return req;
}

void
gabble_vcard_manager_remove_edit_request (GabbleVCardManagerEditRequest *request)
{
  GabbleVCardManagerPrivate *priv = request->manager->priv;

  DEBUG("request == %p", request);

  g_return_if_fail (request != NULL);
  g_assert (NULL != g_list_find (priv->edit_requests, request));

  if (request->bound_object)
      g_object_weak_unref (request->bound_object, notify_delete_edit_request,
          request);

  g_slice_free (GabbleVCardManagerEditRequest, request);
  priv->edit_requests = g_list_remove (priv->edit_requests, request);
}

static void
notify_delete_edit_request (gpointer data, GObject *obj)
{
  GabbleVCardManagerEditRequest *request = data;

  DEBUG("request == %p", request);

  request->bound_object = NULL;
  gabble_vcard_manager_remove_edit_request (request);
}

static void
cancel_all_edit_requests (GabbleVCardManager *self)
{
  GabbleVCardManagerPrivate *priv = self->priv;
  GError cancelled = { GABBLE_VCARD_MANAGER_ERROR,
      GABBLE_VCARD_MANAGER_ERROR_CANCELLED,
      "Request cancelled" };

  while (priv->edit_requests)
    {
      GabbleVCardManagerEditRequest *req = priv->edit_requests->data;
      if (req->callback)
        {
          (req->callback) (req->manager, req, NULL,
              &cancelled, req->user_data);
        }

      gabble_vcard_manager_remove_edit_request (req);
    }
}


void
gabble_vcard_manager_cancel_request (GabbleVCardManager *self,
                                     GabbleVCardManagerRequest *request)
{
  g_return_if_fail (GABBLE_IS_VCARD_MANAGER (self));
  g_return_if_fail (NULL != request);
  g_return_if_fail (self == request->manager);

  cancel_request (request);
}

/**
 * Return cached message for the handle's vCard if it's available.
 */
gboolean
gabble_vcard_manager_get_cached (GabbleVCardManager *self,
                                 TpHandle handle,
                                 WockyNode **node)
{
  GabbleVCardManagerPrivate *priv = self->priv;
  GabbleVCardCacheEntry *entry = g_hash_table_lookup (priv->cache,
      GUINT_TO_POINTER (handle));
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  if ((entry == NULL) || (entry->vcard_node == NULL))
      return FALSE;

  if (node != NULL)
      *node = entry->vcard_node;

  return TRUE;
}

/**
 * Return the cached alias derived from the vCard for the given handle,
 * if any. If there is no cached alias, return NULL.
 */
const gchar *
gabble_vcard_manager_get_cached_alias (GabbleVCardManager *self,
                                       TpHandle handle)
{
  GabbleVCardManagerPrivate *priv;
  TpHandleRepoIface *contact_repo;
  const gchar *s;

  g_return_val_if_fail (GABBLE_IS_VCARD_MANAGER (self), NULL);

  priv = self->priv;
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL), NULL);

  s = tp_handle_get_qdata (contact_repo, handle,
      gabble_vcard_manager_cache_quark ());

  if (s == NO_ALIAS)
    s = NULL;

  return s;
}

/**
 * Return TRUE if we've tried looking up an alias for this handle before.
 */
gboolean
gabble_vcard_manager_has_cached_alias (GabbleVCardManager *self,
                                       TpHandle handle)
{
  GabbleVCardManagerPrivate *priv;
  TpHandleRepoIface *contact_repo;
  gpointer p;

  g_return_val_if_fail (GABBLE_IS_VCARD_MANAGER (self), FALSE);

  priv = self->priv;
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  p = tp_handle_get_qdata (contact_repo, handle,
      gabble_vcard_manager_cache_quark ());

  return p != NULL;
}

/* For unit tests only */
void
gabble_vcard_manager_set_suspend_reply_timeout (guint timeout)
{
  request_wait_delay = timeout;
}

void
gabble_vcard_manager_set_default_request_timeout (guint timeout)
{
  default_request_timeout = timeout;
}

GabbleVCardManagerEditInfo *
gabble_vcard_manager_edit_info_new (const gchar *element_name,
                                    const gchar *element_value,
                                    GabbleVCardEditType edit_type,
                                    ...)
{
  GabbleVCardManagerEditInfo *info;
  va_list ap;
  const gchar *key;
  const gchar *value;

  if (edit_type == GABBLE_VCARD_EDIT_DELETE)
    {
      const gchar *first_edit = NULL;

      g_return_val_if_fail (element_value == NULL, NULL);

      va_start (ap, edit_type);
      first_edit = va_arg (ap, const gchar *);
      va_end (ap);
      g_return_val_if_fail (first_edit == NULL, NULL);
    }

  info = g_slice_new (GabbleVCardManagerEditInfo);
  info->element_name = g_strdup (element_name);
  info->element_value = g_strdup (element_value);
  info->edit_type = edit_type;
  info->children = NULL;

  va_start (ap, edit_type);

  while ((key = va_arg (ap, const gchar *)))
    {
      value = va_arg (ap, const gchar *);
      gabble_vcard_manager_edit_info_add_child (info, key, value);
    }

  va_end (ap);

  return info;
}

void
gabble_vcard_manager_edit_info_add_child (
    GabbleVCardManagerEditInfo *edit_info,
    const gchar *key,
    const gchar *value)
{
  edit_info->children = g_list_append (edit_info->children,
      gabble_vcard_child_new (key, value));
}

void
gabble_vcard_manager_edit_info_free (GabbleVCardManagerEditInfo *info)
{
  g_free (info->element_name);
  g_free (info->element_value);
  g_list_foreach (info->children, (GFunc) gabble_vcard_child_free, NULL);
  g_list_free (info->children);
  g_slice_free (GabbleVCardManagerEditInfo, info);
}

gboolean
gabble_vcard_manager_has_limited_vcard_fields (GabbleVCardManager *self)
{
  if (self->priv->connection->features &
      GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER)
    return TRUE;

  return FALSE;
}

gboolean
gabble_vcard_manager_can_use_vcard_field (GabbleVCardManager *self,
    const gchar *field_name)
{
  if (self->priv->connection->features &
      GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER)
    {
      /* Google's server only allows N, FN and PHOTO */
      if (tp_strdiff (field_name, "N") &&
          tp_strdiff (field_name, "FN") &&
          tp_strdiff (field_name, "PHOTO"))
        {
          return FALSE;
        }
    }

  return TRUE;
}
