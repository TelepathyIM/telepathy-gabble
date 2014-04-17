/*
 * conn-avatars.c - Gabble connection avatar interface
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

#include "conn-avatars.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "presence.h"
#include "presence-cache.h"
#include "conn-presence.h"
#include "namespaces.h"
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
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GError *error = NULL;

  /* sha1 can be "" if we know there is no avatar, but must not be NULL here */
  g_assert (sha1 != NULL);

  if (!tp_strdiff (sha1, conn->self_presence->avatar_sha1))
    return TRUE;

  tp_svc_connection_interface_avatars1_emit_avatar_updated (conn,
      tp_base_connection_get_self_handle (base), sha1);

  g_free (conn->self_presence->avatar_sha1);
  conn->self_presence->avatar_sha1 = g_strdup (sha1);

  if (!conn_presence_signal_own_presence (conn, NULL, &error))
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
                             const gchar *sha1,
                             gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = (TpBaseConnection *) conn;

  /* sha1 can be "" if we know there is no avatar, but must not be NULL here */
  g_assert (sha1 != NULL);

  if (handle == tp_base_connection_get_self_handle (base))
    update_own_avatar_sha1 (conn, sha1, NULL);
  else
    tp_svc_connection_interface_avatars1_emit_avatar_updated (conn,
        handle, sha1);
}

/* Called when our vCard is first fetched, so we can start putting the
 * SHA-1 of an existing avatar in our presence. */
static void
connection_got_self_initial_avatar_cb (GObject *obj,
                                       gchar *sha1,
                                       gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  /* sha1 can be "" if we know there is no avatar, but must not be NULL here */
  g_assert (sha1 != NULL);

  update_own_avatar_sha1 (conn, sha1, NULL);
}

/* Jabber prescribes no MIME type for avatars, but XEP-0153 says support
 * for image/png is REQUIRED, with image/jpeg and image/gif RECOMMENDED */
static const char *mimetypes[] = {
    "image/png", "image/jpeg", "image/gif", NULL };

/* Jabber has no min/max width/height or max size, but XEP-0153 says
 * you SHOULD use 32-96px either way, and no more than 8K of data */
#define AVATAR_MIN_PX 32
#define AVATAR_REC_PX 64
#define AVATAR_MAX_PX 96
#define AVATAR_MAX_BYTES 8192

typedef struct {
  GabbleConnection *conn;
  GDBusMethodInvocation *invocation;
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

  g_assert (tp_base_connection_get_self_handle (base) != 0);

  g_hash_table_insert (context->ret,
      GUINT_TO_POINTER (tp_base_connection_get_self_handle (base)),
      g_strdup (sha1));

  tp_svc_connection_interface_avatars1_return_from_get_known_avatar_tokens (
      context->invocation, context->ret);
  g_hash_table_unref (context->ret);

  g_slice_free (GetKnownAvatarTokensContext, context);
}


/**
 * gabble_connection_get_known_avatar_tokens
 *
 * Implements D-Bus method GetKnownAvatarTokens
 * on interface Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_known_avatar_tokens (TpSvcConnectionInterfaceAvatars1 *iface,
                                           const GArray *contacts,
                                           GDBusMethodInvocation *invocation)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  gboolean wait_for_self_avatar = FALSE;
  gboolean have_self_avatar;
  guint i;
  GHashTable *ret;
  GError *err = NULL;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_ENTITY_TYPE_CONTACT);

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, invocation);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &err))
    {
      g_dbus_method_invocation_return_gerror (invocation, err);
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

      if (tp_base_connection_get_self_handle (base) == handle)
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

  tp_svc_connection_interface_avatars1_return_from_get_known_avatar_tokens (
      invocation, ret);

  g_hash_table_unref (ret);
}



static gboolean
parse_avatar (WockyNode *vcard,
              const gchar **mime_type,
              GString **avatar,
              GError **error)
{
  WockyNode *photo_node;
  WockyNode *type_node;
  WockyNode *binval_node;
  const gchar *binval_value;
  guchar *st;
  gsize outlen;

  photo_node = wocky_node_get_child (vcard, "PHOTO");

  if (NULL == photo_node)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
        "contact vCard has no photo");
      return FALSE;
    }

  type_node = wocky_node_get_child (photo_node, "TYPE");

  if (NULL != type_node)
    {
      *mime_type = type_node->content;
    }
  else
    {
      *mime_type = "";
    }

  binval_node = wocky_node_get_child (photo_node, "BINVAL");

  if (NULL == binval_node)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
        "contact avatar is missing binval node");
      return FALSE;
    }

  binval_value = binval_node->content;

  if (NULL == binval_value)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
        "contact avatar is missing binval content");
      return FALSE;
    }

  st = g_base64_decode (binval_value, &outlen);
  *avatar = g_string_new_len ((gchar *) st, outlen);
  g_free (st);

  if (NULL == *avatar)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
        "failed to decode avatar from base64");
      return FALSE;
    }

  return TRUE;
}

static void
emit_avatar_retrieved (TpSvcConnectionInterfaceAvatars1 *iface,
                       TpHandle contact,
                       WockyNode *vcard_node)
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
  tp_svc_connection_interface_avatars1_emit_avatar_retrieved (iface, contact,
      sha1, arr, mime_type);
  g_array_unref (arr);
  g_free (sha1);
  g_string_free (avatar_str, TRUE);
}

/* All references are borrowed */
typedef struct {
    TpHandle handle;
    GabbleConnection *conn;
    TpSvcConnectionInterfaceAvatars1 *iface;
} RequestAvatarsContext;

static void
request_avatars_cb (GabbleVCardManager *manager,
                    GabbleVCardManagerRequest *request,
                    TpHandle handle,
                    WockyNode *vcard,
                    GError *vcard_error,
                    gpointer user_data)
{
  RequestAvatarsContext *ctx = user_data;

  g_assert (g_hash_table_lookup (ctx->conn->avatar_requests,
      GUINT_TO_POINTER (ctx->handle)));

  g_hash_table_remove (ctx->conn->avatar_requests,
      GUINT_TO_POINTER (ctx->handle));

  if (vcard_error == NULL)
    emit_avatar_retrieved (ctx->iface, handle, vcard);

  g_slice_free (RequestAvatarsContext, ctx);
}

static void
gabble_connection_request_avatars (TpSvcConnectionInterfaceAvatars1 *iface,
                                   const GArray *contacts,
                                   GDBusMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contacts_repo =
      tp_base_connection_get_handles (base, TP_ENTITY_TYPE_CONTACT);
  GError *error = NULL;
  guint i;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contacts_repo, contacts, FALSE, &error))
    {
      g_dbus_method_invocation_return_gerror (context, error);
      g_error_free (error);
      return;
    }

  for (i = 0; i < contacts->len; i++)
    {
      WockyNode *vcard_node;
      TpHandle contact = g_array_index (contacts, TpHandle, i);

      if (gabble_vcard_manager_get_cached (self->vcard_manager,
            contact, &vcard_node))
        {
          emit_avatar_retrieved (iface, contact, vcard_node);
        }
      else
        {
          if (NULL == g_hash_table_lookup (self->avatar_requests,
                GUINT_TO_POINTER (contact)))
            {
              RequestAvatarsContext *ctx = g_slice_new (RequestAvatarsContext);

              ctx->conn = self;
              ctx->iface = iface;
              ctx->handle = contact;

              g_hash_table_insert (self->avatar_requests,
                  GUINT_TO_POINTER (contact), ctx);

              gabble_vcard_manager_request (self->vcard_manager,
                contact, 0, request_avatars_cb, ctx, NULL);
            }
        }
    }

  tp_svc_connection_interface_avatars1_return_from_request_avatars (context);
}


struct _set_avatar_ctx {
  GabbleConnection *conn;
  GDBusMethodInvocation *invocation;
  GString *avatar;
  gboolean is_clear;
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
                 WockyNode *vcard,
                 GError *vcard_error,
                 gpointer user_data)
{
  struct _set_avatar_ctx *ctx = (struct _set_avatar_ctx *) user_data;
  TpBaseConnection *base = (TpBaseConnection *) ctx->conn;

  if (NULL == vcard)
    {
      GError tp_error = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          vcard_error->message };

      /* Google Talk has been observed to return bad-request when the avatar is
       * too big. It's not clear what other XMPP errors make sense here, or how
       * to map them.
       */
      if (vcard_error->domain == WOCKY_XMPP_ERROR)
        if (vcard_error->code == WOCKY_XMPP_ERROR_BAD_REQUEST ||
            vcard_error->code == WOCKY_XMPP_ERROR_NOT_ACCEPTABLE)
          tp_error.code = TP_ERROR_INVALID_ARGUMENT;

      g_dbus_method_invocation_return_gerror (ctx->invocation, &tp_error);
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

      if (conn_presence_signal_own_presence (ctx->conn, NULL, &error))
        {
          if (ctx->is_clear)
            tp_svc_connection_interface_avatars1_return_from_clear_avatar (
                ctx->invocation);
          else
            tp_svc_connection_interface_avatars1_return_from_set_avatar (
                ctx->invocation, presence->avatar_sha1);

          tp_svc_connection_interface_avatars1_emit_avatar_updated (
              ctx->conn, tp_base_connection_get_self_handle (base),
              presence->avatar_sha1);
        }
      else
        {
          g_dbus_method_invocation_return_gerror (ctx->invocation, error);
          g_error_free (error);
        }
    }

  _set_avatar_ctx_free (ctx);
}

static void
set_avatar_internal (TpSvcConnectionInterfaceAvatars1 *iface,
    const GArray *avatar,
    const gchar *mime_type,
    GDBusMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  GabbleVCardManagerEditInfo *edit_info;
  GList *edits = NULL;
  struct _set_avatar_ctx *ctx;
  gchar *base64;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  ctx = g_new0 (struct _set_avatar_ctx, 1);
  ctx->conn = self;
  ctx->invocation = context;
  ctx->is_clear = (avatar == NULL);

  if (avatar != NULL && avatar->len > 0)
    {
      gint state = 0, save = 0, outlen;
      /* See the documentation for g_base64_encode_step(). */
      guint base64_data_size = (avatar->len / 3 + 1) * 4 + 4;
      guint base64_line_wrapped_data_size =
          base64_data_size + (base64_data_size / 72) + 1;

      ctx->avatar = g_string_new_len (avatar->data, avatar->len);
      base64 = g_malloc (base64_line_wrapped_data_size);
      outlen = g_base64_encode_step ((const guchar *) avatar->data,
          avatar->len, TRUE, base64, &state, &save);
      outlen += g_base64_encode_close (TRUE, base64 + outlen, &state, &save);
      base64[outlen] = '\0';

      DEBUG ("Replacing avatar");

      edit_info = gabble_vcard_manager_edit_info_new ("PHOTO",
          NULL, GABBLE_VCARD_EDIT_REPLACE,
          '(', "TYPE", '$', mime_type, ')',
          '(', "BINVAL", '$', base64, ')',
          NULL);

      g_free (base64);
    }
  else
    {
      DEBUG ("Removing avatar");
      edit_info = gabble_vcard_manager_edit_info_new ("PHOTO",
          NULL, GABBLE_VCARD_EDIT_DELETE, NULL);
    }

  edits = g_list_append (edits, edit_info);

  gabble_vcard_manager_edit (self->vcard_manager, 0,
      _set_avatar_cb2, ctx, (GObject *) self,
      edits);
}

static void
gabble_connection_set_avatar (TpSvcConnectionInterfaceAvatars1 *iface,
                              const GArray *avatar,
                              const gchar *mime_type,
                              GDBusMethodInvocation *context)
{
  g_assert (avatar != NULL);
  set_avatar_internal (iface, avatar, mime_type, context);
}

/**
 * gabble_connection_clear_avatar
 *
 * Implements D-Bus method ClearAvatar
 * on interface Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_clear_avatar (TpSvcConnectionInterfaceAvatars1 *iface,
                                GDBusMethodInvocation *context)
{
  set_avatar_internal (iface, NULL, NULL, context);
}

gboolean
conn_avatars_fill_contact_attributes (GabbleConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    GVariantDict *attributes)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (self);

  if (!tp_strdiff (dbus_interface, TP_IFACE_CONNECTION_INTERFACE_AVATARS1))
    {
      GabblePresence *presence = NULL;

      if (tp_base_connection_get_self_handle (base) == handle)
        presence = self->self_presence;
      else
        presence = gabble_presence_cache_get (self->presence_cache, handle);

      if (NULL != presence)
        {
          g_variant_dict_insert_value (attributes,
            TP_TOKEN_CONNECTION_INTERFACE_AVATARS1_TOKEN,
            g_variant_new_string (
              (presence->avatar_sha1 == NULL ? "" : presence->avatar_sha1)));
        }

      return TRUE;
    }

  return FALSE;
}


void
conn_avatars_init (GabbleConnection *conn)
{
  g_assert (conn->vcard_manager != NULL);

  g_signal_connect (conn->vcard_manager, "got-self-initial-avatar", G_CALLBACK
      (connection_got_self_initial_avatar_cb), conn);
  g_signal_connect (conn->presence_cache, "avatar-update", G_CALLBACK
      (connection_avatar_update_cb), conn);
}


void
conn_avatars_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAvatars1Class *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_avatars1_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_known_avatar_tokens);
  IMPLEMENT(request_avatars);
  IMPLEMENT(set_avatar);
  IMPLEMENT(clear_avatar);
#undef IMPLEMENT
}

static TpDBusPropertiesMixinPropImpl props[] = {
      { "MinimumAvatarWidth", GUINT_TO_POINTER (AVATAR_MIN_PX), NULL },
      { "RecommendedAvatarWidth", GUINT_TO_POINTER (AVATAR_REC_PX), NULL },
      { "MaximumAvatarWidth", GUINT_TO_POINTER (AVATAR_MAX_PX), NULL },
      { "MinimumAvatarHeight", GUINT_TO_POINTER (AVATAR_MIN_PX), NULL },
      { "RecommendedAvatarHeight", GUINT_TO_POINTER (AVATAR_REC_PX), NULL },
      { "MaximumAvatarHeight", GUINT_TO_POINTER (AVATAR_MAX_PX), NULL },
      { "MaximumAvatarBytes", GUINT_TO_POINTER (AVATAR_MAX_BYTES), NULL },
      /* special-cased - it's the only one with a non-guint value */
      { "SupportedAvatarMIMETypes", NULL, NULL },
      { NULL }
};
TpDBusPropertiesMixinPropImpl *conn_avatars_properties = props;

void
conn_avatars_properties_getter (GObject *object,
                                GQuark interface,
                                GQuark name,
                                GValue *value,
                                gpointer getter_data)
{
  GQuark q_mime_types = g_quark_from_static_string (
      "SupportedAvatarMIMETypes");

  if (name == q_mime_types)
    {
      g_value_set_static_boxed (value, mimetypes);
    }
  else
    {
      g_value_set_uint (value, GPOINTER_TO_UINT (getter_data));
    }
}

void
gabble_connection_dup_avatar_requirements (GStrv *supported_mime_types,
    guint *min_height,
    guint *min_width,
    guint *rec_height,
    guint *rec_width,
    guint *max_height,
    guint *max_width,
    guint *max_bytes)
{
  if (supported_mime_types != NULL)
    {
      *supported_mime_types = g_strdupv ((gchar **) mimetypes);
    }

  if (min_height != NULL)
    *min_height = AVATAR_MIN_PX;
  if (min_width != NULL)
    *min_width = AVATAR_MIN_PX;

  if (rec_height != NULL)
    *rec_height = AVATAR_REC_PX;
  if (rec_width != NULL)
    *rec_width = AVATAR_REC_PX;

  if (max_height != NULL)
    *max_height = AVATAR_MAX_PX;
  if (max_width != NULL)
    *max_width = AVATAR_MAX_PX;

  if (max_bytes != NULL)
    *max_bytes = AVATAR_MAX_BYTES;
}
