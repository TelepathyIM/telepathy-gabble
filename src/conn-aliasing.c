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

#include "conn-aliasing.h"

#include <telepathy-glib/svc-connection.h>

#include "gabble-connection.h"
#include "roster.h"
#include "vcard-manager.h"
#include "pubsub.h"
#include "namespaces.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "debug.h"

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
  GArray *contacts;
  GabbleVCardManagerRequest **vcard_requests;
  gchar **aliases;
};


static AliasesRequest *
aliases_request_new (GabbleConnection *conn,
                     DBusGMethodInvocation *request_call,
                     const GArray *contacts)
{
  AliasesRequest *request;

  request = g_slice_new0 (AliasesRequest);
  request->conn = conn;
  request->request_call = request_call;
  request->contacts = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  g_array_insert_vals (request->contacts, 0, contacts->data, contacts->len);
  request->vcard_requests =
    g_new0 (GabbleVCardManagerRequest *, contacts->len);
  request->aliases = g_new0 (gchar *, contacts->len + 1);
  return request;
}


static void
aliases_request_free (AliasesRequest *request)
{
  guint i;

  for (i = 0; i < request->contacts->len; i++)
    {
      if (request->vcard_requests[i] != NULL)
        gabble_vcard_manager_cancel_request (request->conn->vcard_manager,
            request->vcard_requests[i]);
    }

  g_array_free (request->contacts, TRUE);
  g_free (request->vcard_requests);
  g_strfreev (request->aliases);
  g_slice_free (AliasesRequest, request);
}


static gboolean
aliases_request_try_return (AliasesRequest *request)
{
  if (request->pending_vcard_requests == 0)
    {
      /* FIXME: I'm not entirely sure why gcc warns without this cast from
       * (gchar **) to (const gchar **) */
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
  TpBaseConnection *base = (TpBaseConnection *)self;
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
      else
        {
          DEBUG ("requesting vCard for alias of contact %s",
              tp_handle_inspect (contact_handles, handle));

          g_free (alias);
          vcard_request = gabble_vcard_manager_request (self->vcard_manager,
              handle, 0, aliases_request_vcard_cb, request, G_OBJECT (self),
              &error);

          if (NULL != error)
            {
              dbus_g_method_return_error (context, error);
              g_error_free (error);
              aliases_request_free (request);
              return;
            }

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
  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      error_node = lm_message_node_get_child (reply_msg->node, "error");

      if (error_node != NULL)
        {
          GabbleXmppError error = gabble_xmpp_error_from_node (error_node);

          g_warning ("%s: can't publish nick using PEP: %s: %s", G_STRFUNC,
              gabble_xmpp_error_string (error),
              gabble_xmpp_error_description (error));
        }
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
setaliases_foreach (gpointer key, gpointer value, gpointer user_data)
{
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  TpHandle handle = GPOINTER_TO_INT (key);
  gchar *alias = (gchar *) value;
  GError *error = NULL;
  TpBaseConnection *base = (TpBaseConnection *)data->conn;
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
          G_OBJECT(data->conn), "NICKNAME", alias, NULL);
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
  TpBaseConnection *base = (TpBaseConnection *)self;
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


void
conn_aliasing_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasingClass *klass =
    (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_alias_flags);
  IMPLEMENT(request_aliases);
  IMPLEMENT(set_aliases);
#undef IMPLEMENT
}

