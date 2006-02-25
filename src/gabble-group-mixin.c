/*
 * gabble-group-mixin.c - Source for GabbleGroupMixin
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include "gabble-group-mixin.h"
#include "gabble-group-mixin-signals-marshal.h"
#include "telepathy-errors.h"

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
  GabbleGroupMixinClass *mixin_cls = GABBLE_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
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

  g_debug ("%s: emitting group flags changed, added 0x%X, removed 0x%X", G_STRFUNC, added, removed);

  g_signal_emit(obj, mixin_cls->group_flags_changed_signal_id, 0, added, removed);
}

/**
 * FIXME: Really horrible -- just a quick hack for debugging.
 */
static gchar *
g_array_to_string (const GArray *array)
{
  gchar *buf, *p;
  guint i;

  buf = g_strdup ("(");

  for (i = 0; i < array->len; i++)
    {
      p = g_strdup_printf ("%s%s%u", buf,
                           (i > 0) ? ", " : "",
                           g_array_index (array, guint32, i));

      g_free (buf);
      buf = p;
    }

  p = g_strdup_printf ("%s)", buf);
  g_free (buf);

  return p;
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
      gchar *add_str, *rem_str, *local_str, *remote_str;

      /* translate intsets to arrays */
      arr_add = g_intset_to_array (new_add);
      arr_remove = g_intset_to_array (new_remove);
      arr_local = g_intset_to_array (new_local_pending);
      arr_remote = g_intset_to_array (new_remote_pending);

      /* debug start */
      add_str = g_array_to_string (arr_add);
      rem_str = g_array_to_string (arr_remove);
      local_str = g_array_to_string (arr_local);
      remote_str = g_array_to_string (arr_remote);

      g_debug ("%s: emitting members changed\n"
               "  added: %s\n"
               "  removed: %s\n"
               "  local_pending: %s\n"
               "  remote_pending: %s",
               G_STRFUNC, add_str, rem_str, local_str, remote_str);

      g_free (add_str);
      g_free (rem_str);
      g_free (local_str);
      g_free (remote_str);
      /* debug end */

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

