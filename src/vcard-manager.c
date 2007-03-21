/*
 * vcard-manager.c - Source for Gabble vCard lookup helper
 *
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

#include <telepathy-glib/dbus.h>

#define DEBUG_FLAG GABBLE_DEBUG_VCARD

#include "base64.h"
#include "debug.h"
#include "gabble-connection.h"
#include "handles.h"
#include "namespaces.h"
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/heap.h>
#include "handles.h"
#include "time.h"
#include "util.h"
#include "vcard-manager.h"

#define DEFAULT_REQUEST_TIMEOUT 20000
#define VCARD_CACHE_ENTRY_TTL 30
#define VCARD_PIPELINE_SIZE 3

static const gchar *NO_ALIAS = "none";

/* signal enum */
enum
{
    NICKNAME_UPDATE,
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

typedef struct _GabbleVCardManagerPrivate GabbleVCardManagerPrivate;
struct _GabbleVCardManagerPrivate
{
  GabbleConnection *connection;
  GSList *requests;
  GSList *request_pipeline;
  GSList *reqs_in_flight;

  GHashTable *cache;
  TpHeap *timed_cache;
  guint cache_timer;

  gboolean have_self_avatar;
  gboolean dispose_has_run;
};

struct _GabbleVCardManagerRequest
{
  GabbleVCardManager *manager;
  guint timer_id;
  guint timeout;

  TpHandle handle;
  gchar **edit_args;

  GabbleVCardManagerCb callback;
  gpointer user_data;
  GObject *bound_object;
};

struct _GabbleVCardCacheEntry
{
  GabbleVCardManager *manager;
  TpHandle handle;
  GabbleVCardManagerRequest *request;
  GSList *pending_requests;
  time_t received;
  LmMessage *message;
};

typedef struct _GabbleVCardPipelineItem GabbleVCardPipelineItem;
struct _GabbleVCardPipelineItem {
    GabbleVCardManagerRequest *request;
    LmMessage *msg;
    gpointer callback;
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

#define GABBLE_VCARD_MANAGER_GET_PRIVATE(o)     ((GabbleVCardManagerPrivate*)((o)->priv));

static void cache_entry_free (void *data);
static gint cache_entry_compare (gconstpointer a, gconstpointer b);

static void
gabble_vcard_manager_init (GabbleVCardManager *obj)
{
  GabbleVCardManagerPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_VCARD_MANAGER, GabbleVCardManagerPrivate);
  obj->priv = priv;

  priv->cache = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      cache_entry_free);
  priv->timed_cache = tp_heap_new (cache_entry_compare);
}

static void gabble_vcard_manager_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gabble_vcard_manager_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);
static void gabble_vcard_manager_dispose (GObject *object);
static void gabble_vcard_manager_finalize (GObject *object);

static void
gabble_vcard_manager_class_init (GabbleVCardManagerClass *gabble_vcard_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_vcard_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_vcard_manager_class, sizeof (GabbleVCardManagerPrivate));

  object_class->get_property = gabble_vcard_manager_get_property;
  object_class->set_property = gabble_vcard_manager_set_property;

  object_class->dispose = gabble_vcard_manager_dispose;
  object_class->finalize = gabble_vcard_manager_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "vCard lookup helper object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boolean ("have-self-avatar", "Have our own avatar",
                                     "TRUE after the local user's own vCard "
                                     "has been retrieved in order to get"
                                     "their initial avatar.",
                                     FALSE,
                                     G_PARAM_READABLE |
                                     G_PARAM_STATIC_NICK |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HAVE_SELF_AVATAR,
                                   param_spec);

  /* signal definitions */

  signals[NICKNAME_UPDATE] =
    g_signal_new ("nickname-update",
                  G_TYPE_FROM_CLASS (gabble_vcard_manager_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[GOT_SELF_INITIAL_AVATAR] =
    g_signal_new ("got-self-initial-avatar",
                  G_TYPE_FROM_CLASS (gabble_vcard_manager_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
gabble_vcard_manager_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleVCardManager *chan = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (chan);

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
gabble_vcard_manager_set_property (GObject     *object,
                                   guint        property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleVCardManager *chan = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void delete_request (GabbleVCardManagerRequest *request);
static void cancel_request (GabbleVCardManagerRequest *request);

static gint
cache_entry_compare (gconstpointer a, gconstpointer b)
{
  const GabbleVCardCacheEntry *foo = a;
  const GabbleVCardCacheEntry *bar = b;
  return foo->received - bar->received;
}

static void
cache_entry_free (void *data)
{
  GabbleVCardManagerPrivate *priv;
  TpHandleRepoIface *contact_repo;
  GabbleVCardCacheEntry *entry = (GabbleVCardCacheEntry *) data;

  g_assert (entry != NULL);

  while (entry->pending_requests)
    {
      cancel_request (entry->pending_requests->data);
    }

  if (entry->request)
    {
      delete_request (entry->request);
    }

  g_slist_free (entry->pending_requests);

  if (entry->message)
      lm_message_unref (entry->message);

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (entry->manager);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);

  tp_handle_unref (contact_repo, entry->handle);

  g_slice_free (GabbleVCardCacheEntry, entry);
}

static GabbleVCardCacheEntry *
cache_entry_get (GabbleVCardManager *manager, TpHandle handle)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);
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
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  GabbleVCardCacheEntry *entry;

  time_t now = time(NULL);

  while (NULL != (entry = tp_heap_peek_first (priv->timed_cache)))
    {
      if ((entry->received + VCARD_CACHE_ENTRY_TTL) > now)
          break;

      /* shouldn't have in-flight request nor any pending requests */
      g_assert (entry->request == NULL);

      gabble_vcard_manager_invalidate_cache (manager, entry->handle);
    }

  priv->cache_timer = 0;

  if (entry)
    {
      priv->cache_timer = g_timeout_add (
          1000 * (entry->received + VCARD_CACHE_ENTRY_TTL - time(NULL)),
          cache_entry_timeout, manager);
    }

  return FALSE;
}


static void
cache_entry_attempt_to_free (GabbleVCardManager *manager, TpHandle handle)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  GabbleVCardCacheEntry *entry = g_hash_table_lookup (priv->cache, GUINT_TO_POINTER (handle));

  if (!entry || entry->message || entry->pending_requests)
      return;

  tp_heap_remove (priv->timed_cache, entry);

  g_hash_table_remove (priv->cache, GUINT_TO_POINTER (entry->handle));
}

void
gabble_vcard_manager_invalidate_cache (GabbleVCardManager *manager, TpHandle handle)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  GabbleVCardCacheEntry *entry = g_hash_table_lookup (priv->cache, GUINT_TO_POINTER (handle));
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_if_fail (tp_handle_is_valid (contact_repo, handle, NULL));

  if (!entry)
      return;

  tp_heap_remove (priv->timed_cache, entry);

  if (entry->message)
    {
      lm_message_unref (entry->message);
      entry->message = NULL;
    }

  cache_entry_attempt_to_free (manager, handle);
}

static void
cache_entry_fill (GabbleVCardManager *manager, LmMessage *msg, TpHandle handle)
{
  GabbleVCardCacheEntry *entry = cache_entry_get (manager, handle);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);

  /* the upstream request should be finished by now */
  g_assert (entry->request == NULL);

  g_assert (entry->message == NULL);
  entry->message = lm_message_new ("", LM_MESSAGE_TYPE_IQ);
  lm_message_node_steal_children (entry->message->node, msg->node);
  entry->received = time(NULL);
  tp_heap_add (priv->timed_cache, entry);

  if (priv->cache_timer == 0)
    {
      GabbleVCardCacheEntry *first = tp_heap_peek_first (priv->timed_cache);
      priv->cache_timer = g_timeout_add ((first->received +
          VCARD_CACHE_ENTRY_TTL - time(NULL)) * 1000, cache_entry_timeout, manager);
    }
}

static void
complete_pending_requests (GabbleVCardManager *manager, TpHandle handle, GError *err)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  GabbleVCardCacheEntry *entry =
    g_hash_table_lookup (priv->cache, GUINT_TO_POINTER (handle));
  LmMessageNode *vcard_node = NULL;

  g_assert (entry);

  /* the upstream request should be finished by now */
  g_assert (entry->request == NULL);

  if (entry->message)
      vcard_node = lm_message_node_get_child (entry->message->node, "vCard");

  while (entry->pending_requests)
    {
      GabbleVCardManagerRequest *req = entry->pending_requests->data;

      if (req->callback)
        {
          DEBUG ("invoking callback %p for pending request %p (handle %u)",
              req->callback, req, handle);
          (req->callback)(req->manager, req, handle,
                              vcard_node, err, req->user_data);
        }
      delete_request (req);
    }

  cache_entry_attempt_to_free (manager, handle);

  if (err)
    g_error_free (err);
}

static void
complete_one_request (GabbleVCardManagerRequest *request, gpointer data, GError *err)
{
  GabbleVCardManager *manager = request->manager;
  TpHandle handle = request->handle;

  if (request->callback)
    {
      DEBUG ("invoking callback %p for request %p (handle %u)",
          request->callback, request, handle);
      (request->callback)(manager, request, handle,
                          data, err, request->user_data);
    }
  delete_request (request);

  cache_entry_attempt_to_free (manager, handle);

  if (err)
    g_error_free (err);
}

void
gabble_vcard_manager_dispose (GObject *object)
{
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  DBusGProxy *bus_proxy;
  bus_proxy = tp_get_bus_proxy ();

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->cache_timer)
      g_source_remove (priv->cache_timer);

  /* cancel request removes the element from the list after cancelling */
  while (priv->requests)
    cancel_request (priv->requests->data);

  tp_heap_destroy (priv->timed_cache);
  g_hash_table_destroy (priv->cache);

  /* should've been cleared by delete_request already */
  g_assert (priv->reqs_in_flight == NULL);
  g_assert (priv->request_pipeline == NULL);

  if (G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->dispose (object);
}

void
gabble_vcard_manager_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->finalize (object);
}

/* Called during connection. */
static void
initial_request_cb (GabbleVCardManager *self,
                    GabbleVCardManagerRequest *request,
                    TpHandle handle,
                    LmMessageNode *vcard,
                    GError *error,
                    gpointer user_data)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  gchar *alias = (gchar *)user_data;
  LmMessageNode *node;

  if (!vcard)
    {
      g_free (alias);
      return;
    }

  /* We now have our own avatar (or lack thereof) so can answer
   * GetAvatarTokens([self_handle])
   */
  priv->have_self_avatar = TRUE;

  /* try to patch the alias, if one was provided */
  if (alias)
    {
      DEBUG ("Patching our vCard to have alias \"%s\"", alias);
      node = lm_message_node_get_child (vcard, "NICKNAME");
      if (node)
        {
          lm_message_node_set_value (node, alias);
        }
      else
        {
          lm_message_node_add_child (vcard, "NICKNAME", alias);
        }

      /* basically ignore error here, there's not a lot we can do about it */
      gabble_vcard_manager_replace (self, vcard, 0, NULL, NULL,
                                    G_OBJECT (priv->connection), NULL);
    }

  /* Do we have an avatar already? If so, the presence cache ought to be
   * told (anyone else's avatar SHA-1 we'd get from their presence, 
   * but unless we have another XEP-0153 resource connected, we never
   * see our own presence)
   */
  node = lm_message_node_get_child (vcard, "PHOTO");
  if (node)
    {
      DEBUG ("Our vCard has a PHOTO %p", node);
      GString *avatar = NULL;
      LmMessageNode *binval = lm_message_node_get_child (node, "BINVAL");

      if (binval)
        {
          gchar *sha1;

          avatar = base64_decode (lm_message_node_get_value (binval));
          if (avatar)
            {
              sha1 = sha1_hex (avatar->str, avatar->len);
              DEBUG ("Successfully decoded PHOTO.BINVAL, SHA-1 %s", sha1);
              g_signal_emit (self, signals[GOT_SELF_INITIAL_AVATAR], 0, sha1);
              g_free (sha1);
            }
          else
            {
              DEBUG ("Avatar is in garbled Base64, ignoring it:\n%s",
                     lm_message_node_get_value (binval));
            }

          g_string_free (avatar, TRUE);
        }
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
  GabbleConnection *conn = GABBLE_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *)conn;

  if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
      gchar *alias;
      GabbleConnectionAliasSource alias_src;

      /* if we have a better alias, patch it into our vCard on the server */
      alias_src = _gabble_connection_get_cached_alias (conn,
                                                       base->self_handle,
                                                       &alias);
      if (alias_src < GABBLE_CONNECTION_ALIAS_FROM_VCARD)
        {
          /* this alias isn't reliable enough to want to patch it in */
          g_free (alias);
          alias = NULL;
        }

      /* fetch our vCard, and possibly patch it to include our new alias */
      gabble_vcard_manager_request (self, base->self_handle, 0,
                                    initial_request_cb, alias,
                                    G_OBJECT (conn), NULL);
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

  self = GABBLE_VCARD_MANAGER (g_object_new (GABBLE_TYPE_VCARD_MANAGER, "connection", conn, NULL));
  g_signal_connect (conn, "status-changed",
                    G_CALLBACK (status_changed_cb), self);
  return self;
}

static void notify_delete_request (gpointer data, GObject *obj);
static void vcard_pipeline_go (GabbleVCardManager *manager);

static void
delete_request (GabbleVCardManagerRequest *request)
{
  GabbleVCardManager *manager = request->manager;
  GabbleVCardManagerPrivate *priv;
  TpHandleRepoIface *contact_repo;
  GabbleVCardCacheEntry *entry;
  GSList *l;

  DEBUG ("Discarding request %p", request);

  g_assert (NULL != request);
  g_assert (GABBLE_IS_VCARD_MANAGER (manager));

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_assert (NULL != g_slist_find (priv->requests, request));

  priv->requests = g_slist_remove (priv->requests, request);

  for (l = priv->request_pipeline; l; l = g_slist_next (l))
    {
      GabbleVCardPipelineItem *item = l->data;
      if (item->request == request)
        {
          priv->request_pipeline = g_slist_remove (priv->request_pipeline, item);
          lm_message_unref (item->msg);
          g_free (item);
          break;
        }
    }

  if (g_slist_find (priv->reqs_in_flight, request))
    {
        priv->reqs_in_flight = g_slist_remove (priv->reqs_in_flight, request);
        vcard_pipeline_go (manager);
    }

  if (NULL != request->bound_object)
    {
      g_object_weak_unref (request->bound_object, notify_delete_request, request);
    }

  if (0 != request->timer_id)
    {
      g_source_remove (request->timer_id);
    }

  tp_handle_unref (contact_repo, request->handle);

  entry = g_hash_table_lookup (priv->cache, GUINT_TO_POINTER (request->handle));
  if (entry)
    {
      if (request == entry->request)
        {
          entry->request = NULL;
        }
      else
        {
          entry->pending_requests = g_slist_remove (entry->pending_requests, request);
        }
    }

  g_strfreev (request->edit_args);

  g_slice_free (GabbleVCardManagerRequest, request);
}

static gboolean
timeout_request (gpointer data)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest*) data;
  GError *err;
  g_return_val_if_fail (data != NULL, FALSE);

  err = g_error_new (GABBLE_VCARD_MANAGER_ERROR, GABBLE_VCARD_MANAGER_ERROR_TIMEOUT,
      "Request timed out");
  DEBUG ("Request %p timed out, notifying callback %p",
         request, request->callback);

  request->timer_id = 0;
  complete_one_request (request, NULL, err);
  return FALSE;
}

static void
cancel_request (GabbleVCardManagerRequest *request)
{
  GError *err;

  g_assert (request != NULL);

  err = g_error_new (GABBLE_VCARD_MANAGER_ERROR, GABBLE_VCARD_MANAGER_ERROR_CANCELLED,
      "Request cancelled");
  DEBUG ("Request %p cancelled, notifying callback %p",
         request, request->callback);

  complete_one_request (request, NULL, err);
}

static void
observe_vcard (GabbleConnection *conn, GabbleVCardManager *manager,
               TpHandle handle, LmMessageNode *vcard_node)
{
  LmMessageNode *nick_node = lm_message_node_get_child (vcard_node,
                                                        "NICKNAME");
  TpBaseConnection *base = (TpBaseConnection *)conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  DEBUG ("Observing vCard for %u", handle);
  //NODE_DEBUG(vcard_node, "their vCard is");

  if (nick_node)
    {
      const gchar *nick = lm_message_node_get_value (nick_node);

      DEBUG ("%u has <NICKNAME> \"%s\"", handle, nick ? nick : "(null)");

      if (nick && *nick)
        {
          /* nicknames are comma-separated, let's use the first one */
          gchar **bits = g_strsplit (nick, ",", 2);

          if (bits[0])
            {
              gchar *alias = g_strdup (bits[0]);

              DEBUG ("... using \"%s\" as their alias", alias);

              if (!tp_handle_set_qdata (contact_handles, handle,
                    gabble_vcard_manager_cache_quark(), alias, g_free))
                {
                  DEBUG ("failed to cache their alias");
                  g_free (alias);
                }

              g_signal_emit (G_OBJECT (manager), signals[NICKNAME_UPDATE],
                             0, handle);
            }

          g_strfreev (bits);
        }
    }
  else
    {
      const gchar *fn = NULL;
      /* let's see if they have a FN (formatted name) instead */
      nick_node = lm_message_node_get_child (vcard_node, "FN");
      if (nick_node)
        fn = lm_message_node_get_value (nick_node);
      DEBUG ("%u has no <NICKNAME>, but has <FN> \"%s\"", handle,
             fn ? fn : "(null)");
      if (fn && *fn)
        {
          gchar *alias = g_strdup (fn);

          DEBUG ("... using \"%s\" as their alias", alias);

          g_signal_emit (G_OBJECT (manager), signals[NICKNAME_UPDATE],
                         0, handle);
          if (!tp_handle_set_qdata (contact_handles,
                handle, gabble_vcard_manager_cache_quark(), alias, g_free))
            {
              DEBUG ("failed to cache their alias");
              g_free (alias);
            }
        }
      else
        {
          /* remember that they don't have an alias */
          if (!tp_handle_set_qdata (contact_handles,
                handle, gabble_vcard_manager_cache_quark (),
                (gchar *) NO_ALIAS, NULL))
            {
              DEBUG ("failed to cache their lack of vcard alias");
            }
        }

    }
}

static LmHandlerResult
replace_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                  LmMessage *reply_msg, GObject *object, gpointer user_data)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest*) user_data;
  GabbleVCardManager *manager = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)conn, TP_HANDLE_TYPE_CONTACT);
  GError *err = NULL;

  g_assert (request);

  DEBUG ("Replace request got a reply: conn@%p, sent_msg@%p, reply_msg@%p, "
         "bound object@%p, request@%p", conn, sent_msg, reply_msg, object,
         user_data);

  if (!g_slist_find (priv->requests, request))
    {
      DEBUG ("I don't care about that request any more");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  g_assert (tp_handle_is_valid (contact_repo, request->handle, NULL));

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      error_node = lm_message_node_get_child (reply_msg->node, "error");
      if (error_node)
        {
          err = gabble_xmpp_error_to_g_error (
              gabble_xmpp_error_from_node (error_node));
        }

      if (err == NULL)
        {
          err = g_error_new (GABBLE_VCARD_MANAGER_ERROR,
                             GABBLE_VCARD_MANAGER_ERROR_UNKNOWN,
                             "an unknown error occurred");
        }
    }

  DEBUG ("Request %p %s, notifying callback %p", request,
         err ? "failed" : "succeeded", request->callback);

  complete_one_request (request, sent_msg, err);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static GabbleVCardManagerRequest *request_send (GabbleVCardManagerRequest *request,
    LmMessageNode *replacement, const gchar *jid, GError **error);

static LmHandlerResult
request_reply_cb (GabbleConnection *conn,
                  LmMessage *sent_msg,
                  LmMessage *reply_msg,
                  GObject *object,
                  gpointer user_data)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest*) user_data;
  GabbleVCardManager *manager = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *vcard_node = NULL;
  GError *err = NULL;

  g_assert (request);

  DEBUG ("Fetch request got a reply: conn@%p, sent_msg@%p, reply_msg@%p, "
         "bound object@%p, request@%p", conn, sent_msg, reply_msg, object,
         user_data);

  if (!g_slist_find (priv->requests, request))
    {
      DEBUG ("I don't care about that request any more");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  g_assert (tp_handle_is_valid (contact_repo, request->handle, NULL));

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      vcard_node = NULL;

      error_node = lm_message_node_get_child (reply_msg->node, "error");
      if (error_node)
        {
          err = gabble_xmpp_error_to_g_error (
              gabble_xmpp_error_from_node (error_node));
        }

      if (err == NULL)
        {
          err = g_error_new (GABBLE_VCARD_MANAGER_ERROR,
                             GABBLE_VCARD_MANAGER_ERROR_UNKNOWN,
                             "an unknown error occurred");
        }
    }
  else
    {
      vcard_node = lm_message_node_get_child (reply_msg->node, "vCard");

      if (NULL == vcard_node)
        {
          DEBUG ("successful lookup response contained no <vCard> node, "
              "creating an empty one");

          vcard_node = lm_message_node_add_child (reply_msg->node, "vCard",
              NULL);
          lm_message_node_set_attribute (vcard_node, "xmlns", NS_VCARD_TEMP);
        }

      if (!request->edit_args)
        {
          observe_vcard (conn, manager, request->handle, vcard_node);
        }
      /* else we'll observe the vcard after editing it if we intend 
       * to modify it */
    }

  if (vcard_node && request->edit_args)
    {
      gchar **ptr;
      for (ptr = request->edit_args; *ptr; ptr++)
        {
          gchar *key = *ptr;
          gchar *value = *(++ptr);
          LmMessageNode *node;

          if (!value)
            {
              /* oops, someone passed in an odd number of args. */
              g_assert_not_reached ();
              break;
            }

          node = lm_message_node_get_child (vcard_node, key);
          if (node)
            {
              lm_message_node_set_value (node, value);
            }
          else
            {
              node = lm_message_node_add_child (vcard_node, key, value);
            }
        }

      observe_vcard (conn, manager, request->handle, vcard_node);

      /* XXX: Manually remove the request from the list of requests in flight,
       * so that it's not in there twice when we send it a second time. The
       * correct fix is to remove it from reqs_in_flight as soon as we get the
       * result IQ.
       */
      priv->reqs_in_flight = g_slist_remove (priv->reqs_in_flight, request);

      request_send (request, vcard_node, NULL, &err);

      if (err)
        {
          complete_one_request (request, NULL, err);
        }
      else
        {
          DEBUG ("Request %p fetch succeeded", request);
        }
    }
  else
    {
      GabbleVCardCacheEntry *entry =
          g_hash_table_lookup (priv->cache, GUINT_TO_POINTER (request->handle));

      if (entry)
        {
          DEBUG ("entry == %p, entry->request == %p, request == %p",
              entry, entry->request, request);
          g_assert (entry->request == request);

          entry->request = NULL;
          cache_entry_fill (manager, reply_msg, request->handle);
          complete_pending_requests (manager, request->handle, err);
          delete_request (request);
        }
      else
        {
          complete_one_request (request, vcard_node, err);
        }
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
vcard_pipeline_send_next_request (GabbleVCardManager *manager)
{
  GError *error = NULL;
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);

  if (priv->request_pipeline == NULL)
      return;

  GabbleVCardPipelineItem *item = priv->request_pipeline->data;
  priv->request_pipeline = g_slist_remove (priv->request_pipeline, item);

  DEBUG ("launching request %p", item->request);

  if (!_gabble_connection_send_with_reply (priv->connection, item->msg,
      item->callback, G_OBJECT(manager), item->request, &error))
    {
      GabbleVCardCacheEntry *entry =
          g_hash_table_lookup (priv->cache, GUINT_TO_POINTER (item->request->handle));
      if (entry && entry->request == item->request)
        {
          entry->request = NULL;
          complete_pending_requests (manager, item->request->handle, error);
          delete_request (item->request);
        }
      else
        {
          complete_one_request (item->request, NULL, error);
        }
      vcard_pipeline_send_next_request (manager);
    }
  else
    {
      priv->reqs_in_flight = g_slist_prepend (priv->reqs_in_flight, item->request);
    }

  lm_message_unref (item->msg);
  g_free (item);
}

static void
vcard_pipeline_go (GabbleVCardManager *manager)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);

  DEBUG ("called");

  while (priv->request_pipeline &&
      (g_slist_length (priv->reqs_in_flight) < VCARD_PIPELINE_SIZE))
    {
      vcard_pipeline_send_next_request (manager);
    }
}

static void
vcard_pipeline_enqueue (GabbleVCardManagerRequest *request, LmMessage *msg,
    gpointer callback)
{
  GabbleVCardManager *self = request->manager;
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  GabbleVCardPipelineItem *item = g_new0 (GabbleVCardPipelineItem, 1);

  item->request = request;
  item->msg = msg;
  item->callback = callback;

  DEBUG ("enqueue request %p", request);

  priv->request_pipeline = g_slist_append (priv->request_pipeline, item);
  if (request->timeout && (0 == request->timer_id))
    {
      request->timer_id =
          g_timeout_add (request->timeout, timeout_request, request);
    }
}

/* If @replacement is NULL sends a request, calling request_reply_cb when
 * it returns.
 *
 * Otherwise steals its children and sends an update, calling
 * replace_reply_cb when it returns.
 *
 * Frees the @request on error, returns it on success. */
static GabbleVCardManagerRequest *
request_send (GabbleVCardManagerRequest *request,
              LmMessageNode *replacement,
              const gchar *jid,
              GError **error)
{
  GabbleVCardManager *self = request->manager;
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);
  LmMessage *msg;
  LmMessageNode *lm_node;

  DEBUG ("Sending off request %p to %s for %s", request,
         replacement ? "replace vCard" : "retrieve vCard",
         jid ? jid : "myself");
  msg = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
                                           (replacement
                                                ? LM_MESSAGE_SUB_TYPE_SET
                                                : LM_MESSAGE_SUB_TYPE_GET));
  lm_node = lm_message_node_add_child (msg->node, "vCard", NULL);
  lm_message_node_set_attribute (lm_node, "xmlns", NS_VCARD_TEMP);

  if (replacement)
    lm_message_node_steal_children (lm_node, replacement);

  g_assert (tp_handle_is_valid (contact_repo, request->handle, NULL));

  vcard_pipeline_enqueue (request, msg,
      (replacement ? replace_reply_cb : request_reply_cb));

  vcard_pipeline_go (self);

  return request;
}

static void
notify_delete_request (gpointer data, GObject *obj)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest *) data;
  GabbleVCardManager *manager = request->manager;
  TpHandle handle = request->handle;

  request->bound_object = NULL;
  delete_request (request);
  cache_entry_attempt_to_free (manager, handle);
}

/* Request the vCard for the given handle. When it arrives, call the given
 * callback.
 *
 * The callback may be NULL if you just want the side-effect of this
 * operation, which is to update the cached alias.
 */
GabbleVCardManagerRequest *
gabble_vcard_manager_request (GabbleVCardManager *self,
                              TpHandle handle,
                              guint timeout,
                              GabbleVCardManagerCb callback,
                              gpointer user_data,
                              GObject *object,
                              GError **error)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *connection = (TpBaseConnection *)priv->connection;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      connection, TP_HANDLE_TYPE_CONTACT);
  GabbleVCardManagerRequest *request;
  GabbleVCardCacheEntry *entry;
  const gchar *jid;

  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL), NULL);

  if (timeout == 0)
    timeout = DEFAULT_REQUEST_TIMEOUT;

  request = g_slice_new0 (GabbleVCardManagerRequest);
  DEBUG ("Created request %p to retrieve <%u>'s vCard", request, handle);
  request->timeout = timeout;
  request->manager = self;
  tp_handle_ref (contact_repo, handle);
  request->handle = handle;
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  priv->requests = g_slist_prepend (priv->requests, request);
  if (handle == connection->self_handle) 
    {
      jid = NULL;
    }
  else
    {
      jid = tp_handle_inspect (contact_repo, handle);
    }

  entry = cache_entry_get (self, handle);
  if (entry->request)
    {
      DEBUG ("adding pending request to existing cache entry %p", entry);
      entry->pending_requests = g_slist_prepend (entry->pending_requests, request);
    }
  else
    {
      entry->request = g_slice_new0 (GabbleVCardManagerRequest);
      entry->request->manager = self;
      entry->request->handle = handle;
      tp_handle_ref (contact_repo, handle);
      priv->requests = g_slist_prepend (priv->requests, entry->request);

      entry->pending_requests = g_slist_prepend (entry->pending_requests, request);
      DEBUG ("adding the request to new entry %p and sending the request", entry);

      if (!request_send (entry->request, NULL, jid, error))
        {
          DEBUG ("some kind of error happened");

          delete_request (entry->request);
          entry->request = NULL;
          gabble_vcard_manager_invalidate_cache (self, entry->handle);

          delete_request (request);
          return NULL;
        }
    }

  return request;
}

GabbleVCardManagerRequest *
gabble_vcard_manager_replace (GabbleVCardManager *self,
                              LmMessageNode *replacement,
                              guint timeout,
                              GabbleVCardManagerCb callback,
                              gpointer user_data,
                              GObject *object,
                              GError **error)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *connection = (TpBaseConnection *)priv->connection;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      connection, TP_HANDLE_TYPE_CONTACT);
  GabbleVCardManagerRequest *request;

  if (timeout == 0)
    timeout = DEFAULT_REQUEST_TIMEOUT;

  request = g_slice_new0 (GabbleVCardManagerRequest);
  DEBUG ("Created request %p to replace my vCard, callback = %p",
         request, callback);
  request->timeout = timeout;
  request->manager = self;
  tp_handle_ref (contact_repo, connection->self_handle);
  request->handle = connection->self_handle;
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  priv->requests = g_slist_prepend (priv->requests, request);

  gabble_vcard_manager_invalidate_cache (self, request->handle);

  return request_send (request, replacement, NULL, error);
}

GabbleVCardManagerRequest *
gabble_vcard_manager_edit (GabbleVCardManager *self,
                           guint timeout,
                           GabbleVCardManagerCb callback,
                           gpointer user_data,
                           GObject *object,
                           GError **error,
                           ...)
{
  va_list ap;
  size_t i, argc;
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *connection = (TpBaseConnection *)priv->connection;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      connection, TP_HANDLE_TYPE_CONTACT);
  GabbleVCardManagerRequest *request;

  if (timeout == 0)
    timeout = DEFAULT_REQUEST_TIMEOUT;

  request = g_slice_new0 (GabbleVCardManagerRequest);
  DEBUG ("Created request %p to edit my vCard", request);
  request->timeout = timeout;
  request->manager = self;
  tp_handle_ref (contact_repo, connection->self_handle);
  request->handle = connection->self_handle;
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  priv->requests = g_slist_prepend (priv->requests, request);

  argc = 0;
  va_start (ap, error);
  while (va_arg (ap, const gchar *) != NULL)
    {
      argc++;
    }
  va_end (ap);
  g_return_val_if_fail (argc % 2 == 0, NULL);

  request->edit_args = g_new (gchar *, argc + 1);

  va_start (ap, error);
  for (i = 0; i < argc; i++)
    {
      request->edit_args[i] = g_strdup (va_arg (ap, const gchar *));
    }
  request->edit_args[argc] = NULL;
  va_end (ap);

  return request_send (request, NULL, NULL, error);
}

void
gabble_vcard_manager_cancel_request (GabbleVCardManager *manager,
                                     GabbleVCardManagerRequest *request)
{
  GabbleVCardManagerPrivate *priv;

  g_return_if_fail (GABBLE_IS_VCARD_MANAGER (manager));
  g_return_if_fail (NULL != request);

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);

  g_return_if_fail (NULL != g_slist_find (priv->requests, request));

  cancel_request (request);
}

/**
 * Return cached message for the handle's VCard if it's available.
 */
gboolean
gabble_vcard_manager_get_cached (GabbleVCardManager *self, TpHandle handle,
    LmMessageNode **node)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  GabbleVCardCacheEntry *entry = g_hash_table_lookup (priv->cache,
      GUINT_TO_POINTER (handle));
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  if ((entry == NULL) || (entry->message == NULL))
      return FALSE;

  if (node != NULL)
      *node = lm_message_node_get_child (entry->message->node, "vCard");

  return TRUE;
}

/**
 * Return the cached alias derived from the vCard for the given handle,
 * if any. If there is no cached alias, return NULL.
 */
const gchar *
gabble_vcard_manager_get_cached_alias (GabbleVCardManager *manager,
                                       TpHandle handle)
{
  GabbleVCardManagerPrivate *priv;
  TpHandleRepoIface *contact_repo;
  const gchar *s;

  g_return_val_if_fail (GABBLE_IS_VCARD_MANAGER (manager), NULL);

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL), NULL);

  s = tp_handle_get_qdata (contact_repo, handle,
      gabble_vcard_manager_cache_quark());

  if (s == NO_ALIAS)
    s = NULL;

  DEBUG ("Cached alias for %u is \"%s\"", handle, s ? s : "(null)");
  return s;
}

/**
 * Return TRUE if we've tried looking up an alias for this handle before.
 */
gboolean
gabble_vcard_manager_has_cached_alias (GabbleVCardManager *manager,
                                       TpHandle handle)
{
  GabbleVCardManagerPrivate *priv;
  TpHandleRepoIface *contact_repo;
  gpointer p;

  g_return_val_if_fail (GABBLE_IS_VCARD_MANAGER (manager), FALSE);

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_val_if_fail (tp_handle_is_valid (contact_repo, handle, NULL),
      FALSE);

  p = tp_handle_get_qdata (contact_repo, handle,
      gabble_vcard_manager_cache_quark());

  return p != NULL;
}

