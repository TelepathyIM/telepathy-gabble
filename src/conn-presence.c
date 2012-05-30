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
#include <stdlib.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <wocky/wocky.h>

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "extensions/extensions.h"    /* for Decloak */

#include "connection.h"
#include "debug.h"
#include "plugin-loader.h"
#include "presence-cache.h"
#include "presence.h"
#include "roster.h"
#include "conn-util.h"
#include "util.h"

#define GOOGLE_SHARED_STATUS_VERSION "2"

typedef enum {
    INVISIBILITY_METHOD_NONE = 0,
    INVISIBILITY_METHOD_PRESENCE_INVISIBLE, /* presence type=invisible */
    INVISIBILITY_METHOD_PRIVACY,
    INVISIBILITY_METHOD_INVISIBLE_COMMAND,
    INVISIBILITY_METHOD_SHARED_STATUS
} InvisibilityMethod;

struct _GabbleConnectionPresencePrivate {
    InvisibilityMethod invisibility_method;
    guint iq_list_push_id;
    gchar *invisible_list_name;

    /* Mapping between status "show" strings, and shared statuses */
    GHashTable *shared_statuses;

    /* Map of presence statuses backed by privacy lists. This
     * will be NULL until we receive a (possibly empty) list of
     * all lists in get_existing_privacy_lists_cb().
     *
     * gchar *presence_status_name → gchar *privacy_list
     */
    GHashTable *privacy_statuses;

    /* Are all of the connected resources complying to version 2 */
    gboolean shared_status_compat;

    /* Max length of status message */
    gint max_status_message_length;

    /* Max statuses in a shared status list */
    gint max_shared_statuses;

    /* The shared status IQ handler */
    guint iq_shared_status_cb;

    /* The previous presence when using shared status */
    GabblePresenceId previous_shared_status;
};

static const TpPresenceStatusOptionalArgumentSpec gabble_status_arguments[] = {
  { "message",  "s", NULL, NULL },
  { "priority", "n", NULL, NULL },
  { NULL, NULL, NULL, NULL }
};


/* order must match PresenceId enum in connection.h */
/* in increasing order of presence */
static const TpPresenceStatusSpec gabble_base_statuses[] = {
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

static TpPresenceStatusSpec *gabble_statuses = NULL;

/* prototypes */

static void set_xep0186_invisible_cb (GabbleConnection *conn,
    WockyStanza *sent_msg, WockyStanza *reply_msg, GObject *obj,
    gpointer user_data);

static void activate_current_privacy_list_cb (
    GabbleConnection *conn, WockyStanza *sent_msg, WockyStanza *reply_msg,
    GObject *obj, gpointer user_data);

static void setup_invisible_privacy_list_async (GabbleConnection *self,
    GAsyncReadyCallback callback, gpointer user_data);

static gboolean iq_privacy_list_push_cb (
    WockyPorter *porter,
    WockyStanza *message,
    gpointer user_data);

static void verify_invisible_privacy_list_cb (
    GabbleConnection *conn, WockyStanza *sent_msg, WockyStanza *reply_msg,
    GObject *obj, gpointer user_data);

static void toggle_presence_visibility_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data);

static gboolean toggle_presence_visibility_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error);

static void toggle_initial_presence_visibility_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data);

static void activate_current_privacy_list (GabbleConnection *self,
    GSimpleAsyncResult *result);

/* actual code! */

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
         if (gabble_roster_handle_sends_presence_to_us (self->roster, handle))
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
      g_hash_table_unref (parameters);

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
  g_hash_table_unref (contact_statuses);
}


/*
 * emit_presences_changed_for_self:
 * @self: A #GabbleConnection
 *
 * Convenience function for emitting presence update signals on D-Bus for our
 * self handle.
 */
static void
emit_presences_changed_for_self (GabbleConnection *self)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GArray *handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);

  g_array_insert_val (handles, 0, base->self_handle);
  conn_presence_emit_presence_update (self, handles);
  g_array_unref (handles);
}

static WockyStanza *
build_shared_status_stanza (GabbleConnection *self)
{
  GabblePresence *presence = self->self_presence;
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  const gchar *bare_jid = conn_util_get_bare_self_jid (self);
  gpointer key, value;
  GHashTableIter iter;
  WockyNode *query_node = NULL;
  WockyStanza *iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, NULL, bare_jid,
        '(', "query",
          ':', NS_GOOGLE_SHARED_STATUS,
          '*', &query_node,
          '@', "version", GOOGLE_SHARED_STATUS_VERSION,
          '(', "status",
            '$', presence->status_message,
          ')',
          '(', "show",
            '$', presence->status == GABBLE_PRESENCE_DND ? "dnd" : "default",
          ')',
        ')',
      NULL);

  g_hash_table_iter_init (&iter, priv->shared_statuses);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      gchar **status_iter;
      gchar **statuses = (gchar **) value;
      WockyNode *list_node = wocky_node_add_child (query_node, "status-list");

      wocky_node_set_attribute (list_node, "show", (const gchar *) key);

      for (status_iter = statuses; *status_iter != NULL; status_iter++)
        wocky_node_add_child_with_content (list_node, "status", *status_iter);
    }

  wocky_node_set_attribute (wocky_node_add_child (query_node, "invisible"), "value",
      presence->status == GABBLE_PRESENCE_HIDDEN ? "true" : "false");

  return iq;
}

static gboolean
is_presence_away (GabblePresenceId status)
{
  return status == GABBLE_PRESENCE_AWAY || status == GABBLE_PRESENCE_XA;
}

static void
set_shared_status_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GabblePresence *presence = self->self_presence;
  GError *error = NULL;

  if (!conn_util_send_iq_finish (self, res, NULL, &error))
    {
      g_simple_async_result_set_error (result,
          CONN_PRESENCE_ERROR, CONN_PRESENCE_ERROR_SET_SHARED_STATUS,
          "error setting Google shared status: %s", error->message);
    }
  else
    {
      gabble_muc_factory_broadcast_presence (self->muc_factory);

      if (is_presence_away (priv->previous_shared_status))
        {
          /* To use away and xa we need to send a <presence/> to the server,
           * but then GTalk also expects us to leave the status using
           * <presence/> too. */
          conn_presence_signal_own_presence (self, NULL, &error);
        }
      else if (priv->previous_shared_status == GABBLE_PRESENCE_HIDDEN &&
          is_presence_away (presence->status))
        {
          /* We sent the shared status change to leave the invisibility, so
           * now we can actually go to away / xa. */
          conn_presence_signal_own_presence (self, NULL, &error);
          emit_presences_changed_for_self (self);
        }

      priv->previous_shared_status = presence->status;
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);

  if (error != NULL)
    g_error_free (error);
}

static void
insert_presence_to_shared_statuses (GabbleConnection *self)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GabblePresence *presence = self->self_presence;
  const gchar *show = presence->status == GABBLE_PRESENCE_DND ? "dnd" : "default";
  gchar **statuses = g_hash_table_lookup (priv->shared_statuses, show);

  if (presence->status_message == NULL || is_presence_away (presence->status))
    return;

  if (statuses == NULL)
    {
      statuses = g_new0 (gchar *, 2);
      statuses[0] = g_strdup (presence->status_message);
      g_hash_table_insert (priv->shared_statuses, g_strdup (show), statuses);
    }
  else
    {
      guint i;
      guint list_len = MIN (priv->max_shared_statuses,
          (gint) g_strv_length (statuses) + 1);
      gchar **new_statuses = g_new0 (gchar *, list_len + 1);

      new_statuses[0] = g_strdup (presence->status_message);

      for (i = 1; i < list_len; i++)
        new_statuses[i] = g_strdup (statuses[i - 1]);

      g_hash_table_insert (priv->shared_statuses, g_strdup (show), new_statuses);
    }
}

static void
set_shared_status (GabbleConnection *self,
    GSimpleAsyncResult *result)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GabblePresence *presence = self->self_presence;

  g_object_ref (result);

  /* Away is treated like idleness in GTalk; it's per connection and not
   * global. To set the presence as away we use the traditional <presence/>,
   * but, if we were invisible, we need to first leave invisibility. */
  if (!is_presence_away (presence->status) ||
      priv->previous_shared_status == GABBLE_PRESENCE_HIDDEN)
    {
      WockyStanza *iq;

      DEBUG ("shared status invisibility is %savailable",
          priv->shared_status_compat ? "" : "un");

      if (presence->status == GABBLE_PRESENCE_HIDDEN && !priv->shared_status_compat)
        presence->status = GABBLE_PRESENCE_DND;

      insert_presence_to_shared_statuses (self);

      iq = build_shared_status_stanza (self);

      conn_util_send_iq_async (self, iq, NULL, set_shared_status_cb, result);

      g_object_unref (iq);
    }
  else
    {
      gboolean retval;
      GError *error = NULL;

      DEBUG ("not updating shared status as it's not supported for away");

      retval = conn_presence_signal_own_presence (self, NULL, &error);
      if (!retval)
        {
          g_simple_async_result_set_from_error (result, error);
          g_error_free (error);
        }

      emit_presences_changed_for_self (self);

      priv->previous_shared_status = presence->status;

      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
    }
}

static void
set_shared_status_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, set_shared_status_async);

  set_shared_status (self, result);

  g_object_unref (result);
}

static gboolean
set_shared_status_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self, set_shared_status_async);
}

static void
set_xep0186_invisible (GabbleConnection *self,
    gboolean invisible,
    GSimpleAsyncResult *result)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GError *error = NULL;
  const gchar *element = invisible ? "invisible" : "visible";
  WockyStanza *iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
        '(', element, ':', NS_INVISIBLE, ')',
      NULL);

  g_object_ref (result);

  if (!invisible && base->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      if (priv->privacy_statuses != NULL)
        {
          /* A plugin might need to activate a privacy list */
          activate_current_privacy_list (self, result);
          g_object_unref (result);
        }
      else
        {
          conn_presence_signal_own_presence (self, NULL, &error);
          g_simple_async_result_complete_in_idle (result);
          g_object_unref (result);
        }
    }
  else if (!_gabble_connection_send_with_reply (self, (WockyStanza *) iq,
          set_xep0186_invisible_cb, NULL, result, &error))
    {
      g_simple_async_result_set_from_error (result, error);
      g_simple_async_result_complete_in_idle (result);

      g_object_unref (result);
      g_error_free (error);
    }

  g_object_unref (iq);
}

static void
set_xep0186_invisible_cb (GabbleConnection *conn,
    WockyStanza *sent_msg,
    WockyStanza *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  if (wocky_stanza_extract_errors (reply_msg, NULL, &error, NULL, NULL))
    {
      g_simple_async_result_set_error (result,
          CONN_PRESENCE_ERROR, CONN_PRESENCE_ERROR_SET_INVISIBLE,
          "error setting XEP-0186 (in)visiblity: %s", error->message);
      g_clear_error (&error);
    }
  else
    {
      /* If we've become visible, broadcast our new presence and update our MUC
       * presences.
       *
       * If we've become invisible, we only need to do the latter, but the
       * server will block the former in any case, so let's not bother adding
       * complexity.
       */
      conn_presence_signal_own_presence (conn, NULL, &error);
    }

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}


static void
activate_current_privacy_list (GabbleConnection *self,
    GSimpleAsyncResult *result)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  WockyStanza *iq;
  const gchar *list_name = NULL;
  gboolean invisible;
  GabblePresence *presence = self->self_presence;
  GError *error = NULL;
  WockyNode *active_node;

  g_assert (priv->privacy_statuses != NULL);

  list_name = g_hash_table_lookup (priv->privacy_statuses,
    gabble_statuses[presence->status].name);
  invisible = (gabble_statuses[presence->status].presence_type ==
      TP_CONNECTION_PRESENCE_TYPE_HIDDEN);

  DEBUG ("Privacy status %s, backed by %s",
      gabble_statuses[presence->status].name,
      list_name ? list_name : "(no list)");

  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
      '(', "query", ':', NS_PRIVACY,
        '(', "active",
          '*', &active_node,
        ')',
      ')',
      NULL);

  if (list_name != NULL)
    wocky_node_set_attribute (active_node, "name", list_name);

  g_object_ref (result);

  if (base->status == TP_CONNECTION_STATUS_CONNECTED && invisible)
    {
      if (!gabble_connection_send_presence (self,
              WOCKY_STANZA_SUB_TYPE_UNAVAILABLE, NULL, NULL, &error))
        goto ERROR;
    }
  /* If we're still connecting and there's no list to be set, we don't
   * need to bother with removing the active list; just shortcut to
   * signalling our presence. */
  else if (list_name == NULL &&
      base->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      if (!conn_presence_signal_own_presence (self, NULL, &error))
        goto ERROR;

      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);

      goto OUT;
    }

  _gabble_connection_send_with_reply (self, (WockyStanza *) iq,
      activate_current_privacy_list_cb, NULL, result, &error);

 ERROR:
  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_simple_async_result_complete_in_idle (result);
      g_error_free (error);
      g_object_unref (result);
    }

 OUT:
  g_object_unref (iq);
}

static void
activate_current_privacy_list_cb (GabbleConnection *conn,
    WockyStanza *sent_msg,
    WockyStanza *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  if (wocky_stanza_extract_errors (reply_msg, NULL, &error, NULL, NULL))
    {
      g_simple_async_result_set_error (result,
          CONN_PRESENCE_ERROR, CONN_PRESENCE_ERROR_SET_PRIVACY_LIST,
          "error setting requested privacy list: %s", error->message);
      g_clear_error (&error);
    }
  else
    {
      /* Whether we were becoming invisible or visible, we now need to
       * re-broadcast our presence.
       */
      conn_presence_signal_own_presence (conn, NULL, &error);
      emit_presences_changed_for_self (conn);
    }

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
disable_invisible_privacy_list (GabbleConnection *self)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;

  if (priv->invisibility_method == INVISIBILITY_METHOD_PRIVACY)
    {
      if (self->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE)
        priv->invisibility_method = INVISIBILITY_METHOD_PRESENCE_INVISIBLE;
      else
        priv->invisibility_method = INVISIBILITY_METHOD_NONE;

      DEBUG ("Set invisibility method to %s",
          (priv->invisibility_method == INVISIBILITY_METHOD_PRESENCE_INVISIBLE)
          ? "presence-invisible" : "none");
    }
}

static void
create_invisible_privacy_list_reply_cb (GabbleConnection *conn,
    WockyStanza *sent_msg,
    WockyStanza *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  if (wocky_stanza_extract_errors (reply_msg, NULL, &error, NULL, NULL))
    {
      g_simple_async_result_set_from_error (result, error);
      g_free (error);
    }

  g_simple_async_result_complete_in_idle (result);

  g_object_unref (result);
}

static void
create_invisible_privacy_list_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GError *error = NULL;
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, create_invisible_privacy_list_async);
  WockyStanza *iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, NULL, NULL,
        '(', "query",
          ':', NS_PRIVACY,
          '(', "list",
            '@', "name", self->presence_priv->invisible_list_name,
            '(', "item",
              '@', "action", "deny",
              '@', "order", "1",
              '(', "presence-out", ')',
            ')',
          ')',
        ')',
      NULL);

  DEBUG ("Creating '%s'", self->presence_priv->invisible_list_name);

  if (!_gabble_connection_send_with_reply (self, (WockyStanza *) iq,
          create_invisible_privacy_list_reply_cb, NULL, result, &error))
    {
      g_simple_async_result_set_from_error (result, error);
      g_simple_async_result_complete_in_idle (result);

      g_object_unref (result);
      g_error_free (error);
    }

  g_object_unref (iq);
}

static gboolean
create_invisible_privacy_list_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self, create_invisible_privacy_list_async);
}

static void
create_invisible_privacy_list_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GError *error = NULL;

  if (!create_invisible_privacy_list_finish (self, result, &error))
    {
      DEBUG ("Error creating privacy list: %s", error->message);

      disable_invisible_privacy_list (self);

      g_error_free (error);
    }

  g_assert (priv->privacy_statuses != NULL);

  /* "hidden" presence status will be backed by the invisible list */
  g_hash_table_insert (priv->privacy_statuses,
      g_strdup ("hidden"),
      g_strdup (priv->invisible_list_name));

  toggle_presence_visibility_async (self,
      toggle_initial_presence_visibility_cb, user_data);
}

static gboolean
store_shared_statuses (GabbleConnection *self,
    WockyNode *query_node)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GabblePresenceId presence_id = self->self_presence->status;
  const gchar *status_message = NULL;
  const gchar *min_version = wocky_node_get_attribute (query_node, "status-min-ver");
  WockyNodeIter iter;
  gchar *resource;
  guint8 prio;
  gboolean rv;
  WockyNode *node;
  gboolean dnd = FALSE;
  gboolean invisible = FALSE;

  DEBUG ("status-min-ver %s", min_version);

  g_object_get (self,
        "resource", &resource,
        "priority", &prio,
        NULL);

  if (priv->shared_statuses != NULL)
    g_hash_table_unref (priv->shared_statuses);

  priv->shared_statuses = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_strfreev);

  wocky_node_iter_init (&iter, query_node, NULL, NULL);
  while (wocky_node_iter_next (&iter, &node))
    {
      if (g_strcmp0 (node->name, "status-list") == 0)
        {
          WockyNodeIter list_iter;
          WockyNode *list_item;
          GPtrArray *statuses;
          const gchar *show = wocky_node_get_attribute (node, "show");

          if (show == NULL)
            continue;

          statuses = g_ptr_array_new ();

          wocky_node_iter_init (&list_iter, node, "status", NULL);
          while (wocky_node_iter_next (&list_iter, &list_item))
            g_ptr_array_add (statuses, g_strdup (list_item->content));

          g_ptr_array_add (statuses, NULL);

          g_hash_table_insert (priv->shared_statuses, g_strdup (show),
              g_ptr_array_free (statuses, FALSE));
        }
      else if (g_strcmp0 (node->name, "status") == 0)
        {
          status_message = node->content;
        }
      else if (g_strcmp0 (node->name, "show") == 0)
        {
          dnd = g_strcmp0 (node->content, "dnd") == 0;
        }
      else if (g_strcmp0 (node->name, "invisible") == 0)
        {
          invisible = g_strcmp0 (wocky_node_get_attribute (node, "value"), "true") == 0;
        }
    }

  /* - status-min-ver == 0 means that at least one resource doesn't support
   *   Google shared status, so we fallback to "dnd".
   * - status-min-ver == 1 means that all the resources support shared
   *   status, but at least one doesn't support invisibility; we have to fall
   *   fall back to "dnd".
   * - status-miv-ver == 2 means that all the resources support shared status
   *   with invisibility.
   * - any other value means that the other resources will have to fall back
   *   to version 2 for us. */
  priv->shared_status_compat =
    (g_strcmp0 (min_version, "0") != 0 && g_strcmp0 (min_version, "1") != 0);

  if (invisible)
    {
      if (priv->shared_status_compat)
        presence_id = GABBLE_PRESENCE_HIDDEN;
      else
        presence_id = GABBLE_PRESENCE_DND;
    }
  else if (dnd)
    {
      presence_id = GABBLE_PRESENCE_DND;
    }
  else
    {
      if (presence_id == GABBLE_PRESENCE_DND || presence_id == GABBLE_PRESENCE_HIDDEN)
        presence_id = GABBLE_PRESENCE_AVAILABLE;
    }

  if (base->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      /* Not connected, override with the local status. */
      rv = TRUE;
    }
  else if (is_presence_away (self->self_presence->status))
    {
      /* Away presence is not overridden with remote presence because it's
       * per connection. */
      rv = FALSE;
    }
  else
    {
      /* Update with the remote presence */
      rv = gabble_presence_update (self->self_presence, resource, presence_id,
          status_message, prio, NULL, time (NULL));
    }

  g_free (resource);

  return rv;
}

static gboolean
iq_shared_status_changed_cb (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  WockyNode *query_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (stanza), "query",
      NS_GOOGLE_SHARED_STATUS);

  if (store_shared_statuses (self, query_node))
    emit_presences_changed_for_self (self);

  wocky_porter_acknowledge_iq (porter, stanza, NULL);

  return TRUE;
}

static gboolean
iq_privacy_list_push_cb (
    WockyPorter *porter,
    WockyStanza *message,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  WockyNode *list_node, *query_node, *iq;
  const gchar *list_name;

  wocky_porter_acknowledge_iq (wocky_session_get_porter (conn->session),
      message, NULL);

  iq = wocky_stanza_get_top_node (message);
  query_node = wocky_node_get_first_child (iq);
  list_node = wocky_node_get_child (query_node, "list");
  list_name = wocky_node_get_attribute (list_node, "name");

  if (g_strcmp0 (list_name, conn->presence_priv->invisible_list_name) == 0)
    setup_invisible_privacy_list_async (conn, NULL, NULL);

  return TRUE;
}

/**********************************************************************
* get_existing_privacy_lists_async
* ↓
* privacy_lists_loaded_cb
* ↓
* ↓ inv_list_name = "invisible" unless set to something else by plugins
* ↓
* setup_invisible_privacy_list_async
* ↓
* verify_invisible_privacy_list_cb─────────────────────────────────────┐
* |        | |                                                         |
* |success | |n/a                                               failure|
* |        | |                                                         |
* |        | ↓                                                         |
* ├────────+─presence_create_invisible_privacy_list(inv_list_name)─────|
* |        |                                                           |
* |        |invalid                                                    |
* |        ↓                                                           |
* ├────────presence_create_invisible_privacy_list("invisible-gabble")──|
* |                                                                    |
* |                                                                    ↓
* |                                       disable_invisible_privacy_list
* |                                                         |
* ├─────────────────────────────────────────────────────────┘
* ↓
* toggle_presence_visibility_async
* ↓
* ...
**********************************************************************/

static void
get_shared_status_cb  (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  WockyStanza *iq = NULL;
  GError *error = NULL;

  DEBUG (" ");

  if (!conn_util_send_iq_finish (self, res, &iq, &error))
    {
      DEBUG ("Error getting shared status: %s", error->message);

      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }
  else
    {
      WockyNode *query_node = wocky_node_get_child_ns (wocky_stanza_get_top_node (iq),
          "query", NS_GOOGLE_SHARED_STATUS);

      if (query_node != NULL)
        {
          const gchar *max_status_message = wocky_node_get_attribute (query_node,
              "status-max");
          const gchar *max_shared = wocky_node_get_attribute (query_node,
              "status-list-contents-max");

          if (max_status_message != NULL)
            priv->max_status_message_length = (gint) g_ascii_strtoll (
                max_status_message, NULL, 10);
          else
            priv->max_status_message_length = 0; /* no limit */

          if (max_shared != NULL)
            priv->max_shared_statuses = (gint) g_ascii_strtoll (max_shared, NULL, 10);
          else
            priv->max_shared_statuses = 5; /* Safe bet */

          store_shared_statuses (self, query_node);
        }
      else
        {
          g_simple_async_result_set_error (result, CONN_PRESENCE_ERROR,
              CONN_PRESENCE_ERROR_SET_SHARED_STATUS,
              "Error retrieving shared status, received empty reply");
        }

      g_object_unref (iq);
    }

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static void
get_shared_status_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, get_shared_status_async);
  WockyStanza *iq;

  DEBUG (" ");

  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, conn_util_get_bare_self_jid (self),
        '(', "query",
          ':', NS_GOOGLE_SHARED_STATUS,
          '@', "version", GOOGLE_SHARED_STATUS_VERSION,
        ')',
      NULL);

  conn_util_send_iq_async (self, iq, NULL, get_shared_status_cb, result);

  /* We cannot use the chat status with GTalk's shared status. */
  if (self->self_presence->status == GABBLE_PRESENCE_CHAT)
    self->self_presence->status = GABBLE_PRESENCE_AVAILABLE;

  g_object_unref (iq);
}

static gboolean
get_shared_status_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self, get_shared_status_async);
}

static void
get_existing_privacy_lists_cb (GabbleConnection *conn,
    WockyStanza *sent_msg,
    WockyStanza *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  WockyNode *query_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (reply_msg), "query", NS_PRIVACY);
  GError *error = NULL;

  if (wocky_stanza_extract_errors (reply_msg, NULL, &error, NULL, NULL))
    {
      DEBUG ("Error getting privacy lists: %s", error->message);

      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }
  else if (query_node == NULL)
    {
      g_simple_async_result_set_error (result,
          TP_ERROR, TP_ERROR_NETWORK_ERROR,
          "no <query/> node in 'list privacy lists' reply");
    }
  else
    {
      GabbleConnectionPresencePrivate *priv = conn->presence_priv;
      WockyNode *list_node;
      WockyNodeIter iter;
      GabblePluginLoader *loader = gabble_plugin_loader_dup ();

      /* As we're called only once, privacy_statuses couldn't have been
       * already initialised. */
      g_assert (priv->privacy_statuses == NULL);

      priv->privacy_statuses = g_hash_table_new_full (
          g_str_hash, g_str_equal, g_free, g_free);

      wocky_node_iter_init (&iter, query_node, "list", NULL);
      while (wocky_node_iter_next (&iter, &list_node))
        {
          const gchar *list_name = wocky_node_get_attribute (list_node,
              "name");
          const gchar *status_name;

          status_name =
              gabble_plugin_loader_presence_status_for_privacy_list (loader,
                  list_name);

          if (status_name)
            {
              DEBUG ("Presence status %s backed by privacy list %s",
                  status_name, list_name);

              g_hash_table_replace (priv->privacy_statuses,
                g_strdup (status_name), g_strdup (list_name));
            }
        }
    }

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static void
get_existing_privacy_lists_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GError *error = NULL;
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, get_existing_privacy_lists_async);
  WockyStanza *iq;

  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, NULL,
        '(', "query",
          ':', NS_PRIVACY,
        ')',
      NULL);

  if (!_gabble_connection_send_with_reply (self, (WockyStanza *) iq,
          get_existing_privacy_lists_cb, NULL, result, &error))
    {
      g_simple_async_result_set_from_error (result, error);
      g_simple_async_result_complete_in_idle (result);

      g_object_unref (result);
      g_error_free (error);
    }

  g_object_unref (iq);
}

static gboolean
get_existing_privacy_lists_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self, get_existing_privacy_lists_async);
}


static void
setup_invisible_privacy_list_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GError *error = NULL;
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, setup_invisible_privacy_list_async);
  WockyStanza *iq;

  if (priv->invisible_list_name == NULL)
    priv->invisible_list_name = g_strdup ("invisible");

  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, NULL,
        '(', "query",
          ':', NS_PRIVACY,
          '(', "list",
            '@', "name", self->presence_priv->invisible_list_name,
          ')',
        ')',
      NULL);

  if (!_gabble_connection_send_with_reply (self, (WockyStanza *) iq,
          verify_invisible_privacy_list_cb, NULL, result, &error))
    {
      g_simple_async_result_set_from_error (result, error);
      g_simple_async_result_complete_in_idle (result);

      g_object_unref (result);
      g_error_free (error);
    }

  g_object_unref (iq);
}

static gboolean
setup_invisible_privacy_list_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self, setup_invisible_privacy_list_async);
}

static gboolean
is_valid_invisible_list (WockyNode *list_node)
{
  WockyNode *top_node = NULL;
  WockyNode *child;
  WockyNodeIter i;
  guint top_order = G_MAXUINT;

  wocky_node_iter_init (&i, list_node, "item", NULL);
  while (wocky_node_iter_next (&i, &child))
    {
      const gchar *order_str;
      guint order;
      gchar *end;

      order_str = wocky_node_get_attribute (child, "order");

      if (order_str == NULL)
        continue;

      order = strtoul (order_str, &end, 10);

      if (*end != '\0')
        continue;

      if (order < top_order)
        {
          top_order = order;
          top_node = child;
        }
    }

  if (top_node != NULL)
    {
      const gchar *value = wocky_node_get_attribute (top_node, "value");
      const gchar *action = wocky_node_get_attribute (top_node, "action");
      WockyNode *presence_out = wocky_node_get_child (top_node,
          "presence-out");

      return (value == NULL && g_strcmp0 (action, "deny") == 0 &&
          presence_out != NULL);
    }

  return FALSE;
}

static void
verify_invisible_privacy_list_cb (GabbleConnection *conn,
    WockyStanza *sent_msg,
    WockyStanza *reply_msg,
    GObject *obj,
    gpointer user_data)
{
  GabbleConnectionPresencePrivate *priv = conn->presence_priv;
  WockyNode *query_node, *list_node = NULL;
  GError *error = NULL;

  query_node = wocky_node_get_child_ns (wocky_stanza_get_top_node (reply_msg),
      "query", NS_PRIVACY);

  if (query_node != NULL)
    list_node = wocky_node_get_child (query_node, "list");

  if (!wocky_stanza_extract_errors (reply_msg, NULL, &error, NULL, NULL) &&
      list_node != NULL)
    {
      if (!is_valid_invisible_list (list_node))
        {
          g_free (priv->invisible_list_name);
          priv->invisible_list_name = g_strdup ("invisible-gabble");

          create_invisible_privacy_list_async (conn,
              create_invisible_privacy_list_cb, user_data);
        }
      else
        {
          /* "hidden" presence status can be backed  by this list */
          g_hash_table_insert (conn->presence_priv->privacy_statuses,
              g_strdup ("hidden"),
              g_strdup (priv->invisible_list_name));

          toggle_presence_visibility_async (conn,
              toggle_initial_presence_visibility_cb, user_data);
        }
    }
  else if (error->code == WOCKY_XMPP_ERROR_ITEM_NOT_FOUND)
    {
      create_invisible_privacy_list_async (conn,
          create_invisible_privacy_list_cb, user_data);
    }
  else
    {
      disable_invisible_privacy_list (conn);

      toggle_presence_visibility_async (conn,
          toggle_initial_presence_visibility_cb, user_data);
    }

  if (error != NULL)
    g_error_free (error);
}

static void
initial_presence_setup_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GSimpleAsyncResult *external_result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  if (!setup_invisible_privacy_list_finish (self, result, &error))
    {
      g_simple_async_result_set_from_error (external_result, error);

      g_error_free (error);
    }

  if (priv->invisibility_method == INVISIBILITY_METHOD_PRIVACY &&
      self->session != NULL)
    {
      priv->iq_list_push_id = wocky_c2s_porter_register_handler_from_server (
          WOCKY_C2S_PORTER (wocky_session_get_porter (self->session)),
          WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
          WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
          iq_privacy_list_push_cb, self,
          '(', "query", ':', NS_PRIVACY,
            '(', "list", ')',
          ')', NULL);
    }

  g_simple_async_result_complete_in_idle (external_result);

  g_object_unref (external_result);
}

static void
toggle_initial_presence_visibility_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GSimpleAsyncResult *external_result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  if (!toggle_presence_visibility_finish (self, result, &error))
    {
      self->self_presence->status = GABBLE_PRESENCE_DND;

      g_clear_error (&error);

      if (!conn_presence_signal_own_presence (self, NULL, &error))
          {
            g_simple_async_result_set_from_error (external_result, error);
            g_error_free (error);
          }
    }

  g_simple_async_result_complete_in_idle (external_result);

  g_object_unref (external_result);
}

static void
privacy_lists_loaded_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GError *error = NULL;

  if (get_existing_privacy_lists_finish (self, result, &error))
    {
      /* if the above call succeeded, the server supports privacy
       * lists, so this should be initialised. */
      g_assert (priv->privacy_statuses != NULL);

      /* If anyone/plugins already set up "hidden" status backing
       * by a specific list, try to use that instead. If that list
       * is inadequate, we'll create gabble-specific one. */
      priv->invisible_list_name =
          g_strdup (g_hash_table_lookup (priv->privacy_statuses, "hidden"));

      if (priv->invisibility_method == INVISIBILITY_METHOD_NONE)
        priv->invisibility_method = INVISIBILITY_METHOD_PRIVACY;
    }

  if (priv->invisibility_method == INVISIBILITY_METHOD_PRIVACY)
    setup_invisible_privacy_list_async (self, initial_presence_setup_cb,
        user_data);
  else
    toggle_presence_visibility_async (self,
        toggle_initial_presence_visibility_cb, user_data);
}

static void
shared_status_toggle_initial_presence_visibility_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GSimpleAsyncResult *external_result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  if (!toggle_presence_visibility_finish (self, result, &error))
    {
      g_simple_async_result_set_from_error (external_result, error);
      g_clear_error (&error);
    }
  else if (self->self_presence->status != GABBLE_PRESENCE_AWAY &&
      self->self_presence->status != GABBLE_PRESENCE_XA)
    {
      /* With shared status we send the normal <presence/> only with away and
       * extended away, but for initial status we need to send <presence/> as
       * it also contains the caps. */
      if (!conn_presence_signal_own_presence (self, NULL, &error))
        {
          g_simple_async_result_set_from_error (external_result, error);
          g_error_free (error);
        }
    }

  g_simple_async_result_complete_in_idle (external_result);

  g_object_unref (external_result);
}

static void
shared_status_setup_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GabbleConnectionPresencePrivate *priv = self->presence_priv;

  if (get_shared_status_finish (self, result, &error))
    {
      WockyPorter *porter = wocky_session_get_porter (self->session);

      priv->invisibility_method = INVISIBILITY_METHOD_SHARED_STATUS;

      priv->iq_shared_status_cb = wocky_c2s_porter_register_handler_from_server (
          WOCKY_C2S_PORTER (porter),
          WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
          WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
          iq_shared_status_changed_cb, self,
          '(', "query",
            ':', NS_GOOGLE_SHARED_STATUS,
          ')', NULL);
    }
  else
    {
      DEBUG ("failed: %s", error->message);
      g_error_free (error);
    }

  toggle_presence_visibility_async (self,
      shared_status_toggle_initial_presence_visibility_cb, user_data);
}

void
conn_presence_set_initial_presence_async (GabbleConnection *self,
    GAsyncReadyCallback callback, gpointer user_data)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, conn_presence_set_initial_presence_async);

  if (self->features & GABBLE_CONNECTION_FEATURES_INVISIBLE)
    priv->invisibility_method = INVISIBILITY_METHOD_INVISIBLE_COMMAND;

  if (self->features & GABBLE_CONNECTION_FEATURES_GOOGLE_SHARED_STATUS)
    get_shared_status_async (self, shared_status_setup_cb, result);
  else
    get_existing_privacy_lists_async (self, privacy_lists_loaded_cb, result);
}

gboolean
conn_presence_set_initial_presence_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self, conn_presence_set_initial_presence_async);
}

/**
 * conn_presence_statuses:
 *
 * Used to retrieve the list of presence statuses supported by connections
 * consisting of the basic statuses followed by any statuses defined by
 * plugins.
 *
 * Returns: an array of #TpPresenceStatusSpec terminated by a 0 filled member
 * The array is owned by te connection presence implementation and must not
 * be altered or freed by anyone else.
 **/
const TpPresenceStatusSpec *
conn_presence_statuses (void)
{
  if (gabble_statuses == NULL)
    {
      GabblePluginLoader *loader = gabble_plugin_loader_dup ();

      gabble_statuses = gabble_plugin_loader_append_statuses (
          loader, gabble_base_statuses);

      g_object_unref (loader);
    }

  return gabble_statuses;
}

static guint
get_maximum_status_message_length_cb (GObject *obj)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  GabbleConnectionPresencePrivate *priv = conn->presence_priv;

  return priv->max_status_message_length;
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
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GabblePresence *presence = self->self_presence;
  TpBaseConnection *base = (TpBaseConnection *) self;
  WockyStanza *message = gabble_presence_as_message (presence, to);
  gboolean ret;

  if (presence->status == GABBLE_PRESENCE_HIDDEN && to == NULL)
    {
      if (priv->invisibility_method == INVISIBILITY_METHOD_PRESENCE_INVISIBLE)
        wocky_node_set_attribute (wocky_stanza_get_top_node (message),
            "type", "invisible");
      /* FIXME: or if sending directed presence, should we add
       * <show>away</show>? */
    }

  gabble_connection_fill_in_caps (self, message);

  ret = _gabble_connection_send (self, message, error);

  g_object_unref (message);

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

  if (!gabble_roster_handle_gets_presence_from_us (self->roster, recipient))
    return FALSE;

  /* FIXME: other reasons they might not be able to see our presence? */

  return TRUE;
}

static void
activate_current_privacy_list_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, activate_current_privacy_list_async);

  activate_current_privacy_list (self, result);
  g_object_unref (result);
}

static void
toggle_presence_visibility_async (GabbleConnection *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, toggle_presence_visibility_async);
  gboolean set_invisible =
    (self->self_presence->status == GABBLE_PRESENCE_HIDDEN);

  switch (priv->invisibility_method)
    {
      case INVISIBILITY_METHOD_SHARED_STATUS:
        set_shared_status (self, result);
        break;

      case INVISIBILITY_METHOD_INVISIBLE_COMMAND:
        set_xep0186_invisible (self, set_invisible, result);
        break;

      case INVISIBILITY_METHOD_PRIVACY:
        activate_current_privacy_list (self, result);
        break;

      default:
      {
        GError *error = NULL;

        /* If we don't even support XEP-0018, revert to DND */
        if (priv->invisibility_method == INVISIBILITY_METHOD_NONE &&
            set_invisible)
          self->self_presence->status = GABBLE_PRESENCE_DND;

        if (!conn_presence_signal_own_presence (self, NULL, &error))
          {
            g_simple_async_result_set_from_error (result, error);
            g_error_free (error);
          }

        g_simple_async_result_complete_in_idle (result);
      }

    }

  g_object_unref (result);
}

static gboolean
toggle_presence_visibility_finish (
    GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self, toggle_presence_visibility_async);
}

static void
set_shared_status_presence_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GError *error = NULL;

  DEBUG (" ");
  if (!set_shared_status_finish (self, res, &error))
    {
      DEBUG ("Error setting shared status %s",
          error->message);

      g_error_free (error);
      error = NULL;
    }

  emit_presences_changed_for_self (self);
}

static void
toggle_presence_visibility_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (source_object);
  GError *error = NULL;

  DEBUG (" ");
  if (!toggle_presence_visibility_finish (self, res, &error))
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

  emit_presences_changed_for_self (self);
}

static gboolean
set_own_status_cb (GObject *obj,
                   const TpPresenceStatus *status,
                   GError **error)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  GabbleConnectionPresencePrivate *priv = conn->presence_priv;
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabblePresenceId i = GABBLE_PRESENCE_AVAILABLE;
  const gchar *message_str = NULL;
  gchar *message_truncated = NULL;
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
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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
              g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
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
              g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                   "Status argument 'priority' requires a signed integer");
              retval = FALSE;
              goto OUT;
            }
          prio = CLAMP (g_value_get_int (priority), G_MININT8, G_MAXINT8);
        }
    }

  if (message_str && priv->max_status_message_length > 0 &&
      priv->shared_statuses != NULL)
    {
      message_truncated = g_strndup (message_str,
          priv->max_status_message_length);
      message_str = message_truncated;
    }

  if (gabble_presence_update (conn->self_presence, resource, i,
          message_str, prio, NULL, time (NULL)))
    {
      if (base->status != TP_CONNECTION_STATUS_CONNECTED)
        {
          retval = TRUE;
        }
      else if (prev_status != i && (prev_status == GABBLE_PRESENCE_HIDDEN ||
            i  == GABBLE_PRESENCE_HIDDEN))
        {
          toggle_presence_visibility_async (conn,
              toggle_presence_visibility_cb, NULL);
        }
      /* if privacy lists are supported, make sure we update the current
       * list as needed, before signalling own presence */
      else if (priv->privacy_statuses != NULL)
        {
          activate_current_privacy_list_async (conn, NULL, NULL);
        }
      else if (priv->shared_statuses != NULL)
        {
          set_shared_status_async (conn, set_shared_status_presence_cb, NULL);
        }
      else
        {
          retval = conn_presence_signal_own_presence (conn, NULL, error);
          emit_presences_changed_for_self (conn);
        }

    }

OUT:
  g_free (message_truncated);
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
  if (status == TP_CONNECTION_STATUS_CONNECTED)
    emit_presences_changed_for_self (conn);
}


static gboolean
status_available_cb (GObject *obj, guint status)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabbleConnectionPresencePrivate *priv = conn->presence_priv;
  TpConnectionPresenceType presence_type =
    gabble_statuses[status].presence_type;

  if (base->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      /* we just don't know yet */
      return TRUE;
    }

  /* This relies on the fact the first entries in the statuses table
   * are from gabble_base_statuses. If index to the statuses table is outside
   * the gabble_base_statuses table, the status is provided by a plugin. */
  if (status >= G_N_ELEMENTS (gabble_base_statuses))
    {
      /* At the moment, plugins can only implement statuses via privacy
       * lists, so any extra status should be backed by one. If it's not
       * (or if privacy lists are not supported by the server at all)
       * by the time we're connected, it's not available. */
      if (priv->privacy_statuses != NULL &&
          g_hash_table_lookup (priv->privacy_statuses,
              gabble_statuses[status].name))
        {
          return TRUE;
        }
      else
        {
          return FALSE;
        }
    }

  if (presence_type == TP_CONNECTION_PRESENCE_TYPE_HIDDEN &&
      priv->invisibility_method == INVISIBILITY_METHOD_NONE)
    {
      /* If we've gone online and found that the server doesn't support
       * invisible, reject it. */
      return FALSE;
    }
  else if (status == GABBLE_PRESENCE_CHAT &&
      priv->shared_statuses != NULL)
    {
      /* We cannot use the chat status with GTalk's shared status. */
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

GabblePresenceId
conn_presence_get_type (GabblePresence *presence)
{
  return gabble_statuses[presence->status].presence_type;
}

/* We should update this when telepathy-glib supports setting
 * statuses at constructor time (see
 *   https://bugs.freedesktop.org/show_bug.cgi?id=12896 ).
 * Until then, gabble_statuses is leaked.
 */
void
conn_presence_class_init (GabbleConnectionClass *klass)
{
  TpPresenceMixinClass *mixin_cls;

  tp_presence_mixin_class_init ((GObjectClass *) klass,
      G_STRUCT_OFFSET (GabbleConnectionClass, presence_class),
      status_available_cb, construct_contact_statuses_cb,
      set_own_status_cb, conn_presence_statuses ());
  mixin_cls = TP_PRESENCE_MIXIN_CLASS (klass);
  mixin_cls->get_maximum_status_message_length =
      get_maximum_status_message_length_cb;

  tp_presence_mixin_simple_presence_init_dbus_properties (
    (GObjectClass *) klass);
}

void
conn_presence_init (GabbleConnection *conn)
{
  conn->presence_priv = g_slice_new0 (GabbleConnectionPresencePrivate);
  conn->presence_priv->previous_shared_status = GABBLE_PRESENCE_UNKNOWN;

  g_signal_connect (conn->presence_cache, "presences-updated",
      G_CALLBACK (connection_presences_updated_cb), conn);

  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), conn);

  conn->presence_priv->invisible_list_name = g_strdup ("invisible");

  conn->presence_priv->privacy_statuses = NULL;

  tp_presence_mixin_init ((GObject *) conn,
      G_STRUCT_OFFSET (GabbleConnection, presence));

  tp_presence_mixin_simple_presence_register_with_contacts_mixin (
      G_OBJECT (conn));
}

void
conn_presence_dispose (GabbleConnection *self)
{
  GabbleConnectionPresencePrivate *priv = self->presence_priv;
  WockyPorter *porter;

  if (self->session == NULL)
    return;

  porter = wocky_session_get_porter (self->session);

  if (priv->iq_shared_status_cb != 0)
    {
      wocky_porter_unregister_handler (porter, priv->iq_shared_status_cb);
      priv->iq_shared_status_cb = 0;
    }

  if (priv->iq_list_push_id != 0)
    {
      wocky_porter_unregister_handler (porter, priv->iq_list_push_id);
      priv->iq_list_push_id = 0;
    }
}

void
conn_presence_finalize (GabbleConnection *conn)
{
  GabbleConnectionPresencePrivate *priv = conn->presence_priv;

  g_free (priv->invisible_list_name);

  if (priv->privacy_statuses != NULL)
      g_hash_table_unref (priv->privacy_statuses);

  if (priv->shared_statuses != NULL)
      g_hash_table_unref (priv->shared_statuses);

  g_slice_free (GabbleConnectionPresencePrivate, priv);

  tp_presence_mixin_finalize ((GObject *) conn);
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
