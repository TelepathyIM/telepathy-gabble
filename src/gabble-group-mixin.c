/*
 * gabble-group-mixin.c - Source for GabbleGroupMixin
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include "gabble-group-mixin.h"
#include "gabble-group-mixin-signals-marshal.h"
#include "telepathy-errors.h"

/**
 * gabble_group_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on an
 *          object class
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

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
                    GABBLE_GROUP_MIXIN_OFFSET_QUARK,
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
                  gabble_group_mixin_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  mixin_cls->members_changed_signal_id =
    g_signal_new ("members-changed",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_group_mixin_marshal_VOID__STRING_BOXED_BOXED_BOXED_BOXED,
                  G_TYPE_NONE, 5, G_TYPE_STRING, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY);
}

void gabble_group_mixin_init (GObject *obj,
                              glong offset,
                              GabbleHandleRepo *handle_repo,
                              GabbleHandle self_handle)
{
  GabbleGroupMixin *mixin;

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    GABBLE_GROUP_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = GABBLE_GROUP_MIXIN (obj);

  mixin->handle_repo = handle_repo;
  mixin->self_handle = self_handle;

  mixin->members = handle_set_new (handle_repo, TP_HANDLE_TYPE_CONTACT);
  mixin->local_pending = handle_set_new (handle_repo, TP_HANDLE_TYPE_CONTACT);
  mixin->remote_pending = handle_set_new (handle_repo, TP_HANDLE_TYPE_CONTACT);
}

void gabble_group_mixin_finalize (GObject *obj)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  handle_set_destroy (mixin->members);
  handle_set_destroy (mixin->local_pending);
  handle_set_destroy (mixin->remote_pending);
}

gboolean
gabble_group_mixin_get_self_handle (GObject *obj, guint *ret, GError **error)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  *ret = mixin->self_handle;

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
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  guint i;
  GabbleHandle handle;

  /* reject invalid handles */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if (!gabble_handle_is_valid (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT, handle))
        {
          g_debug ("%s: invalid handle %u", G_STRFUNC, handle);

          *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
              "invalid handle %u", handle);

          return FALSE;
        }
    }

  /* add handle by handle */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

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
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  guint i;
  GabbleHandle handle;

  /* reject invalid handles */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if (!gabble_handle_is_valid (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT, handle))
        {
          g_debug ("%s: invalid handle %u", G_STRFUNC, handle);

          *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
              "invalid handle %u", handle);

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

/**
 * gabble_group_mixin_change_members:
 *
 * Request members to be added, removed or marked as local or remote pending.
 * Changes member sets, references, and emits the MembersChanged signal.
 */
void
gabble_group_mixin_change_members (GObject *obj,
                                   const gchar *message,
                                   GIntSet *add,
                                   GIntSet *remove,
                                   GIntSet *local_pending,
                                   GIntSet *remote_pending)
{
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  GIntSet *new_add, *new_remove, *new_local_pending,
          *new_remote_pending, *tmp, *tmp2;

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

      /* translate intsets to arrays */
      arr_add = g_intset_to_array (new_add);
      arr_remove = g_intset_to_array (new_remove);
      arr_local = g_intset_to_array (new_local_pending);
      arr_remote = g_intset_to_array (new_remote_pending);

      /* emit signal */
      g_signal_emit(obj, mixin_cls->members_changed_signal_id, 0,
                    message,
                    arr_add, arr_remove,
                    arr_local, arr_remote);

      /* free arrays */
      g_array_free (arr_add, TRUE);
      g_array_free (arr_remove, TRUE);
      g_array_free (arr_local, TRUE);
      g_array_free (arr_remote, TRUE);
    }
  else
    {
      g_debug ("%s: not emitting signal, nothing changed", G_STRFUNC);
    }

  /* free intsets */
  g_intset_destroy (new_add);
  g_intset_destroy (new_remove);
  g_intset_destroy (new_local_pending);
  g_intset_destroy (new_remote_pending);
}

