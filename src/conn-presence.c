/*
 * conn-presence - Gabble connection presence interface
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
#include "conn-presence.h"
#include "namespaces.h"
#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/presence-mixin.h>
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "extensions/extensions.h"    /* for Decloak */

#include "connection.h"
#include "debug.h"
#include "presence-cache.h"
#include "presence.h"
#include "roster.h"

static const TpPresenceStatusOptionalArgumentSpec gabble_status_arguments[] = {
  { "message",  "s", NULL, NULL },
  { "priority", "n", NULL, NULL },
  { NULL, NULL, NULL, NULL }
};


/* order must match PresenceId enum in connection.h */
/* in increasing order of presence */
static const TpPresenceStatusSpec gabble_statuses[] = {
  { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE,
    gabble_status_arguments, NULL, NULL },
  { "unknown", TP_CONNECTION_PRESENCE_TYPE_UNKNOWN, FALSE,
    gabble_status_arguments, NULL, NULL },
  { "error", TP_CONNECTION_PRESENCE_TYPE_ERROR, FALSE,
    gabble_status_arguments, NULL, NULL },
  { "hidden", TP_CONNECTION_PRESENCE_TYPE_HIDDEN, TRUE, gabble_status_arguments,
    NULL, NULL },
  { "xa", TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY, TRUE,
    gabble_status_arguments, NULL, NULL },
  { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, gabble_status_arguments,
    NULL, NULL },
  { "dnd", TP_CONNECTION_PRESENCE_TYPE_BUSY, TRUE, gabble_status_arguments,
    NULL, NULL },
  { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE,
    gabble_status_arguments, NULL, NULL },
  { "chat", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE,
    gabble_status_arguments, NULL, NULL },
  { NULL, 0, FALSE, NULL, NULL, NULL }
};

static LmHandlerResult set_xep0186_invisible_cb (GabbleConnection *conn,
    LmMessage *sent_msg, LmMessage *reply_msg, GObject *obj,
    gpointer user_data);

static LmHandlerResult set_xep0126_invisible_cb (GabbleConnection *conn,
    LmMessage *sent_msg, LmMessage *reply_msg, GObject *obj,
    gpointer user_data);

static LmHandlerResult create_invisible_privacy_list_cb (
    GabbleConnection *conn, LmMessage *sent_msg, LmMessage *reply_msg,
    GObject *obj, gpointer user_data);

GQuark
conn_presence_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("conn-presence-error");
  return quark;
}

static GHashTable *
construct_contact_statuses_cb (GObject *obj,
                               const GArray *contact_handles,
                               GError **error)
{
  GabbleConnection *self = GABBLE_CONNECTION (obj);
  TpBaseConnection *base = (TpBaseConnection *) self;
  guint i;
  TpHandle handle;
  GHashTable *contact_statuses, *parameters;
  TpPresenceStatus *contact_status;
  GValue *message;
  GabblePresence *presence;
  GabblePresenceId status;
  const gchar *status_message;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (handle_repo, contact_handles, FALSE, error))
    return NULL;

  contact_statuses = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) tp_presence_status_free);

  for (i = 0; i < contact_handles->len; i++)
    {
      handle = g_array_index (contact_handles, TpHandle, i);

      if (handle == base->self_handle)
        presence = self->self_presence;
      else
        presence = gabble_presence_cache_get (self->presence_cache, handle);

      if (presence)
        {
          status = presence->status;
          status_message = presence->status_message;
        }
      else
        {
         if (gabble_roster_handle_get_subscription (self->roster, handle)
             & GABBLE_ROSTER_SUBSCRIPTION_TO)
           status = GABBLE_PRESENCE_OFFLINE;
         else
           status = GABBLE_PRESENCE_UNKNOWN;

         status_message = NULL;
        }

      parameters = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
          (GDestroyNotify) tp_g_value_slice_free);

      if (status_message != NULL)
        {
          message = tp_g_value_slice_new (G_TYPE_STRING);
          g_value_set_static_string (message, status_message);
          g_hash_table_insert (parameters, "message", message);
        }

      contact_status = tp_presence_status_new (status, parameters);
      g_hash_table_destroy (parameters);

      g_hash_table_insert (contact_statuses, GUINT_TO_POINTER (handle),
          contact_status);
    }

  return contact_statuses;
}


/**
 * conn_presence_emit_presence_update:
 * @self: A #GabbleConnection
 * @contact_handles: A zero-terminated array of #TpHandle for
 *                    the contacts to emit presence for
 *
 * Emits the Telepathy PresenceUpdate signal with the current
 * stored presence information for the given contact.
 */
void
conn_presence_emit_presence_update (
    GabbleConnection *self,
    const GArray *contact_handles)
{
  GHashTable *contact_statuses;

  contact_statuses = construct_contact_statuses_cb ((GObject *) self,
      contact_handles, NULL);
  tp_presence_mixin_emit_presence_update ((GObject *) self, contact_statuses);
  g_hash_table_destroy (contact_statuses);
}


/**
 * emit_one_presence_update:
 * Convenience function for calling conn_presence_emit_presence_update with
 * one handle.
 */

static void
emit_one_presence_update (GabbleConnection *self,
                          TpHandle handle)
{
  GArray *handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);

  g_array_insert_val (handles, 0, handle);
  conn_presence_emit_presence_update (self, handles);
  g_array_free (handles, TRUE);
}

static void
set_xep0126_invisible (GabbleConnection *self,
    gboolean invisible,
    GSimpleAsyncResult *result)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  WockyXmppStanza *iq;
  GError *error = NULL;

  if (invisible)
    iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
        WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
        '(', "query",
        ':', NS_PRIVACY,
        '(', "active",
        '@', "name", "invisible",
        ')',
        ')',
        NULL);
  else
    iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
        WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
        '(', "query",
        ':', NS_PRIVACY,
        '(', "active",
        ')',
        ')',
        NULL);

  g_object_ref (result);

  if (base->status == TP_CONNECTION_STATUS_CONNECTED)
    {
      if (!gabble_connection_send_presence (self,
              LM_MESSAGE_SUB_TYPE_UNAVAILABLE, "", "", &error))
          goto OUT;
    }

  _gabble_connection_send_with_reply (self, (LmMessage *) iq,
      set_xep0126_invisible_cb, NULL, result, &error);

 OUT:
  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_simple_async_result_complete (result);
      g_error_free (error);
      g_object_unref (result);
    }

  g_object_unref (iq);
}

static LmHandlerResult
set_xep0126_invisible_cb (GabbleConnection *conn,
    LmMessage *sent_msg,
    LmMessage *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GSimpleAsyncResult *result = (GSimpleAsyncResult *) user_data;
  GError *error = NULL;

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      g_simple_async_result_set_error (result,
          CONN_PRESENCE_ERROR, CONN_PRESENCE_ERROR_SET_PRIVACY_LIST,
          "error setting 'invisible' privacy list");
    }
  else
    {
      LmMessageNode *node = lm_message_node_find_child (
          lm_message_get_node (sent_msg), "active");

      g_assert (node != NULL);

      if (g_strcmp0 (lm_message_node_get_attribute (node, "name"),
              "invisible") == 0)
        {
          gabble_connection_send_presence (conn, LM_MESSAGE_SUB_TYPE_NOT_SET,
              "", "", &error);
          gabble_muc_factory_broadcast_presence (conn->muc_factory);
        }
      else
        {
          conn_presence_signal_own_presence (conn, NULL, &error);
        }
    }

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
set_xep0186_invisible (GabbleConnection *self,
    gboolean invisible,
    GSimpleAsyncResult *result)
{
  GError *error = NULL;
  WockyXmppStanza *iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
        '(', invisible ? "invisible" : "visible",
         ':', NS_INVISIBLE,
        ')',
      NULL);

  g_object_ref (result);

  if (!_gabble_connection_send_with_reply (self, (LmMessage *) iq,
          set_xep0186_invisible_cb, NULL, result, &error))
    {
      if (error != NULL)
        {
          g_simple_async_result_set_from_error (result, error);
          g_error_free (error);
        }

      g_simple_async_result_complete (result);
      g_object_unref (result);
    }

  g_object_unref (iq);
}

static LmHandlerResult
set_xep0186_invisible_cb (GabbleConnection *conn,
    LmMessage *sent_msg,
    LmMessage *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GSimpleAsyncResult *result = (GSimpleAsyncResult *) user_data;
  GError *error = NULL;

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      g_simple_async_result_set_error (result,
          CONN_PRESENCE_ERROR, CONN_PRESENCE_ERROR_SET_INVISIBLE,
          "error setting XEP-0186 (in)visiblity");
    }
  else
    {
      LmMessageNode *node = lm_message_get_node (sent_msg);
      if (lm_message_node_find_child (node, "invisible") != NULL)
        {
          gabble_muc_factory_broadcast_presence (conn->muc_factory);
        }
      else
        {
          conn_presence_signal_own_presence (conn, NULL, &error);
        }
    }

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
presence_create_invisible_privacy_list (GabbleConnection *self,
    GSimpleAsyncResult *result)
{
  GError *error = NULL;
  WockyXmppStanza *iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
        '(', "query",
          ':', NS_PRIVACY,
          '(', "list",
            '@', "name", "invisible",
            '(', "item",
              '@', "action", "deny",
              '@', "order", "1",
              '(', "presence-out", ')',
            ')',
          ')',
        ')',
      NULL);

  g_object_ref (result);

  if (!_gabble_connection_send_with_reply (self, (LmMessage *) iq,
          create_invisible_privacy_list_cb, NULL, result, &error))
    {
      if (error != NULL)
        {
          g_simple_async_result_set_from_error (result, error);
          g_error_free (error);
        }

      g_simple_async_result_complete (result);
      g_object_unref (result);
    }

  g_object_unref (iq);
}

static LmHandlerResult
create_invisible_privacy_list_cb (GabbleConnection *conn,
    LmMessage *sent_msg,
    LmMessage *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GSimpleAsyncResult *result = (GSimpleAsyncResult *) user_data;

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      g_simple_async_result_set_error (result,
          CONN_PRESENCE_ERROR, CONN_PRESENCE_ERROR_CREATE_PRIVACY_LIST,
          "error creating 'invisible' privacy list");
      g_simple_async_result_complete (result);
    }
  else if (conn->self_presence->status == GABBLE_PRESENCE_HIDDEN)
    {
      set_xep0126_invisible (conn, TRUE, result);
    }
  else
    {
      g_simple_async_result_complete (result);
    }

  g_object_unref (result);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

void
conn_presence_set_initial_presence_async (GabbleConnection *self,
    GAsyncReadyCallback callback, gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, conn_presence_set_initial_presence_finished);

  if ((self->features & GABBLE_CONNECTION_FEATURES_INVISIBLE) != 0 &&
      self->self_presence->status == GABBLE_PRESENCE_HIDDEN)
    {
      set_xep0186_invisible (self, TRUE, result);
    }
  else if ((self->features & GABBLE_CONNECTION_FEATURES_PRIVACY) != 0 &&
      (self->features & GABBLE_CONNECTION_FEATURES_INVISIBLE) == 0)
    {
      presence_create_invisible_privacy_list (self, result);
    }
  else
    {
      GError *error = NULL;

      if ((self->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) == 0
          && self->self_presence->status == GABBLE_PRESENCE_HIDDEN)
        self->self_presence->status = GABBLE_PRESENCE_DND;

      /* send presence to the server to indicate availability */
      /* TODO: some way for the user to set this */
      if (!conn_presence_signal_own_presence (self, NULL, &error))
        {
          g_simple_async_result_set_from_error (result, error);
          g_error_free (error);
        }
      g_simple_async_result_complete (result);
    }

  g_object_unref (result);
}

gboolean
conn_presence_set_initial_presence_finished (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  gboolean rv = TRUE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (self), conn_presence_set_initial_presence_finished), FALSE);

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    rv = FALSE;

  return rv;
}

/**
 * conn_presence_signal_own_presence:
 * @self: A #GabbleConnection
 * @to: bare or full JID for directed presence, or NULL
 * @error: pointer in which to return a GError in case of failure.
 *
 * Signal the user's stored presence to @to, or to the jabber server
 *
 * Retuns: FALSE if an error occurred
 */
gboolean
conn_presence_signal_own_presence (GabbleConnection *self,
    const gchar *to,
    GError **error)
{
  GabblePresence *presence = self->self_presence;
  TpBaseConnection *base = (TpBaseConnection *) self;
  LmMessage *message = gabble_presence_as_message (presence, to);
  gboolean ret;

  if (presence->status == GABBLE_PRESENCE_HIDDEN && to == NULL)
    {
      if ((self->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) != 0)
        lm_message_node_set_attribute (lm_message_get_node (message),
            "type", "invisible");
      /* FIXME: or if sending directed presence, should we add
       * <show>away</show>? */
    }

  gabble_connection_fill_in_caps (self, message);

  ret = _gabble_connection_send (self, message, error);

  lm_message_unref (message);

  /* FIXME: if sending broadcast presence, should we echo it to everyone we
   * previously sent directed presence to? (Perhaps also GC them after a
   * while?) */

  if (to == NULL && base->status == TP_CONNECTION_STATUS_CONNECTED)
    gabble_muc_factory_broadcast_presence (self->muc_factory);

  return ret;
}

gboolean
conn_presence_visible_to (GabbleConnection *self,
    TpHandle recipient)
{
  if (self->self_presence->status == GABBLE_PRESENCE_HIDDEN)
    return FALSE;

  if ((gabble_roster_handle_get_subscription (self->roster, recipient)
      & GABBLE_ROSTER_SUBSCRIPTION_FROM) == 0)
    return FALSE;

  /* FIXME: other reasons they might not be able to see our presence? */

  return TRUE;
}

static void
toggle_presence_visibility_async (GabbleConnection *self,
    GAsyncReadyCallback callback)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, NULL, NULL);
  gboolean set_invisible =
    (self->self_presence->status == GABBLE_PRESENCE_HIDDEN);

  if ((self->features & GABBLE_CONNECTION_FEATURES_INVISIBLE) != 0)
    {
      /* XEP-0186 */
      set_xep0186_invisible (self, set_invisible, result);
    }
  else if ((self->features & GABBLE_CONNECTION_FEATURES_PRIVACY) != 0)
    {
      /* XEP-0126 */
      set_xep0126_invisible (self, set_invisible, result);
    }
  else
    {
      GError *error;
      /* XEP-0018 */
      if (!conn_presence_signal_own_presence (self, NULL, &error))
        {
          g_simple_async_result_set_from_error (result, error);
          g_error_free (error);
        }
      g_simple_async_result_complete (result);
    }

  g_object_unref (result);
}

static void
toggle_presence_visibility_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = (GabbleConnection *) source_object;
  TpBaseConnection *base = (TpBaseConnection *) self;
  GError *error = NULL;

  g_return_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (self),
          NULL));

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res),
      &error))
    {
      DEBUG ("Error setting visibility, falling back to dnd: %s",
          error->message);

      g_error_free (error);
      error = NULL;

      self->self_presence->status = GABBLE_PRESENCE_DND;

      if (!conn_presence_signal_own_presence (self, NULL, &error))
        {
          DEBUG ("Failed to set fallback status: %s", error->message);
          g_error_free (error);
        }
    }

  emit_one_presence_update (self, base->self_handle);
}

static gboolean
set_own_status_cb (GObject *obj,
                   const TpPresenceStatus *status,
                   GError **error)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabblePresenceId i = GABBLE_PRESENCE_AVAILABLE;
  const gchar *message_str = NULL;
  gchar *resource;
  gint8 prio;
  gboolean retval = TRUE;
  GabblePresenceId prev_status = conn->self_presence->status;

  g_object_get (conn,
        "resource", &resource,
        "priority", &prio,
        NULL);

  if (status)
    {
      GHashTable *args = status->optional_arguments;
      GValue *message = NULL, *priority = NULL;

      i = status->index;

      /* Workaround for tp-glib not checking whether we support setting
       * a particular status (can be removed once we depend on tp-glib
       * with the check enabled). Assumes PresenceId value ordering. */
      if (i < GABBLE_PRESENCE_HIDDEN)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Status '%s' can not be requested in this connection",
                gabble_statuses[i].name);
          retval = FALSE;
          goto OUT;
        }

      if (args != NULL)
        {
          message = g_hash_table_lookup (args, "message");
          priority = g_hash_table_lookup (args, "priority");
        }

      if (message)
        {
          if (!G_VALUE_HOLDS_STRING (message))
            {
              DEBUG ("got a status message which was not a string");
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Status argument 'message' requires a string");
              retval = FALSE;
              goto OUT;
            }
          message_str = g_value_get_string (message);
        }

      if (priority)
        {
          if (!G_VALUE_HOLDS_INT (priority))
            {
              DEBUG ("got a priority value which was not a signed integer");
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "Status argument 'priority' requires a signed integer");
              retval = FALSE;
              goto OUT;
            }
          prio = CLAMP (g_value_get_int (priority), G_MININT8, G_MAXINT8);
        }
    }

  if (gabble_presence_update (conn->self_presence, resource, i,
          message_str, prio))
    {
      if (base->status != TP_CONNECTION_STATUS_CONNECTED)
        {
          retval = TRUE;
          goto OUT;
        }
      if (prev_status != i && (prev_status == GABBLE_PRESENCE_HIDDEN ||
            i  == GABBLE_PRESENCE_HIDDEN))
        {
          toggle_presence_visibility_async (conn,
              toggle_presence_visibility_cb);
        }
      else
        {
          retval = conn_presence_signal_own_presence (conn, NULL, error);
          emit_one_presence_update (conn, base->self_handle);
        }

    }

OUT:
  g_free (resource);

  return retval;
}


static void
connection_presences_updated_cb (
    GabblePresenceCache *cache,
    const GArray *handles,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  conn_presence_emit_presence_update (conn, handles);
}


static void
connection_status_changed_cb (
    GabbleConnection *conn,
    TpConnectionStatus status,
    TpConnectionStatusReason reason,
    gpointer user_data)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (status == TP_CONNECTION_STATUS_CONNECTED)
    emit_one_presence_update (conn, base->self_handle);
}


static gboolean
status_available_cb (GObject *obj, guint status)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabbleConnectionFeatures invisibility_features =
    GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE |
    GABBLE_CONNECTION_FEATURES_PRIVACY | GABBLE_CONNECTION_FEATURES_INVISIBLE;
  TpConnectionPresenceType presence_type =
    gabble_statuses[status].presence_type;

  /* If we've gone online and found that the server doesn't support invisible,
   * reject it.
   */

  if (base->status == TP_CONNECTION_STATUS_CONNECTED &&
      presence_type == TP_CONNECTION_PRESENCE_TYPE_HIDDEN &&
      (conn->features & invisibility_features) == 0)
    return FALSE;
  else
    return TRUE;
}


void
conn_presence_class_init (GabbleConnectionClass *klass)
{
  tp_presence_mixin_class_init ((GObjectClass *) klass,
      G_STRUCT_OFFSET (GabbleConnectionClass, presence_class),
      status_available_cb, construct_contact_statuses_cb,
      set_own_status_cb, gabble_statuses);

  tp_presence_mixin_simple_presence_init_dbus_properties (
    (GObjectClass *) klass);
}


void
conn_presence_init (GabbleConnection *conn)
{
  g_signal_connect (conn->presence_cache, "presences-updated",
      G_CALLBACK (connection_presences_updated_cb), conn);
  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), conn);

  tp_presence_mixin_init ((GObject *) conn,
      G_STRUCT_OFFSET (GabbleConnection, presence));

  tp_presence_mixin_simple_presence_register_with_contacts_mixin (
      G_OBJECT (conn));
}


void
conn_presence_finalize (GabbleConnection *conn)
{
  tp_presence_mixin_finalize ((GObject *) conn);
}


void
conn_presence_iface_init (gpointer g_iface, gpointer iface_data)
{
  tp_presence_mixin_iface_init (g_iface, iface_data);
}

static void
conn_presence_send_directed_presence (
    GabbleSvcConnectionInterfaceGabbleDecloak *conn,
    guint contact,
    gboolean full,
    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (conn);
  TpBaseConnection *base = TP_BASE_CONNECTION (conn);
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  const gchar *jid = tp_handle_inspect (contact_handles, contact);
  gboolean ok;
  GError *error = NULL;

  g_return_if_fail (jid != NULL);

  /* We don't strictly respect @full - we'll always send full presence to
   * people we think ought to be receiving it anyway, because if we didn't,
   * you could confuse them by sending directed presence that was less
   * informative than the broadcast presence they already saw. */
  if (full || conn_presence_visible_to (self, contact))
    {
      ok = conn_presence_signal_own_presence (self, jid, &error);
    }
  else
    {
      ok = gabble_connection_send_capabilities (self, jid, &error);
    }

  if (ok)
    {
      gabble_svc_connection_interface_gabble_decloak_return_from_send_directed_presence (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

void
conn_decloak_emit_requested (GabbleConnection *conn,
    TpHandle contact,
    const gchar *reason,
    gboolean decloaked)
{
  gabble_svc_connection_interface_gabble_decloak_emit_decloak_requested (conn,
      contact, reason, decloaked);
}

void
conn_decloak_iface_init (gpointer g_iface,
    gpointer iface_data)
{
#define IMPLEMENT(x) \
  gabble_svc_connection_interface_gabble_decloak_implement_##x (\
  g_iface, conn_presence_##x)
  IMPLEMENT (send_directed_presence);
#undef IMPLEMENT
}
