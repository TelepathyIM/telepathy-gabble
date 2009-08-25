/*
 * conn-aliasing.c - Gabble connection aliasing interface
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
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

#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "pubsub.h"
#include "request-pipeline.h"
#include "roster.h"
#include "util.h"
#include "vcard-manager.h"

static void gabble_conn_aliasing_pep_nick_reply_handler (
    GabbleConnection *conn, LmMessage *msg, TpHandle handle);
static GQuark gabble_conn_aliasing_pep_alias_quark (void);

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
      if (request->vcard_requests[i] != NULL)
        gabble_vcard_manager_cancel_request (request->conn->vcard_manager,
            request->vcard_requests[i]);
    }

  contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *) request->conn, TP_HANDLE_TYPE_CONTACT);
  tp_handles_unref (contact_handles, request->contacts);

  g_array_free (request->contacts, TRUE);
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
                          LmMessageNode *vcard,
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
                           LmMessage *msg,
                           TpHandle handle,
                           GError *error)
{
  if (error != NULL)
    {
      DEBUG ("Error getting alias from PEP: %s", error->message);
      _cache_negatively (self, handle);
    }
  else if (lm_message_get_sub_type (msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      NODE_DEBUG (msg->node, "Error getting alias from PEP");
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
                              LmMessage *msg,
                              gpointer user_data,
                              GError *error)
{
  GabbleConnectionAliasSource source = GABBLE_CONNECTION_ALIAS_NONE;
  TpHandle handle = GPOINTER_TO_UINT (user_data);

  aliases_request_cache_pep (self, msg, handle, error);

  source = _gabble_connection_get_cached_alias (self, handle, NULL);

  if (source < GABBLE_CONNECTION_ALIAS_FROM_VCARD &&
      !gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle))
    {
      /* no alias in PEP, get the vcard */
      gabble_vcard_manager_request (self->vcard_manager, handle, 0,
        NULL, NULL, G_OBJECT (self));
    }
}

static void
aliases_request_pep_cb (GabbleConnection *self,
                        LmMessage *msg,
                        gpointer user_data,
                        GError *error)
{
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
      gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle))
    {
      aliases_request->aliases[index] = alias;
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
    LmMessage *msg,
    gpointer user_data,
    GError *error)
{
  pep_request_ctx *ctx = user_data;

  ctx->callback (conn, msg, ctx->user_data, error);
  tp_handle_unref (ctx->contact_handles, ctx->handle);
  g_slice_free (pep_request_ctx, ctx);
}

static GabbleRequestPipelineItem *
gabble_do_pep_request (GabbleConnection *self,
                       TpHandle handle,
                       TpHandleRepoIface *contact_handles,
                       GabbleRequestPipelineCb callback,
                       gpointer user_data)
{
  LmMessage *msg;
  GabbleRequestPipelineItem *pep_request;
  pep_request_ctx *ctx = g_slice_new0 (pep_request_ctx);

  ctx->callback = callback;
  ctx->user_data = user_data;
  ctx->contact_handles = contact_handles;
  ctx->handle = handle;

  tp_handle_ref (contact_handles, handle);
  msg = lm_message_build (tp_handle_inspect (contact_handles, handle),
      LM_MESSAGE_TYPE_IQ,
      '@', "type", "get",
      '(', "pubsub", "",
        '@', "xmlns", NS_PUBSUB,
        '(', "items", "",
          '@', "node", NS_NICK,
        ')',
      ')',
      NULL);
   pep_request = gabble_request_pipeline_enqueue (self->req_pipeline,
      msg, 0, pep_request_cb, ctx);
   lm_message_unref (msg);

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


struct _i_hate_g_hash_table_foreach
{
  GabbleConnection *conn;
  GError **error;
  gboolean retval;
};

static LmHandlerResult
nick_publish_msg_reply_cb (GabbleConnection *conn,
                           LmMessage *sent_msg,
                           LmMessage *reply_msg,
                           GObject *object,
                           gpointer user_data)
{
#ifdef ENABLE_DEBUG
  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      error_node = lm_message_node_get_child (reply_msg->node, "error");

      if (error_node != NULL)
        {
          GabbleXmppError error = gabble_xmpp_error_from_node (error_node,
              NULL);

          DEBUG ("can't publish nick using PEP: %s: %s",
              gabble_xmpp_error_string (error),
              gabble_xmpp_error_description (error));
        }
    }
#endif

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
setaliases_foreach (gpointer key, gpointer value, gpointer user_data)
{
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  TpHandle handle = GPOINTER_TO_UINT (key);
  gchar *alias = (gchar *) value;
  GError *error = NULL;
  TpBaseConnection *base = (TpBaseConnection *) data->conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (contact_handles, handle, &error))
    {
      data->retval = FALSE;
    }
  else if (base->self_handle == handle)
    {
      /* only alter the roster if we're already there, e.g. because someone
       * added us with another client
       */
      if (gabble_roster_handle_has_entry (data->conn->roster, handle)
          && !gabble_roster_handle_set_name (data->conn->roster, handle,
                                             alias, data->error))
        {
          data->retval = FALSE;
        }
    }
  else if (!gabble_roster_handle_set_name (data->conn->roster, handle, alias,
        data->error))
    {
      data->retval = FALSE;
    }

  if (base->self_handle == handle)
    {
      /* User has called SetAliases on themselves - patch their vCard.
       * FIXME: because SetAliases is currently synchronous, we ignore errors
       * here, and just let the request happen in the background.
       */

      if (data->conn->features & GABBLE_CONNECTION_FEATURES_PEP)
        {
          /* Publish nick using PEP */
          LmMessage *msg;
          LmMessageNode *publish;

          msg = pubsub_make_publish_msg (NULL, NS_NICK, NS_NICK, "nick",
              &publish);
          lm_message_node_set_value (publish, alias);

          _gabble_connection_send_with_reply (data->conn, msg,
              nick_publish_msg_reply_cb, NULL, NULL, NULL);

          lm_message_unref (msg);
        }

      gabble_vcard_manager_edit (data->conn->vcard_manager, 0, NULL, NULL,
          G_OBJECT(data->conn), 1, "NICKNAME", alias);
    }

  if (NULL != error)
    {
      if (NULL == *(data->error))
        {
          *(data->error) = error;
        }
      else
        {
          g_error_free (error);
        }
    }
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
  GError *error = NULL;
  struct _i_hate_g_hash_table_foreach data = { NULL, NULL, TRUE };

  g_assert (GABBLE_IS_CONNECTION (self));

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  data.conn = self;
  data.error = &error;

  g_hash_table_foreach (aliases, setaliases_foreach, &data);

  if (data.retval)
    {
      tp_svc_connection_interface_aliasing_return_from_set_aliases (
          context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
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
                LmMessageNode *node)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GQuark quark = gabble_conn_aliasing_pep_alias_quark ();
  const gchar *old, *nickname;

  node = lm_message_node_get_child_with_namespace (node, "nick", NS_NICK);

  if (NULL == node)
    {
      DEBUG ("didn't get a nickname for %s", tp_handle_inspect
          (contact_handles, handle));
      _cache_negatively (self, handle);
      return FALSE;
    }

  nickname = lm_message_node_get_value (node);
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


gboolean
gabble_conn_aliasing_pep_nick_event_handler (GabbleConnection *conn,
                                             LmMessage *msg,
                                             TpHandle handle)
{
  LmMessageNode *node;

  node = lm_message_node_find_child (msg->node, "item");
  if (NULL == node)
    {
    NODE_DEBUG (msg->node, "PEP event without item node, ignoring");
    return FALSE;
  }

  _grab_nickname (conn, handle, node);

  return TRUE;
}


static void
gabble_conn_aliasing_pep_nick_reply_handler (GabbleConnection *conn,
                                             LmMessage *msg,
                                             TpHandle handle)
{
  LmMessageNode *pubsub_node, *items_node;
  gboolean found = FALSE;
  NodeIter i;

  pubsub_node = lm_message_node_get_child_with_namespace (msg->node,
      "pubsub", NS_PUBSUB);
  if (pubsub_node == NULL)
    {
      pubsub_node = lm_message_node_get_child_with_namespace (msg->node,
        "pubsub", NS_PUBSUB "#event");

      if (pubsub_node == NULL)
        {
          NODE_DEBUG (msg->node, "PEP reply with no <pubsub>, ignoring");
          _cache_negatively (conn, handle);
          return;
        }
      else
        {
          NODE_DEBUG (msg->node, "PEP reply from buggy server with #event "
              "on <pubsub> namespace");
        }
    }

  items_node = lm_message_node_get_child (pubsub_node, "items");
  if (items_node == NULL)
    {
      NODE_DEBUG (msg->node, "No items in PEP reply");
      _cache_negatively (conn, handle);
      return;
    }

  for (i = node_iter (items_node); i; i = node_iter_next (i))
    {
      LmMessageNode *item_node = node_iter_data (i);

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
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionAliasSource signal_source, current_source;
  gchar *alias = NULL;
  GPtrArray *aliases;
  GValue entry = { 0, };

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

  current_source = _gabble_connection_get_cached_alias (conn, handle, &alias);

  g_assert (current_source != GABBLE_CONNECTION_ALIAS_NONE);

  /* if the active alias for this handle is already known and from
   * a higher priority, this signal is not interesting so we do
   * nothing */
  if (signal_source < current_source)
    {
      DEBUG ("ignoring boring alias change for handle %u, signal from %u "
          "but source %u has alias \"%s\"", handle, signal_source,
          current_source, alias);
      goto OUT;
    }

  g_value_init (&entry, TP_STRUCT_TYPE_ALIAS_PAIR);
  g_value_take_boxed (&entry, dbus_g_type_specialized_construct
      (TP_STRUCT_TYPE_ALIAS_PAIR));

  dbus_g_type_struct_set (&entry,
      0, handle,
      1, alias,
      G_MAXUINT);

  aliases = g_ptr_array_sized_new (1);
  g_ptr_array_add (aliases, g_value_get_boxed (&entry));


  tp_svc_connection_interface_aliasing_emit_aliases_changed (conn, aliases);

  g_value_unset (&entry);
  g_ptr_array_free (aliases, TRUE);

  /* Check whether the roster has an entry for the handle and if so, set the
   * roster alias so the vCard isn't fetched on every connect. */
  if (signal_source < GABBLE_CONNECTION_ALIAS_FROM_ROSTER &&
      gabble_roster_handle_has_entry (conn->roster, handle))
    gabble_roster_handle_set_name (conn->roster, handle, alias, NULL);

OUT:
  g_free (alias);
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

GabbleConnectionAliasSource
_gabble_connection_get_cached_alias (GabbleConnection *conn,
                                     TpHandle handle,
                                     gchar **alias)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GabblePresence *pres;
  const gchar *tmp, *jid;
  gchar *resource = NULL;

  g_return_val_if_fail (NULL != conn, GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (tp_handle_is_valid (contact_handles, handle, NULL),
      GABBLE_CONNECTION_ALIAS_NONE);

  tmp = gabble_roster_handle_get_name (conn->roster, handle);
  if (NULL != tmp)
    {
      maybe_set (alias, tmp);
      return GABBLE_CONNECTION_ALIAS_FROM_ROSTER;
    }

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

  jid = tp_handle_inspect (contact_handles, handle);
  g_assert (NULL != jid);


  /* MUC handles have the nickname in the resource */
  if (gabble_decode_jid (jid, NULL, NULL, &resource) &&
      NULL != resource)
    {
      set_or_clear (alias, resource);
      return GABBLE_CONNECTION_ALIAS_FROM_MUC_RESOURCE;
    }

  /* if we've seen a nickname in their vCard, use that */
  tmp = gabble_vcard_manager_get_cached_alias (conn->vcard_manager, handle);
  if (NULL != tmp)
    {
      maybe_set (alias, tmp);
      return GABBLE_CONNECTION_ALIAS_FROM_VCARD;
    }

  /* otherwise just take their jid */
  maybe_set (alias, jid);
  return GABBLE_CONNECTION_ALIAS_FROM_JID;
}

static void
maybe_request_vcard (GabbleConnection *self, TpHandle handle,
  GabbleConnectionAliasSource source)
{
  /* If the source wasn't good enough then do a request */
  if (source < GABBLE_CONNECTION_ALIAS_FROM_VCARD &&
      !gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle))
    {
      if (self->features & GABBLE_CONNECTION_FEATURES_PEP)
        {
          TpBaseConnection *base = (TpBaseConnection *) self;
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

  g_hash_table_destroy (result);
}



void
conn_aliasing_init (GabbleConnection *conn)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
    TP_IFACE_CONNECTION_INTERFACE_ALIASING,
    conn_aliasing_fill_contact_attributes);
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

