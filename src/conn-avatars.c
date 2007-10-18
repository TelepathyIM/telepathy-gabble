/*
 * conn-avatars.c - Gabble connection avatar interface
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

#include "conn-avatars.h"

#include <string.h>

#include <loudmouth/loudmouth.h>

#include <telepathy-glib/svc-connection.h>

#include "base64.h"
#include "presence.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "sha1/sha1.h"
#include "vcard-manager.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "debug.h"

/* If the SHA1 has changed, this function will copy it to self_presence,
 * emit a signal and push it to the server. */
static gboolean
update_own_avatar_sha1 (GabbleConnection *conn,
                        const gchar *sha1,
                        GError **out_error)
{
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GError *error = NULL;

  if (!tp_strdiff (sha1, conn->self_presence->avatar_sha1))
    return TRUE;

  tp_svc_connection_interface_avatars_emit_avatar_updated (conn,
      base->self_handle, sha1);

  g_free (conn->self_presence->avatar_sha1);
  conn->self_presence->avatar_sha1 = g_strdup (sha1);

  if (!_gabble_connection_signal_own_presence (conn, &error))
    {
      DEBUG ("failed to signal changed avatar sha1 to the server: %s",
          error->message);

      g_propagate_error (out_error, error);

      return FALSE;
    }

  return TRUE;
}


static void
connection_avatar_update_cb (GabblePresenceCache *cache,
                             TpHandle handle,
                             gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GabblePresence *presence;

  presence = gabble_presence_cache_get (conn->presence_cache, handle);

  g_assert (presence != NULL);

  gabble_vcard_manager_invalidate_cache (conn->vcard_manager, handle);

  if (handle == base->self_handle)
    update_own_avatar_sha1 (conn, presence->avatar_sha1, NULL);
  else
    tp_svc_connection_interface_avatars_emit_avatar_updated (conn,
        handle, presence->avatar_sha1);
}

/* Called when our vCard is first fetched, so we can start putting the
 * SHA-1 of an existing avatar in our presence. */
static void
connection_got_self_initial_avatar_cb (GObject *obj,
                                       gchar *sha1,
                                       gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  update_own_avatar_sha1 (conn, sha1, NULL);
}


/**
 * gabble_connection_get_avatar_requirements
 *
 * Implements D-Bus method GetAvatarRequirements
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_avatar_requirements (TpSvcConnectionInterfaceAvatars *iface,
                                           DBusGMethodInvocation *context)
{
  /* Jabber prescribes no MIME type for avatars, but XEP-0153 says support
   * for image/png is REQUIRED, with image/jpeg and image/gif RECOMMENDED */
  static const char *mimetypes[] = {
      "image/png", "image/jpeg", "image/gif", NULL };

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (TP_BASE_CONNECTION (iface),
      context);

  /* Jabber has no min/max width/height or max size, but XEP-0153 says
   * you SHOULD use 32-96px either way, and no more than 8K of data */

  tp_svc_connection_interface_avatars_return_from_get_avatar_requirements (
      context, mimetypes, 32, 32, 96, 96, 8192);
}

/* begin deprecated code */

typedef struct {
  DBusGMethodInvocation *invocation;
  gchar **ret;
  guint my_index;
  gulong signal_conn;
} GetAvatarTokensContext;

static void
_got_self_avatar_for_get_avatar_tokens (GObject *obj,
                                        gchar *sha1,
                                        gpointer user_data)
{
  GetAvatarTokensContext *context = (GetAvatarTokensContext *) user_data;

  g_signal_handler_disconnect (obj, context->signal_conn);
  g_free (context->ret[context->my_index]);
  context->ret[context->my_index] = g_strdup (sha1);

  /* FIXME: I'm not entirely sure why gcc warns without this cast from
   * (gchar **) to (const gchar **) */
  tp_svc_connection_interface_avatars_return_from_get_avatar_tokens (
      context->invocation, (const gchar **)context->ret);
  g_strfreev (context->ret);

  g_slice_free (GetAvatarTokensContext, context);
}


/**
 * gabble_connection_get_avatar_tokens
 *
 * Implements D-Bus method GetAvatarTokens
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_avatar_tokens (TpSvcConnectionInterfaceAvatars *iface,
                                     const GArray *contacts,
                                     DBusGMethodInvocation *invocation)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  gboolean wait_for_self_avatar = FALSE;
  gboolean have_self_avatar;
  guint i, my_index = 0;
  gchar **ret;
  GError *err = NULL;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, invocation);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &err))
    {
      dbus_g_method_return_error (invocation, err);
      g_error_free (err);
      return;
    }

  g_object_get (self->vcard_manager,
      "have-self-avatar", &have_self_avatar,
      NULL);

  ret = g_new0 (gchar *, contacts->len + 1);

  /* TODO: always call the callback so we can defer presence lookups until
   * we return the method, then we don't need to strdup the strings we're
   * returning. */

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle;
      GabblePresence *presence = NULL;

      handle = g_array_index (contacts, TpHandle, i);

      if (base->self_handle == handle)
        {
          if (have_self_avatar)
            {
              presence = self->self_presence;
            }
          else
            {
              wait_for_self_avatar = TRUE;
              my_index = i;
            }
        }
      else
        {
          presence = gabble_presence_cache_get (self->presence_cache, handle);
        }

      if (NULL != presence && NULL != presence->avatar_sha1)
          ret[i] = g_strdup (presence->avatar_sha1);
      else
          ret[i] = g_strdup ("");
    }

  if (wait_for_self_avatar)
    {
      GetAvatarTokensContext *context = g_slice_new (GetAvatarTokensContext);

      context->invocation = invocation;
      context->my_index = my_index;
      context->ret = ret;
      context->signal_conn = g_signal_connect (self->vcard_manager,
          "got-self-initial-avatar",
          G_CALLBACK (_got_self_avatar_for_get_avatar_tokens),
          context);

      return;
    }

  /* FIXME: I'm not entirely sure why gcc warns without this cast from
   * (gchar **) to (const gchar **) */
  tp_svc_connection_interface_avatars_return_from_get_avatar_tokens (
      invocation, (const gchar **)ret);
  g_strfreev (ret);
}

/* end deprecated code */

typedef struct {
  GabbleConnection *conn;
  DBusGMethodInvocation *invocation;
  GHashTable *ret;
  gulong signal_conn;
} GetKnownAvatarTokensContext;

static void
_got_self_avatar_for_get_known_avatar_tokens (GObject *obj,
                                              gchar *sha1,
                                              gpointer user_data)
{
  GetKnownAvatarTokensContext *context =
      (GetKnownAvatarTokensContext *) user_data;
  TpBaseConnection *base = (TpBaseConnection *) context->conn;

  g_signal_handler_disconnect (obj, context->signal_conn);

  g_hash_table_insert (context->ret, GUINT_TO_POINTER (base->self_handle),
      g_strdup (sha1));

  tp_svc_connection_interface_avatars_return_from_get_known_avatar_tokens (
      context->invocation, context->ret);
  g_hash_table_destroy (context->ret);

  g_slice_free (GetKnownAvatarTokensContext, context);
}


/**
 * gabble_connection_get_avatar_tokens
 *
 * Implements D-Bus method GetAvatarTokens
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_known_avatar_tokens (TpSvcConnectionInterfaceAvatars *iface,
                                           const GArray *contacts,
                                           DBusGMethodInvocation *invocation)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  gboolean wait_for_self_avatar = FALSE;
  gboolean have_self_avatar;
  guint i;
  GHashTable *ret;
  GError *err = NULL;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, invocation);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &err))
    {
      dbus_g_method_return_error (invocation, err);
      g_error_free (err);
      return;
    }

  g_object_get (self->vcard_manager,
      "have-self-avatar", &have_self_avatar,
      NULL);

  ret = g_hash_table_new_full (NULL, NULL, NULL, g_free);

  /* TODO: always call the callback so we can defer presence lookups until
   * we return the method, then we don't need to strdup the strings we're
   * returning. */

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle;
      GabblePresence *presence = NULL;

      handle = g_array_index (contacts, TpHandle, i);

      if (base->self_handle == handle)
        {
          if (have_self_avatar)
            {
              presence = self->self_presence;
            }
          else
            {
              wait_for_self_avatar = TRUE;
            }
        }
      else
        {
          presence = gabble_presence_cache_get (self->presence_cache, handle);
        }

      if (NULL != presence)
        {
          if (NULL != presence->avatar_sha1)
              g_hash_table_insert (ret, GUINT_TO_POINTER (handle),
                  g_strdup (presence->avatar_sha1));
          else
              g_hash_table_insert (ret, GUINT_TO_POINTER (handle), g_strdup (""));
        }
    }

  if (wait_for_self_avatar)
    {
      GetKnownAvatarTokensContext *context = g_slice_new (GetKnownAvatarTokensContext);

      context->conn = self;
      context->invocation = invocation;
      context->ret = ret;
      context->signal_conn = g_signal_connect (self->vcard_manager,
          "got-self-initial-avatar",
          G_CALLBACK (_got_self_avatar_for_get_known_avatar_tokens),
          context);

      return;
    }

  tp_svc_connection_interface_avatars_return_from_get_known_avatar_tokens (
      invocation, ret);

  g_hash_table_destroy (ret);
}



static gboolean
parse_avatar (LmMessageNode *vcard,
              const gchar **mime_type,
              GString **avatar,
              GError **error)
{
  LmMessageNode *photo_node;
  LmMessageNode *type_node;
  LmMessageNode *binval_node;
  const gchar *binval_value;

  photo_node = lm_message_node_get_child (vcard, "PHOTO");

  if (NULL == photo_node)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "contact vCard has no photo");
      return FALSE;
    }

  type_node = lm_message_node_get_child (photo_node, "TYPE");

  if (NULL != type_node)
    {
      *mime_type = lm_message_node_get_value (type_node);
    }
  else
    {
      *mime_type = "";
    }

  binval_node = lm_message_node_get_child (photo_node, "BINVAL");

  if (NULL == binval_node)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "contact avatar is missing binval node");
      return FALSE;
    }

  binval_value = lm_message_node_get_value (binval_node);

  if (NULL == binval_value)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "contact avatar is missing binval content");
      return FALSE;
    }

  *avatar = base64_decode (binval_value);

  if (NULL == *avatar)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "failed to decode avatar from base64");
      return FALSE;
    }

  return TRUE;
}

static void
_request_avatar_cb (GabbleVCardManager *self,
                    GabbleVCardManagerRequest *request,
                    TpHandle handle,
                    LmMessageNode *vcard,
                    GError *vcard_error,
                    gpointer user_data)
{
  DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;
  GabbleConnection *conn;
  TpBaseConnection *base;
  const gchar *mime_type = NULL;
  GArray *arr;
  GError *error = NULL;
  GString *avatar = NULL;
  GabblePresence *presence;

  g_object_get (self, "connection", &conn, NULL);
  base = TP_BASE_CONNECTION (conn);

  if (NULL == vcard)
    {
      dbus_g_method_return_error (context, vcard_error);
      goto out;
    }

  if (!parse_avatar (vcard, &mime_type, &avatar, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      goto out;
    }

  if (handle == base->self_handle)
    presence = conn->self_presence;
  else
    presence = gabble_presence_cache_get (conn->presence_cache, handle);

  if (presence != NULL)
    {
      gchar *sha1;

      sha1 = sha1_hex (avatar->str, avatar->len);

      if (tp_strdiff (presence->avatar_sha1, sha1))
        {
          /* the thinking here is that we have to return an error, because we
           * can't give the user the vcard they're expecting, which has the
           * hash from the time that they requested it. */
          DEBUG ("treason uncloaked! avatar hash in presence does not match "
              "avatar in vCard for handle %u", handle);

          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "avatar hash in presence does not match avatar in vCard");
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          error = NULL;

          if (handle == base->self_handle)
            {
              update_own_avatar_sha1 (conn, sha1, NULL);
              g_free (sha1);
            }
          else
            {
              g_free (presence->avatar_sha1);
              presence->avatar_sha1 = sha1; /* take ownership */

              tp_svc_connection_interface_avatars_emit_avatar_updated (
                  conn, handle, sha1);
            }

          goto out;
        }

      g_free (sha1);
    }

  arr = g_array_new (FALSE, FALSE, sizeof (gchar));
  g_array_append_vals (arr, avatar->str, avatar->len);
  tp_svc_connection_interface_avatars_return_from_request_avatar (
      context, arr, mime_type);
  g_array_free (arr, TRUE);

out:
  if (avatar != NULL)
    g_string_free (avatar, TRUE);

  g_object_unref (conn);
}


/**
 * gabble_connection_request_avatar
 *
 * Implements D-Bus method RequestAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_request_avatar (TpSvcConnectionInterfaceAvatars *iface,
                                  guint contact,
                                  DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GError *err = NULL;
  LmMessageNode *vcard_node;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handle_is_valid (contact_handles, contact, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  if (gabble_vcard_manager_get_cached (self->vcard_manager,
      contact, &vcard_node))
    {
      _request_avatar_cb (self->vcard_manager, NULL, contact, vcard_node, NULL,
          context);
    }
  else
    {
      gabble_vcard_manager_request (self->vcard_manager, contact, 0,
          _request_avatar_cb, context, NULL);
    }
}

static void
emit_avatar_retrieved (TpSvcConnectionInterfaceAvatars *iface,
                       TpHandle contact,
                       LmMessageNode *vcard_node)
{
  const gchar *mime_type;
  GString *avatar_str;
  gchar *sha1;
  GArray *arr;

  if (!parse_avatar (vcard_node, &mime_type, &avatar_str, NULL))
    return;

  sha1 = sha1_hex (avatar_str->str, avatar_str->len);
  arr = g_array_new (FALSE, FALSE, sizeof (gchar));
  g_array_append_vals (arr, avatar_str->str, avatar_str->len);
  tp_svc_connection_interface_avatars_emit_avatar_retrieved (iface, contact,
      sha1, arr, mime_type);
  g_array_free (arr, TRUE);
  g_free (sha1);
  g_string_free (avatar_str, TRUE);
}

static void
request_avatars_cb (GabbleVCardManager *manager,
                    GabbleVCardManagerRequest *request,
                    TpHandle handle,
                    LmMessageNode *vcard,
                    GError *vcard_error,
                    gpointer user_data)
{
  if (vcard_error != NULL)
    return;

  emit_avatar_retrieved (user_data, handle, vcard);
}

static void
gabble_connection_request_avatars (TpSvcConnectionInterfaceAvatars *iface,
                                   const GArray *contacts,
                                   DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contacts_repo =
      tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;
  guint i;

  if (!tp_handles_are_valid (contacts_repo, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  for (i = 0; i < contacts->len; i++)
    {
      LmMessageNode *vcard_node;
      TpHandle contact = g_array_index (contacts, TpHandle, i);

      if (gabble_vcard_manager_get_cached (self->vcard_manager,
            contact, &vcard_node))
        {
          emit_avatar_retrieved (iface, contact, vcard_node);
        }
      else
        {
          gabble_vcard_manager_request (self->vcard_manager,
              contact, 0, request_avatars_cb, iface, NULL);
        }
    }

  tp_svc_connection_interface_avatars_return_from_request_avatars (context);
}


struct _set_avatar_ctx {
  GabbleConnection *conn;
  DBusGMethodInvocation *invocation;
  GString *avatar;
};


static void
_set_avatar_ctx_free (struct _set_avatar_ctx *ctx)
{
  if (ctx->avatar)
      g_string_free (ctx->avatar, TRUE);
  g_free (ctx);
}


static void
_set_avatar_cb2 (GabbleVCardManager *manager,
                 GabbleVCardManagerEditRequest *request,
                 LmMessageNode *vcard,
                 GError *vcard_error,
                 gpointer user_data)
{
  struct _set_avatar_ctx *ctx = (struct _set_avatar_ctx *) user_data;
  TpBaseConnection *base = (TpBaseConnection *)ctx->conn;

  if (NULL == vcard)
    {
      dbus_g_method_return_error (ctx->invocation, vcard_error);
    }
  else
    {
      GabblePresence *presence = ctx->conn->self_presence;
      GError *error = NULL;

      g_free (presence->avatar_sha1);
      if (ctx->avatar)
        {
          presence->avatar_sha1 = sha1_hex (ctx->avatar->str,
                                            ctx->avatar->len);
        }
      else
        {
          presence->avatar_sha1 = NULL;
        }

      if (_gabble_connection_signal_own_presence (ctx->conn, &error))
        {
          tp_svc_connection_interface_avatars_return_from_set_avatar (
              ctx->invocation, presence->avatar_sha1);
          tp_svc_connection_interface_avatars_emit_avatar_updated (
              ctx->conn, base->self_handle, presence->avatar_sha1);
        }
      else
        {
          dbus_g_method_return_error (ctx->invocation, error);
          g_error_free (error);
        }
    }

  _set_avatar_ctx_free (ctx);
}


/**
 * gabble_connection_set_avatar
 *
 * Implements D-Bus method SetAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_set_avatar (TpSvcConnectionInterfaceAvatars *iface,
                              const GArray *avatar,
                              const gchar *mime_type,
                              DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  struct _set_avatar_ctx *ctx;
  gchar *value = NULL;
  gchar *base64;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  ctx = g_new0 (struct _set_avatar_ctx, 1);
  ctx->conn = self;
  ctx->invocation = context;
  if (avatar)
    {
      ctx->avatar = g_string_new_len (avatar->data, avatar->len);
      base64 = base64_encode (avatar->len, avatar->data, TRUE);
      value = g_strdup_printf ("%s %s", mime_type, base64);
      g_free (base64);
    }

  DEBUG ("called");

  gabble_vcard_manager_edit (self->vcard_manager, 0,
      _set_avatar_cb2, ctx, (GObject *) self,
      1, "PHOTO", value);
  g_free (value);
}


/**
 * gabble_connection_clear_avatar
 *
 * Implements D-Bus method ClearAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_clear_avatar (TpSvcConnectionInterfaceAvatars *iface,
                                DBusGMethodInvocation *context)
{
  gabble_connection_set_avatar (iface, NULL, NULL, context);
}


void
conn_avatars_init (GabbleConnection *conn)
{
  g_signal_connect (conn->vcard_manager, "got-self-initial-avatar", G_CALLBACK
      (connection_got_self_initial_avatar_cb), conn);
  g_signal_connect (conn->presence_cache, "avatar-update", G_CALLBACK
      (connection_avatar_update_cb), conn);
}


void
conn_avatars_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAvatarsClass *klass =
    (TpSvcConnectionInterfaceAvatarsClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_avatars_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_avatar_requirements);
  IMPLEMENT(get_avatar_tokens);
  IMPLEMENT(get_known_avatar_tokens);
  IMPLEMENT(request_avatar);
  IMPLEMENT(request_avatars);
  IMPLEMENT(set_avatar);
  IMPLEMENT(clear_avatar);
#undef IMPLEMENT
}

