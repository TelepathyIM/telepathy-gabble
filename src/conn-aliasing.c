/*
 * conn-aliasing.c - Gabble connection aliasing interface
 * Copyright (C) 2005-2010 Collabora Ltd.
 * Copyright (C) 2005-2010 Nokia Corporation
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
#include "conn-aliasing.h"

#include <wocky/wocky.h>
#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "request-pipeline.h"
#include "roster.h"
#include "util.h"
#include "vcard-manager.h"

static void gabble_conn_aliasing_pep_nick_reply_handler (
    GabbleConnection *conn, WockyStanza *msg, TpHandle handle);
static GQuark gabble_conn_aliasing_pep_alias_quark (void);

static GabbleConnectionAliasSource _gabble_connection_get_cached_remote_alias (
    GabbleConnection *, TpHandle, gchar **);
static void maybe_request_vcard (GabbleConnection *self, TpHandle handle,
  GabbleConnectionAliasSource source);

/* distinct from any strdup()d pointer - used for negative caching */
static const gchar *NO_ALIAS = "";

/**
 * gabble_connection_get_alias_flags
 *
 * Implements D-Bus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_alias_flags (TpSvcConnectionInterfaceAliasing *iface,
                                   DBusGMethodInvocation *context)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (iface);

  g_assert (GABBLE_IS_CONNECTION (base));

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  tp_svc_connection_interface_aliasing_return_from_get_alias_flags (
      context, TP_CONNECTION_ALIAS_FLAG_USER_SET);
}


typedef struct _AliasesRequest AliasesRequest;

struct _AliasesRequest
{
  GabbleConnection *conn;
  DBusGMethodInvocation *request_call;
  guint pending_vcard_requests;
  guint pending_pep_requests;
  GArray *contacts;
  GabbleVCardManagerRequest **vcard_requests;
  GabbleRequestPipelineItem **pep_requests;
  gchar **aliases;
};

typedef struct
{
  AliasesRequest *aliases_request;
  guint index;
} AliasRequest;

static AliasesRequest *
aliases_request_new (GabbleConnection *conn,
                     DBusGMethodInvocation *request_call,
                     const GArray *contacts)
{
  AliasesRequest *request;
  TpHandleRepoIface *contact_handles;

  request = g_slice_new0 (AliasesRequest);
  request->conn = conn;
  request->request_call = request_call;
  request->contacts = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  g_array_insert_vals (request->contacts, 0, contacts->data, contacts->len);
  request->vcard_requests =
    g_new0 (GabbleVCardManagerRequest *, contacts->len);
  request->pep_requests =
    g_new0 (GabbleRequestPipelineItem *, contacts->len);
  request->aliases = g_new0 (gchar *, contacts->len + 1);

  contact_handles = tp_base_connection_get_handles ((TpBaseConnection *) conn,
      TP_HANDLE_TYPE_CONTACT);
  tp_handles_ref (contact_handles, contacts);

  return request;
}


static void
aliases_request_free (AliasesRequest *request)
{
  guint i;
  TpHandleRepoIface *contact_handles;

  for (i = 0; i < request->contacts->len; i++)
    {
      /* FIXME: what if vcard_manager is NULL? */
      if (request->vcard_requests[i] != NULL)
        gabble_vcard_manager_cancel_request (request->conn->vcard_manager,
            request->vcard_requests[i]);
    }

  contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *) request->conn, TP_HANDLE_TYPE_CONTACT);
  tp_handles_unref (contact_handles, request->contacts);

  g_array_unref (request->contacts);
  g_free (request->vcard_requests);
  g_free (request->pep_requests);
  g_strfreev (request->aliases);
  g_slice_free (AliasesRequest, request);
}


static gboolean
aliases_request_try_return (AliasesRequest *request)
{
  if (request->pending_vcard_requests == 0 &&
      request->pending_pep_requests == 0)
    {
      /* Cast to (const gchar **) necessary because no-one understands 'const'
       * in C.
       */
      tp_svc_connection_interface_aliasing_return_from_request_aliases (
          request->request_call, (const gchar **)request->aliases);
      return TRUE;
    }

  return FALSE;
}


static void
aliases_request_vcard_cb (GabbleVCardManager *manager,
                          GabbleVCardManagerRequest *request,
                          TpHandle handle,
                          WockyNode *vcard,
                          GError *error,
                          gpointer user_data)
{
  AliasesRequest *aliases_request = (AliasesRequest *) user_data;
  GabbleConnectionAliasSource source;
  guint i;
  gboolean found = FALSE;
  gchar *alias = NULL;

  g_assert (aliases_request->pending_vcard_requests > 0);

  /* The index of the vCard request in the vCard request array is the
   * index of the contact/alias in their respective arrays. */

  for (i = 0; i < aliases_request->contacts->len; i++)
    if (aliases_request->vcard_requests[i] == request)
      {
        found = TRUE;
        break;
      }

  g_assert (found);
  source = _gabble_connection_get_cached_alias (aliases_request->conn,
      g_array_index (aliases_request->contacts, TpHandle, i), &alias);
  g_assert (source != GABBLE_CONNECTION_ALIAS_NONE);
  g_assert (NULL != alias);

  aliases_request->pending_vcard_requests--;
  aliases_request->vcard_requests[i] = NULL;
  aliases_request->aliases[i] = alias;

  if (aliases_request_try_return (aliases_request))
    aliases_request_free (aliases_request);
}


static void
_cache_negatively (GabbleConnection *self,
                   TpHandle handle)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_set_qdata (contact_handles, handle,
      gabble_conn_aliasing_pep_alias_quark (), (gchar *) NO_ALIAS, NULL);
}

/* Cache pep if successful */
static void
aliases_request_cache_pep (GabbleConnection *self,
                           WockyStanza *msg,
                           TpHandle handle,
                           GError *error)
{
  if (error != NULL)
    {
      DEBUG ("Error getting alias from PEP: %s", error->message);
      _cache_negatively (self, handle);
      return;
    }
  else if (wocky_stanza_extract_errors (msg, NULL, NULL, NULL, NULL))
    {
      STANZA_DEBUG (msg, "Error getting alias from PEP");
      _cache_negatively (self, handle);
    }
  else
    {
      /* Try to extract an alias, caching it if necessary. */
      gabble_conn_aliasing_pep_nick_reply_handler (self, msg, handle);
    }

}

static void
aliases_request_basic_pep_cb (GabbleConnection *self,
                              WockyStanza *msg,
                              gpointer user_data,
                              GError *error)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  GabbleConnectionAliasSource source = GABBLE_CONNECTION_ALIAS_NONE;
  TpHandle handle = GPOINTER_TO_UINT (user_data);

  aliases_request_cache_pep (self, msg, handle, error);

  source = _gabble_connection_get_cached_alias (self, handle, NULL);

  if (source < GABBLE_CONNECTION_ALIAS_FROM_VCARD &&
      base->status == TP_CONNECTION_STATUS_CONNECTED &&
      !gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle))
    {
      /* no alias in PEP, get the vcard */
      gabble_vcard_manager_request (self->vcard_manager, handle, 0,
        NULL, NULL, G_OBJECT (self));
    }
}

static void
aliases_request_pep_cb (GabbleConnection *self,
                        WockyStanza *msg,
                        gpointer user_data,
                        GError *error)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  AliasRequest *alias_request = (AliasRequest *) user_data;
  AliasesRequest *aliases_request = alias_request->aliases_request;
  guint index = alias_request->index;
  TpHandle handle = g_array_index (aliases_request->contacts, TpHandle, index);
  GabbleConnectionAliasSource source = GABBLE_CONNECTION_ALIAS_NONE;
  gchar *alias = NULL;

  aliases_request->pending_pep_requests--;
  aliases_request->pep_requests[index] = NULL;
  g_slice_free (AliasRequest, alias_request);

  aliases_request_cache_pep (self, msg, handle, error);

  source = _gabble_connection_get_cached_alias (aliases_request->conn,
      handle, &alias);
  g_assert (source != GABBLE_CONNECTION_ALIAS_NONE);
  g_assert (NULL != alias);
  DEBUG ("Got cached alias %s with priority %u", alias, source);

  if (source >= GABBLE_CONNECTION_ALIAS_FROM_VCARD ||
      (self->vcard_manager != NULL &&
       gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle)))
    {
      aliases_request->aliases[index] = alias;
    }
  else if (base->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      DEBUG ("no longer connected, not chaining up to vCard");
      g_free (alias);
    }
  else
    {
      /* not in PEP and we have no vCard - chain to looking up their vCard */
      GabbleVCardManagerRequest *vcard_request = gabble_vcard_manager_request
          (self->vcard_manager, handle, 0, aliases_request_vcard_cb,
           aliases_request, G_OBJECT (self));

      g_free (alias);
      aliases_request->vcard_requests[index] = vcard_request;
      aliases_request->pending_vcard_requests++;
    }

  if (aliases_request_try_return (aliases_request))
    aliases_request_free (aliases_request);
}

typedef struct {
  GabbleRequestPipelineCb callback;
  gpointer user_data;
  TpHandleRepoIface *contact_handles;
  TpHandle handle;
} pep_request_ctx;

static void
pep_request_cb (
    GabbleConnection *conn,
    WockyStanza *msg,
    gpointer user_data,
    GError *error)
{
  pep_request_ctx *ctx = user_data;

  ctx->callback (conn, msg, ctx->user_data, error);
  tp_handle_unref (ctx->contact_handles, ctx->handle);
  g_slice_free (pep_request_ctx, ctx);
}

/**
 * @self must have %TP_CONNECTION_STATUS_CONNECTED.
 */
static GabbleRequestPipelineItem *
gabble_do_pep_request (GabbleConnection *self,
                       TpHandle handle,
                       TpHandleRepoIface *contact_handles,
                       GabbleRequestPipelineCb callback,
                       gpointer user_data)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  const gchar *to;
  WockyStanza *msg;
  GabbleRequestPipelineItem *pep_request;
  pep_request_ctx *ctx;

  /* callers must check this... */
  g_assert (base->status == TP_CONNECTION_STATUS_CONNECTED);
  /* ... which implies this */
  g_assert (self->req_pipeline != NULL);

  ctx = g_slice_new0 (pep_request_ctx);
  ctx->callback = callback;
  ctx->user_data = user_data;
  ctx->contact_handles = contact_handles;
  ctx->handle = handle;

  tp_handle_ref (contact_handles, handle);
  to = tp_handle_inspect (contact_handles, handle);
  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      NULL, to,
      '(', "pubsub",
        ':', NS_PUBSUB,
        '(', "items",
          '@', "node", NS_NICK,
        ')',
      ')',
      NULL);
   pep_request = gabble_request_pipeline_enqueue (self->req_pipeline,
      msg, 0, pep_request_cb, ctx);
   g_object_unref (msg);

   return pep_request;
}

/**
 * gabble_connection_request_aliases
 *
 * Implements D-Bus method RequestAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_request_aliases (TpSvcConnectionInterfaceAliasing *iface,
                                   const GArray *contacts,
                                   DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  guint i;
  AliasesRequest *request;
  GError *error = NULL;

  g_assert (GABBLE_IS_CONNECTION (self));

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  request = aliases_request_new (self, context, contacts);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GabbleConnectionAliasSource source;
      GabbleVCardManagerRequest *vcard_request;
      gchar *alias;

      source = _gabble_connection_get_cached_alias (self, handle, &alias);
      g_assert (source != GABBLE_CONNECTION_ALIAS_NONE);
      g_assert (NULL != alias);

      if (source >= GABBLE_CONNECTION_ALIAS_FROM_VCARD ||
          gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle))
        {
          /* Either the alias we got was from a vCard or better, or we already
           * tried getting an alias from a vcard and failed, so there's no
           * point trying again. */
          request->aliases[i] = alias;
        }
      else if (self->features & GABBLE_CONNECTION_FEATURES_PEP)
        {
           /* FIXME: we shouldn't have to do this, since we should get PEP
            * events when someone first sends us presence. However, the
            * current ejabberd PEP implementation doesn't seem to give
            * us notifications of the initial state. */
          AliasRequest *data = g_slice_new (AliasRequest);

          g_free (alias);

          data->aliases_request = request;
          data->index = i;

          request->pending_pep_requests++;
          request->pep_requests[i] = gabble_do_pep_request (self,
              handle, contact_handles, aliases_request_pep_cb, data);

        }
      else
        {
          DEBUG ("requesting vCard for alias of contact %s",
              tp_handle_inspect (contact_handles, handle));

          g_free (alias);
          vcard_request = gabble_vcard_manager_request (self->vcard_manager,
              handle, 0, aliases_request_vcard_cb, request, G_OBJECT (self));

          request->vcard_requests[i] = vcard_request;
          request->pending_vcard_requests++;
        }
    }

  if (aliases_request_try_return (request))
    aliases_request_free (request);
}

static void
nick_publish_msg_reply_cb (GabbleConnection *conn,
                           WockyStanza *sent_msg,
                           WockyStanza *reply_msg,
                           GObject *object,
                           gpointer user_data)
{
#ifdef ENABLE_DEBUG
  GError *error = NULL;

  if (wocky_stanza_extract_errors (reply_msg, NULL, &error, NULL, NULL))
    {
      DEBUG ("can't publish nick using PEP: %s: %s",
          wocky_xmpp_stanza_error_to_string (error), error->message);

      g_clear_error (&error);
    }
#endif
}

static gboolean
set_one_alias (
    GabbleConnection *conn,
    TpHandle handle,
    gchar *alias,
    GError **error)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  gboolean ret = TRUE;

  g_assert (base->status == TP_CONNECTION_STATUS_CONNECTED);

  if (tp_str_empty (alias))
    alias = NULL;

  if (!tp_handle_is_valid (contact_handles, handle, error))
    {
      ret = FALSE;
    }
  else if (base->self_handle == handle)
    {
      /* only alter the roster if we're already there, e.g. because someone
       * added us with another client
       */
      if (gabble_roster_handle_has_entry (conn->roster, handle)
          && !gabble_roster_handle_set_name (conn->roster, handle,
                                             alias, error))
        {
          ret = FALSE;
        }
    }
  else
    {
      gchar *remote_alias = NULL;
      GabbleConnectionAliasSource source = GABBLE_CONNECTION_ALIAS_FROM_ROSTER;

      if (alias == NULL)
        {
          source = _gabble_connection_get_cached_remote_alias (conn, handle,
              &remote_alias);
          alias = remote_alias;
        }

      ret = gabble_roster_handle_set_name (conn->roster, handle, alias, error);
      g_free (remote_alias);

      /* If we don't have a cached remote alias for this contact, try to ask
       * for one. (Maybe we haven't seen a PEP update or fetched their vCard in
       * this session?)
       */
      maybe_request_vcard (conn, handle, source);
    }

  if (base->self_handle == handle)
    {
      GabbleVCardManagerEditInfo *edit;
      GQueue edits = G_QUEUE_INIT;

      /* User has called SetAliases on themselves - patch their vCard.
       * FIXME: because SetAliases is currently synchronous, we ignore errors
       * here, and just let the request happen in the background.
       */

      if (conn->features & GABBLE_CONNECTION_FEATURES_PEP)
        {
          /* Publish nick using PEP */
          WockyStanza *msg;
          WockyNode *item;

          msg = wocky_pep_service_make_publish_stanza (conn->pep_nick, &item);
          /* Does the right thing if alias == NULL. */
          wocky_node_add_child_with_content_ns (item, "nick", alias, NS_NICK);

          _gabble_connection_send_with_reply (conn, msg,
              nick_publish_msg_reply_cb, NULL, NULL, NULL);

          g_object_unref (msg);
        }

      if (alias == NULL)
        /* Deliberately not doing the fall-back-to-FN-on-GTalk dance because
         * clearing your FN is more serious.
         */
        edit = gabble_vcard_manager_edit_info_new ("NICKNAME", NULL,
            GABBLE_VCARD_EDIT_DELETE, NULL);
      else
        edit = gabble_vcard_manager_edit_info_new (NULL, alias,
            GABBLE_VCARD_EDIT_SET_ALIAS, NULL);

      g_queue_push_head (&edits, edit);
      /* Yes, gabble_vcard_manager_edit steals the list you pass it. */
      gabble_vcard_manager_edit (conn->vcard_manager, 0, NULL,
          NULL, G_OBJECT (conn), edits.head);
    }

  return ret;
}

/**
 * gabble_connection_set_aliases
 *
 * Implements D-Bus method SetAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 */
static void
gabble_connection_set_aliases (TpSvcConnectionInterfaceAliasing *iface,
                               GHashTable *aliases,
                               DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  GHashTableIter iter;
  gpointer key, value;
  gboolean retval = TRUE;
  GError *first_error = NULL;

  g_assert (GABBLE_IS_CONNECTION (self));

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  g_hash_table_iter_init (&iter, aliases);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (!set_one_alias (self, GPOINTER_TO_UINT (key), value,
            (first_error == NULL ? &first_error : NULL)))
        retval = FALSE;
    }

  if (retval)
    {
      tp_svc_connection_interface_aliasing_return_from_set_aliases (
          context);
    }
  else
    {
      dbus_g_method_return_error (context, first_error);
      g_error_free (first_error);
    }
}


GQuark
gabble_conn_aliasing_pep_alias_quark (void)
{
  static GQuark quark = 0;

  if (G_UNLIKELY (quark == 0))
    quark = g_quark_from_static_string
        ("gabble_conn_aliasing_pep_alias_quark");

  return quark;
}


static gboolean
_grab_nickname (GabbleConnection *self,
                TpHandle handle,
                WockyNode *node)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GQuark quark = gabble_conn_aliasing_pep_alias_quark ();
  const gchar *old, *nickname;

  node = wocky_node_get_child_ns (node, "nick", NS_NICK);

  if (NULL == node)
    {
      DEBUG ("didn't get a nickname for %s", tp_handle_inspect
          (contact_handles, handle));
      _cache_negatively (self, handle);
      return FALSE;
    }

  nickname = node->content;
  old = tp_handle_get_qdata (contact_handles, handle, quark);

  if (tp_strdiff (old, nickname))
    {
      if (nickname == NULL)
        {
          DEBUG ("got empty <nick/> node, caching as NO_ALIAS");
          _cache_negatively (self, handle);
        }
      else
        {
          tp_handle_set_qdata (contact_handles, handle, quark, g_strdup (nickname),
              g_free);
        }

      gabble_conn_aliasing_nickname_updated ((GObject *) self, handle, self);
    }
  return TRUE;
}


static void
pep_nick_node_changed (WockyPepService *pep,
    WockyBareContact *contact,
    WockyStanza *stanza,
    WockyNode *item,
    GabbleConnection *conn)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  const gchar *jid;

  jid = wocky_bare_contact_get_jid (contact);
  handle = tp_handle_ensure (contact_repo, jid, NULL, NULL);
  if (handle == 0)
    {
      DEBUG ("Invalid from: %s", jid);
      return;
    }

  if (NULL == item)
    {
      STANZA_DEBUG (stanza, "PEP event without item node, ignoring");
      return;
    }

  _grab_nickname (conn, handle, item);
}


static void
gabble_conn_aliasing_pep_nick_reply_handler (GabbleConnection *conn,
                                             WockyStanza *msg,
                                             TpHandle handle)
{
  WockyNode *pubsub_node, *items_node, *item_node;
  gboolean found = FALSE;
  WockyNodeIter i;

  pubsub_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (msg), "pubsub", NS_PUBSUB);
  if (pubsub_node == NULL)
    {
      pubsub_node = wocky_node_get_child_ns (
        wocky_stanza_get_top_node (msg), "pubsub", NS_PUBSUB "#event");

      if (pubsub_node == NULL)
        {
          STANZA_DEBUG (msg, "PEP reply with no <pubsub>, ignoring");
          _cache_negatively (conn, handle);
          return;
        }
      else
        {
          STANZA_DEBUG (msg, "PEP reply from buggy server with #event "
              "on <pubsub> namespace");
        }
    }

  items_node = wocky_node_get_child (pubsub_node, "items");
  if (items_node == NULL)
    {
      STANZA_DEBUG (msg, "No items in PEP reply");
      _cache_negatively (conn, handle);
      return;
    }

  wocky_node_iter_init (&i, items_node, NULL, NULL);
  while (wocky_node_iter_next (&i, &item_node))
    {
      if (_grab_nickname (conn, handle, item_node))
        {
          /* FIXME: does this do the right thing on servers which return
           * multiple items? ejabberd only returns one anyway */
          found = TRUE;
          break;
        }
    }

  if (!found)
    {
      _cache_negatively (conn, handle);
    }
}

void
gabble_conn_aliasing_nickname_updated (GObject *object,
                                       TpHandle handle,
                                       gpointer user_data)
{
  GArray *handles;

  handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
  g_array_append_val (handles, handle);

  gabble_conn_aliasing_nicknames_updated (object, handles, user_data);

  g_array_unref (handles);
}

void
gabble_conn_aliasing_nicknames_updated (GObject *object,
                                        GArray *handles,
                                        gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionAliasSource signal_source;
  GPtrArray *aliases;
  guint i;

  g_return_if_fail (handles->len > 0);

  if (object == user_data)
    {
      /* actually PEP */
      signal_source = GABBLE_CONNECTION_ALIAS_FROM_PRESENCE;
    }
  else if (object == G_OBJECT (conn->roster))
    {
      signal_source = GABBLE_CONNECTION_ALIAS_FROM_ROSTER;
    }
  else if (object == G_OBJECT (conn->presence_cache))
    {
      signal_source = GABBLE_CONNECTION_ALIAS_FROM_PRESENCE;
    }
   else if (object == G_OBJECT (conn->vcard_manager))
     {
       signal_source = GABBLE_CONNECTION_ALIAS_FROM_VCARD;
     }
  else
    {
      g_assert_not_reached ();
      return;
    }

  aliases = g_ptr_array_sized_new (handles->len);

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);
      GabbleConnectionAliasSource current_source;
      gchar *alias = NULL;
      GValue entry = { 0, };

      current_source = _gabble_connection_get_cached_alias (conn, handle,
          &alias);
      g_assert (current_source != GABBLE_CONNECTION_ALIAS_NONE);

      /* if the active alias for this handle is already known and from
       * a higher priority, this signal is not interesting so we do
       * nothing */
      if (signal_source < current_source)
        {
          DEBUG ("ignoring boring alias change for handle %u, signal from %u "
              "but source %u has alias \"%s\"", handle, signal_source,
              current_source, alias);
          g_free (alias);
          continue;
        }

      g_value_init (&entry, TP_STRUCT_TYPE_ALIAS_PAIR);
      g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (TP_STRUCT_TYPE_ALIAS_PAIR));

      dbus_g_type_struct_set (&entry,
          0, handle,
          1, alias,
          G_MAXUINT);

      g_ptr_array_add (aliases, g_value_get_boxed (&entry));

      /* Check whether the roster has an entry for the handle and if so, set
       * the roster alias so the vCard isn't fetched on every connect. */
      if (signal_source < GABBLE_CONNECTION_ALIAS_FROM_ROSTER &&
          gabble_roster_handle_has_entry (conn->roster, handle))
        gabble_roster_handle_set_name (conn->roster, handle, alias, NULL);

      g_free (alias);
    }

  if (aliases->len > 0)
    tp_svc_connection_interface_aliasing_emit_aliases_changed (conn, aliases);

  for (i = 0; i < aliases->len; i++)
    g_boxed_free (TP_STRUCT_TYPE_ALIAS_PAIR, g_ptr_array_index (aliases, i));

  g_ptr_array_unref (aliases);
}

static void
set_or_clear (gchar **target,
    gchar *source)
{
  if (target != NULL)
    *target = source;
  else
    g_free (source);
}

static void
maybe_set (gchar **target,
    const gchar *source)
{
  if (target != NULL)
    *target = g_strdup (source);
}

static GabbleConnectionAliasSource
get_cached_remote_alias (
    GabbleConnection *conn,
    TpHandleRepoIface *contact_handles,
    TpHandle handle,
    const gchar *jid,
    gchar **alias)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabblePresence *pres;
  const gchar *tmp;
  gchar *resource;

  tmp = tp_handle_get_qdata (contact_handles, handle,
      gabble_conn_aliasing_pep_alias_quark ());
  if (tmp != NULL && tmp != NO_ALIAS)
    {
      maybe_set (alias, tmp);
      return GABBLE_CONNECTION_ALIAS_FROM_PRESENCE;
    }

  pres = gabble_presence_cache_get (conn->presence_cache, handle);
  if (NULL != pres && NULL != pres->nickname)
    {
      maybe_set (alias, pres->nickname);
      return GABBLE_CONNECTION_ALIAS_FROM_PRESENCE;
    }

  /* XXX: should this be more important than the ones from presence? */
  /* if it's our own handle, use alias passed to the connmgr, if any */
  if (handle == base->self_handle)
    {
      gchar *cm_alias;

      g_object_get (conn,
          "alias", &cm_alias,
          NULL);

      if (cm_alias != NULL)
        {
          set_or_clear (alias, cm_alias);
          return GABBLE_CONNECTION_ALIAS_FROM_CONNMGR;
        }
    }

  /* MUC handles have the nickname in the resource */
  if (wocky_decode_jid (jid, NULL, NULL, &resource) &&
      NULL != resource)
    {
      set_or_clear (alias, resource);
      return GABBLE_CONNECTION_ALIAS_FROM_MUC_RESOURCE;
    }

  if (conn->vcard_manager != NULL)
    {
      /* if we've seen a nickname in their vCard, use that */
      tmp = gabble_vcard_manager_get_cached_alias (conn->vcard_manager,
          handle);
      if (NULL != tmp)
        {
          maybe_set (alias, tmp);
          return GABBLE_CONNECTION_ALIAS_FROM_VCARD;
        }
    }

  maybe_set (alias, NULL);
  return GABBLE_CONNECTION_ALIAS_NONE;
}

/*
 * _gabble_connection_get_cached_alias:
 * @conn: a connection
 * @handle: a handle
 * @alias: (allow-none): location at which to store @handle's alias. If
 *         provided, it will always be set to a non-NULL, non-empty string,
 *         which the caller must free.
 *
 * Gets the best possible alias for @handle, falling back to their JID if
 * necessary.
 *
 * Returns: the source of the alias.
 */
GabbleConnectionAliasSource
_gabble_connection_get_cached_alias (GabbleConnection *conn,
                                     TpHandle handle,
                                     gchar **alias)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  const gchar *tmp, *jid;
  gboolean roster_alias_was_jid = FALSE;
  GabbleConnectionAliasSource source;

  g_return_val_if_fail (NULL != conn, GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (tp_handle_is_valid (contact_handles, handle, NULL),
      GABBLE_CONNECTION_ALIAS_NONE);

  jid = tp_handle_inspect (contact_handles, handle);
  g_assert (NULL != jid);

  tmp = gabble_roster_handle_get_name (conn->roster, handle);
  if (!tp_strdiff (tmp, jid))
    {
      /* Normally, we prefer whatever we've cached on the roster, to avoid
       * wasting bandwidth checking for aliases by repeatedly fetching the
       * vCard, and (more importantly) to prefer anything the local user set
       * over what the contact says their name is.
       *
       * However, if the alias stored on the roster is just the contact's JID,
       * we check for better aliases that we happen to have received from other
       * sources (maybe a PEP nick update, or a vCard we've fetched for the
       * avatar, or whatever). If we can't find anything better, we'll use the
       * JID, and still say that it came from the roster: this means we don't
       * defeat negative caching for contacts who genuinely don't have an
       * alias.
       */
      roster_alias_was_jid = TRUE;
    }
  else if (!tp_str_empty (tmp))
    {
      maybe_set (alias, tmp);
      return GABBLE_CONNECTION_ALIAS_FROM_ROSTER;
    }

  source = get_cached_remote_alias (conn, contact_handles, handle, jid, alias);
  if (source != GABBLE_CONNECTION_ALIAS_NONE)
    return source;

  /* otherwise just take their jid, which may have been specified on the roster
   * as the contact's alias. */
  maybe_set (alias, jid);
  return roster_alias_was_jid ? GABBLE_CONNECTION_ALIAS_FROM_ROSTER
      : GABBLE_CONNECTION_ALIAS_FROM_JID;
}

/*
 * _gabble_connection_get_cached_remote_alias:
 * @conn: a connection
 * @handle: a handle
 * @alias: (allow-none): location at which to store @handle's alias. If
 *         provided, it may be set to %NULL (if @handle has no cached remote
 *         alias) or a non-empty string which the caller must free.
 *
 * Gets the best cached alias for @handle as provided by them (such as via PEP
 * Nicknames, in their vCard, etc), not considering anything the local user has
 * specified on their roster.
 *
 * Returns: the source of the alias, or GABBLE_CONNECTION_ALIAS_NONE if we have
 *          no cached remote alias for @handle
 */
static GabbleConnectionAliasSource
_gabble_connection_get_cached_remote_alias (
    GabbleConnection *conn,
    TpHandle handle,
    gchar **alias)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  const gchar *jid = tp_handle_inspect (contact_handles, handle);

  g_assert (NULL != jid);
  return get_cached_remote_alias (conn, contact_handles, handle, jid, alias);
}

static void
maybe_request_vcard (GabbleConnection *self, TpHandle handle,
  GabbleConnectionAliasSource source)
{
  TpBaseConnection *base = (TpBaseConnection *) self;

  /* If the source wasn't good enough then do a request */
  if (source < GABBLE_CONNECTION_ALIAS_FROM_VCARD &&
      base->status == TP_CONNECTION_STATUS_CONNECTED &&
      !gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle))
    {
      if (self->features & GABBLE_CONNECTION_FEATURES_PEP)
        {
          TpHandleRepoIface *contact_handles =
            tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

          gabble_do_pep_request (self, handle, contact_handles,
            aliases_request_basic_pep_cb, GUINT_TO_POINTER (handle));
        }
      else
        {
          gabble_vcard_manager_request (self->vcard_manager,
             handle, 0, NULL, NULL, G_OBJECT (self));
        }
    }
}

static void
conn_aliasing_fill_contact_attributes (GObject *obj,
    const GArray *contacts, GHashTable *attributes_hash)
{
  guint i;
  GabbleConnection *self = GABBLE_CONNECTION(obj);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GabbleConnectionAliasSource source;
      gchar *alias;
      GValue *val = tp_g_value_slice_new (G_TYPE_STRING);

      source = _gabble_connection_get_cached_alias (self, handle, &alias);
      g_assert (alias != NULL);

      g_value_take_string (val, alias);

      tp_contacts_mixin_set_contact_attribute (attributes_hash,
        handle, TP_IFACE_CONNECTION_INTERFACE_ALIASING"/alias",
        val);

      maybe_request_vcard (self, handle, source);
    }
}

/**
 * gabble_connection_get_aliases
 *
 * Implements D-Bus method GetAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_aliases (TpSvcConnectionInterfaceAliasing *iface,
                               const GArray *contacts,
                               DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GHashTable *result;
  GError *error = NULL;
  guint i;

  g_assert (GABBLE_IS_CONNECTION (self));

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  result = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, g_free);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GabbleConnectionAliasSource source;
      gchar *alias;

      source = _gabble_connection_get_cached_alias (self, handle, &alias);
      g_assert (alias != NULL);

      g_hash_table_insert (result, GUINT_TO_POINTER (handle), alias);

      maybe_request_vcard (self, handle, source);
    }

  tp_svc_connection_interface_aliasing_return_from_get_aliases (context,
    result);

  g_hash_table_unref (result);
}



void
conn_aliasing_init (GabbleConnection *conn)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
    TP_IFACE_CONNECTION_INTERFACE_ALIASING,
    conn_aliasing_fill_contact_attributes);

  conn->pep_nick = wocky_pep_service_new (NS_NICK, TRUE);

  g_signal_connect (conn->pep_nick, "changed",
      G_CALLBACK (pep_nick_node_changed), conn);
}

void
conn_aliasing_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasingClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_alias_flags);
  IMPLEMENT(request_aliases);
  IMPLEMENT(get_aliases);
  IMPLEMENT(set_aliases);
#undef IMPLEMENT
}

