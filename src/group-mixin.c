/*
 * group-mixin.c - Source for GabbleGroupMixin
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#include <dbus/dbus-glib.h>
#include <stdio.h>

#include "ansi.h"

#include "telepathy-errors.h"

#define DEBUG_FLAG GABBLE_DEBUG_GROUPS

#include "debug.h"
#include "group-mixin.h"
#include "group-mixin-signals-marshal.h"

struct _GabbleGroupMixinPrivate {
    GabbleHandleSet *actors;
    GHashTable *handle_owners;
};

/**
 * gabble_group_mixin_class_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
gabble_group_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("GroupMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * gabble_group_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
gabble_group_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("GroupMixinOffsetQuark");
  return offset_quark;
}

void gabble_group_mixin_class_init (GObjectClass *obj_cls,
                                    glong offset,
                                    GabbleGroupMixinAddMemberFunc add_func,
                                    GabbleGroupMixinRemMemberFunc rem_func)
{
  GabbleGroupMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
                    GABBLE_GROUP_MIXIN_CLASS_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin_cls = GABBLE_GROUP_MIXIN_CLASS (obj_cls);

  mixin_cls->add_member = add_func;
  mixin_cls->remove_member = rem_func;

  mixin_cls->group_flags_changed_signal_id =
    g_signal_new ("group-flags-changed",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  group_mixin_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  mixin_cls->members_changed_signal_id =
    g_signal_new ("members-changed",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  group_mixin_marshal_VOID__STRING_BOXED_BOXED_BOXED_BOXED_INT_INT,
                  G_TYPE_NONE, 7, G_TYPE_STRING, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, G_TYPE_UINT, G_TYPE_UINT);
}

void gabble_group_mixin_init (GObject *obj,
                              glong offset,
                              GabbleHandleRepo *handle_repo,
                              GabbleHandle self_handle)
{
  GabbleGroupMixin *mixin;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    GABBLE_GROUP_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = GABBLE_GROUP_MIXIN (obj);

  mixin->handle_repo = handle_repo;
  mixin->self_handle = self_handle;

  mixin->group_flags = 0;

  mixin->members = handle_set_new (handle_repo, TP_HANDLE_TYPE_CONTACT);
  mixin->local_pending = handle_set_new (handle_repo, TP_HANDLE_TYPE_CONTACT);
  mixin->remote_pending = handle_set_new (handle_repo, TP_HANDLE_TYPE_CONTACT);

  mixin->priv = g_new0 (GabbleGroupMixinPrivate, 1);
  mixin->priv->handle_owners = g_hash_table_new (g_direct_hash, g_direct_equal);
  mixin->priv->actors = handle_set_new (handle_repo, TP_HANDLE_TYPE_CONTACT);
}

static void
handle_owners_foreach_unref (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  GabbleGroupMixin *mixin = user_data;

  gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                       GPOINTER_TO_UINT (key));
  gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                       GPOINTER_TO_UINT (value));
}

void gabble_group_mixin_finalize (GObject *obj)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  handle_set_destroy (mixin->priv->actors);

  g_hash_table_foreach (mixin->priv->handle_owners,
                        handle_owners_foreach_unref,
                        mixin);

  g_hash_table_destroy (mixin->priv->handle_owners);

  g_free (mixin->priv);

  handle_set_destroy (mixin->members);
  handle_set_destroy (mixin->local_pending);
  handle_set_destroy (mixin->remote_pending);
}

gboolean
gabble_group_mixin_get_self_handle (GObject *obj, guint *ret, GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  if (handle_set_is_member (mixin->members, mixin->self_handle) ||
      handle_set_is_member (mixin->local_pending, mixin->self_handle) ||
      handle_set_is_member (mixin->remote_pending, mixin->self_handle))
    {
      *ret = mixin->self_handle;
    }
  else
    {
      *ret = 0;
    }

  return TRUE;
}

gboolean
gabble_group_mixin_get_group_flags (GObject *obj, guint *ret, GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  *ret = mixin->group_flags;

  return TRUE;
}

gboolean
gabble_group_mixin_add_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error)
{
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  guint i;
  GabbleHandle handle;

  /* reject invalid handles */
  if (!gabble_handles_are_valid (mixin->handle_repo,
                                 TP_HANDLE_TYPE_CONTACT,
                                 contacts,
                                 FALSE,
                                 error))
    return FALSE;

  /* check that adding is allowed by flags */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_ADD) == 0 &&
          !handle_set_is_member (mixin->local_pending, handle))
        {
          DEBUG ("handle %u cannot be added to members without GROUP_FLAG_CAN_ADD",
              handle);

          *error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
              "handle %u cannot be added to members without GROUP_FLAG_CAN_ADD",
              handle);

          return FALSE;
        }
    }

  /* add handle by handle */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if (handle_set_is_member (mixin->members, handle))
        {
          DEBUG ("handle %u is already a member, skipping", handle);

          continue;
        }

      if (!mixin_cls->add_member (obj, handle, message, error))
        {
          return FALSE;
        }
    }

  return TRUE;
}

gboolean
gabble_group_mixin_remove_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error)
{
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  guint i;
  GabbleHandle handle;

  /* reject invalid handles */
  if (!gabble_handles_are_valid (mixin->handle_repo,
                                 TP_HANDLE_TYPE_CONTACT,
                                 contacts,
                                 FALSE,
                                 error))
    return FALSE;

  /* check removing is allowed by flags */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if (handle_set_is_member (mixin->members, handle))
        {
          if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_REMOVE) == 0)
            {
              DEBUG ("handle %u cannot be removed from members without GROUP_FLAG_CAN_REMOVE",
                  handle);

              *error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                  "handle %u cannot be removed from members without GROUP_FLAG_CAN_REMOVE",
                  handle);

              return FALSE;
            }
        }
      else if (handle_set_is_member (mixin->remote_pending, handle))
        {
          if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_RESCIND) == 0)
            {
              DEBUG ("handle %u cannot be removed from remote pending without GROUP_FLAG_CAN_RESCIND",
                  handle);

              *error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                  "handle %u cannot be removed from remote pending without GROUP_FLAG_CAN_RESCIND",
                  handle);

              return FALSE;
            }
        }
      else if (!handle_set_is_member (mixin->local_pending, handle))
        {
          DEBUG ("handle %u is not a current or pending member",
                   handle);

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
              "handle %u is not a current or pending member", handle);

          return FALSE;
        }
    }

  /* remove handle by handle */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if (!mixin_cls->remove_member (obj, handle, message, error))
        {
          return FALSE;
        }
    }

  return TRUE;
}

gboolean
gabble_group_mixin_get_members (GObject *obj, GArray **ret, GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  *ret = handle_set_to_array (mixin->members);

  return TRUE;
}

gboolean
gabble_group_mixin_get_local_pending_members (GObject *obj, GArray **ret, GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  *ret = handle_set_to_array (mixin->local_pending);

  return TRUE;
}

gboolean
gabble_group_mixin_get_remote_pending_members (GObject *obj, GArray **ret, GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  *ret = handle_set_to_array (mixin->remote_pending);

  return TRUE;
}

gboolean
gabble_group_mixin_get_all_members (GObject *obj, GArray **ret, GArray **ret1, GArray **ret2, GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  *ret = handle_set_to_array (mixin->members);
  *ret1 = handle_set_to_array (mixin->local_pending);
  *ret2 = handle_set_to_array (mixin->remote_pending);

  return TRUE;
}

gboolean
gabble_group_mixin_get_handle_owners (GObject *obj,
                                      const GArray *handles,
                                      GArray **ret,
                                      GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinPrivate *priv = mixin->priv;
  guint i;

  if ((mixin->group_flags &
        TP_CHANNEL_GROUP_FLAG_CHANNEL_SPECIFIC_HANDLES) == 0)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
          "channel doesn't have channel specific handles");

      return FALSE;
    }

  if (!gabble_handles_are_valid (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                                 handles, FALSE, error))
    {
      return FALSE;
    }

  *ret = g_array_sized_new (FALSE, FALSE, sizeof (GabbleHandle), handles->len);

  for (i = 0; i < handles->len; i++)
    {
      GabbleHandle local_handle = g_array_index (handles, GabbleHandle, i);
      GabbleHandle owner_handle;

      if (!handle_set_is_member (mixin->members, local_handle))
        {
          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
              "handle %u is not a member", local_handle);

          g_array_free (*ret, TRUE);
          *ret = NULL;

          return FALSE;
        }

      owner_handle = GPOINTER_TO_UINT (
          g_hash_table_lookup (priv->handle_owners,
                               GUINT_TO_POINTER (local_handle)));

      g_array_append_val (*ret, owner_handle);
    }

  return TRUE;
}

#define GFTS_APPEND_FLAG_IF_SET(flag) \
  if (flags & flag) \
    { \
      if (i++ > 0) \
        g_string_append (str, "|"); \
      g_string_append (str, #flag + 22); \
    }

static gchar *
group_flags_to_string (TpChannelGroupFlags flags)
{
  gint i = 0;
  GString *str;

  str = g_string_new ("[" ANSI_BOLD_OFF);

  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_ADD);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_REMOVE);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_REJECT);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND);

  g_string_append (str, ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

/**
 * gabble_group_mixin_change_flags:
 *
 * Request a change to be made to the flags. Emits the
 * signal with the changes which were made.
 */
void
gabble_group_mixin_change_flags (GObject *obj,
                                 TpChannelGroupFlags add,
                                 TpChannelGroupFlags remove)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  TpChannelGroupFlags added, removed;

  added = add & ~mixin->group_flags;
  mixin->group_flags |= added;

  removed = remove & mixin->group_flags;
  mixin->group_flags &= ~removed;

  if (add != 0 || remove != 0)
    {
      gchar *str_added, *str_removed, *str_flags;

      if (DEBUGGING)
        {
          str_added = group_flags_to_string (added);
          str_removed = group_flags_to_string (removed);
          str_flags = group_flags_to_string (mixin->group_flags);

          printf (ANSI_BOLD_ON ANSI_FG_WHITE
                  "%s: emitting group flags changed\n"
                  "  added    : %s\n"
                  "  removed  : %s\n"
                  "  flags now: %s\n" ANSI_RESET,
                  G_STRFUNC, str_added, str_removed, str_flags);

          fflush (stdout);

          g_free (str_added);
          g_free (str_removed);
          g_free (str_flags);
        }

      g_signal_emit(obj, mixin_cls->group_flags_changed_signal_id, 0, added, removed);
    }
}

static gchar *
member_array_to_string (GabbleHandleRepo *repo, const GArray *array)
{
  GString *str;
  guint i;

  str = g_string_new ("[" ANSI_BOLD_OFF);

  for (i = 0; i < array->len; i++)
    {
      GabbleHandle handle;
      const gchar *handle_str;

      handle = g_array_index (array, guint32, i);
      handle_str = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

      g_string_append_printf (str, "%s%u (%s)",
          (i > 0) ? "\n              " : "",
          handle, handle_str);
    }

  g_string_append (str, ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

static void remove_handle_owners_if_exist (GObject *obj, GArray *array);

/**
 * gabble_group_mixin_change_members:
 *
 * Request members to be added, removed or marked as local or remote pending.
 * Changes member sets, references, and emits the MembersChanged signal.
 */
gboolean
gabble_group_mixin_change_members (GObject *obj,
                                   const gchar *message,
                                   GIntSet *add,
                                   GIntSet *remove,
                                   GIntSet *local_pending,
                                   GIntSet *remote_pending,
                                   GabbleHandle actor,
                                   guint reason)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  GIntSet *new_add, *new_remove, *new_local_pending,
          *new_remote_pending, *tmp, *tmp2;
  gboolean ret;

  g_assert (add != NULL);
  g_assert (remove != NULL);
  g_assert (local_pending != NULL);
  g_assert (remote_pending != NULL);

  /* members + add */
  new_add = handle_set_update (mixin->members, add);

  /* members - remove */
  new_remove = handle_set_difference_update (mixin->members, remove);

  /* members - local_pending */
  tmp = handle_set_difference_update (mixin->members, local_pending);
  g_intset_destroy (tmp);

  /* members - remote_pending */
  tmp = handle_set_difference_update (mixin->members, remote_pending);
  g_intset_destroy (tmp);


  /* local pending + local_pending */
  new_local_pending = handle_set_update (mixin->local_pending, local_pending);

  /* local pending - add */
  tmp = handle_set_difference_update (mixin->local_pending, add);
  g_intset_destroy (tmp);

  /* local pending - remove */
  tmp = handle_set_difference_update (mixin->local_pending, remove);
  tmp2 = g_intset_union (new_remove, tmp);
  g_intset_destroy (new_remove);
  g_intset_destroy (tmp);
  new_remove = tmp2;

  /* local pending - remote_pending */
  tmp = handle_set_difference_update (mixin->local_pending, remote_pending);
  g_intset_destroy (tmp);


  /* remote pending + remote_pending */
  new_remote_pending = handle_set_update (mixin->remote_pending, remote_pending);

  /* remote pending - add */
  tmp = handle_set_difference_update (mixin->remote_pending, add);
  g_intset_destroy (tmp);

  /* remote pending - remove */
  tmp = handle_set_difference_update (mixin->remote_pending, remove);
  tmp2 = g_intset_union (new_remove, tmp);
  g_intset_destroy (new_remove);
  g_intset_destroy (tmp);
  new_remove = tmp2;

  /* remote pending - local_pending */
  tmp = handle_set_difference_update (mixin->remote_pending, local_pending);
  g_intset_destroy (tmp);

  if (g_intset_size (new_add) > 0 ||
      g_intset_size (new_remove) > 0 ||
      g_intset_size (new_local_pending) > 0 ||
      g_intset_size (new_remote_pending) > 0)
    {
      GArray *arr_add, *arr_remove, *arr_local, *arr_remote;
      gchar *add_str, *rem_str, *local_str, *remote_str;

      /* translate intsets to arrays */
      arr_add = g_intset_to_array (new_add);
      arr_remove = g_intset_to_array (new_remove);
      arr_local = g_intset_to_array (new_local_pending);
      arr_remote = g_intset_to_array (new_remote_pending);

      /* remove any handle owner mappings */
      remove_handle_owners_if_exist (obj, arr_remove);

      if (DEBUGGING)
        {
          add_str = member_array_to_string (mixin->handle_repo, arr_add);
          rem_str = member_array_to_string (mixin->handle_repo, arr_remove);
          local_str = member_array_to_string (mixin->handle_repo, arr_local);
          remote_str = member_array_to_string (mixin->handle_repo, arr_remote);

          printf (ANSI_BOLD_ON ANSI_FG_CYAN
                  "%s: emitting members changed\n"
                  "  message       : \"%s\"\n"
                  "  added         : %s\n"
                  "  removed       : %s\n"
                  "  local_pending : %s\n"
                  "  remote_pending: %s\n" ANSI_RESET,
                  G_STRFUNC, message, add_str, rem_str, local_str, remote_str);

          fflush (stdout);

          g_free (add_str);
          g_free (rem_str);
          g_free (local_str);
          g_free (remote_str);
        }

      if (actor)
        {
          handle_set_add (mixin->priv->actors, actor);
        }
      /* emit signal */
      g_signal_emit(obj, mixin_cls->members_changed_signal_id, 0,
                    message,
                    arr_add, arr_remove,
                    arr_local, arr_remote,
                    actor, reason);

      /* free arrays */
      g_array_free (arr_add, TRUE);
      g_array_free (arr_remove, TRUE);
      g_array_free (arr_local, TRUE);
      g_array_free (arr_remote, TRUE);

      ret = TRUE;
    }
  else
    {
      DEBUG ("not emitting signal, nothing changed");

      ret = FALSE;
    }

  /* free intsets */
  g_intset_destroy (new_add);
  g_intset_destroy (new_remove);
  g_intset_destroy (new_local_pending);
  g_intset_destroy (new_remote_pending);

  return ret;
}

void
gabble_group_mixin_add_handle_owner (GObject *obj,
                                     GabbleHandle local_handle,
                                     GabbleHandle owner_handle)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinPrivate *priv = mixin->priv;

  g_hash_table_insert (priv->handle_owners, GUINT_TO_POINTER (local_handle),
                       GUINT_TO_POINTER (owner_handle));

  gabble_handle_ref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                     local_handle);
  gabble_handle_ref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                     owner_handle);
}

static void
remove_handle_owners_if_exist (GObject *obj, GArray *array)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinPrivate *priv = mixin->priv;
  guint i;

  for (i = 0; i < array->len; i++)
    {
      GabbleHandle handle = g_array_index (array, guint32, i);
      gpointer local_handle, owner_handle;

      if (g_hash_table_lookup_extended (priv->handle_owners,
                                        GUINT_TO_POINTER (handle),
                                        &local_handle,
                                        &owner_handle))
        {
          gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                               GPOINTER_TO_UINT (local_handle));
          gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                               GPOINTER_TO_UINT (owner_handle));

          g_hash_table_remove (priv->handle_owners, GUINT_TO_POINTER (handle));
        }
    }
}

