/*
 * group-mixin.c - Source for TpGroupMixin
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

/**
 * SECTION:group-mixin
 * @title: TpGroupMixin
 * @short_description: a mixin implementation of the groups interface
 * @see_also: #TpSvcChannelInterfaceGroup
 *
 * This mixin can be added to a channel GObject class to implement the
 * groups interface in a general way.
 *
 * To use the group mixin, include a #TpGroupMixinClass somewhere in your
 * class structure and a #TpGroupMixin somewhere in your instance structure,
 * and call tp_group_mixin_class_init() from your class_init function,
 * tp_group_mixin_init() from your init function or constructor, and
 * tp_group_mixin_finalize() from your dispose or finalize function.
 *
 * To use the group mixin as the implementation of
 * #TpSvcChannelInterfaceGroup, call
 * <literal>G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
 * tp_group_mixin_iface_init)</literal> in the fourth argument to
 * <literal>G_DEFINE_TYPE_WITH_CODE</literal>.
 */

#include <telepathy-glib/group-mixin.h>

#include <dbus/dbus-glib.h>
#include <stdio.h>

#include <telepathy-glib/debug-ansi.h>
#include <telepathy-glib/errors.h>

#define DEBUG_FLAG TP_DEBUG_GROUPS

#include "internal-debug.h"

#include "_gen/signals-marshal.h"

#define TP_CHANNEL_GROUP_LOCAL_PENDING_WITH_INFO_ENTRY_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
        G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, \
        G_TYPE_INVALID))

static const char *
group_change_reason_str (guint reason)
{
  switch (reason)
    {
    case TP_CHANNEL_GROUP_CHANGE_REASON_NONE:
      return "unspecified reason";
    case TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE:
      return "offline";
    case TP_CHANNEL_GROUP_CHANGE_REASON_KICKED:
      return "kicked";
    case TP_CHANNEL_GROUP_CHANGE_REASON_BUSY:
      return "busy";
    case TP_CHANNEL_GROUP_CHANGE_REASON_INVITED:
      return "invited";
    case TP_CHANNEL_GROUP_CHANGE_REASON_BANNED:
      return "banned";
    default:
      return "(unknown reason code)";
    }
}

typedef struct {
  TpHandle actor;
  guint reason;
  const gchar *message;
  TpHandleRepoIface *repo;
} LocalPendingInfo;

static LocalPendingInfo *
local_pending_info_new (TpHandleRepoIface *repo,
                        TpHandle actor,
                        guint reason,
                        const gchar *message)
{
  LocalPendingInfo *info = g_slice_new0 (LocalPendingInfo);
  info->actor = actor;
  info->reason = reason;
  info->message = g_strdup (message);
  info->repo = repo;
  if (actor)
    tp_handle_ref (repo, actor);

  return info;
}

static void
local_pending_info_free (LocalPendingInfo *info)
{
  g_free ((gchar *)info->message);
  if (info->actor)
    tp_handle_unref (info->repo, info->actor);
  g_slice_free (LocalPendingInfo, info);
}

struct _TpGroupMixinPrivate {
    TpHandleSet *actors;
    GHashTable *handle_owners;
    GHashTable *local_pending_info;
};


/**
 * tp_group_mixin_class_get_offset_quark:
 *
 * <!--Returns: says it all-->
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
tp_group_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string ("TpGroupMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * tp_group_mixin_get_offset_quark:
 *
 * <!--Returns: says it all-->
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
tp_group_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string ("TpGroupMixinOffsetQuark");
  return offset_quark;
}

/**
 * tp_group_mixin_class_init:
 * @obj_cls: The class of an object implementing the group interface using this
 *  mixin
 * @offset: The offset of the TpGroupMixinClass structure within the class
 *  structure
 * @add_func: A callback to be used to add contacts to this group
 * @rem_func: A callback to be used to remove contacts from this group
 *
 * Configure the mixin for use with the given class.
 */
void
tp_group_mixin_class_init (GObjectClass *obj_cls,
                           glong offset,
                           TpGroupMixinAddMemberFunc add_func,
                           TpGroupMixinRemMemberFunc rem_func)
{
  TpGroupMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
                    TP_GROUP_MIXIN_CLASS_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin_cls = TP_GROUP_MIXIN_CLASS (obj_cls);

  mixin_cls->add_member = add_func;
  mixin_cls->remove_member = rem_func;
}

/**
 * tp_group_mixin_init:
 * @obj: An object implementing the group interface using this mixin
 * @offset: The offset of the TpGroupMixin structure within the instance
 *  structure
 * @handle_repo: The connection's handle repository for contacts
 * @self_handle: The handle of the local user in this group, if any
 *
 * Initialize the mixin.
 */
void
tp_group_mixin_init (GObject *obj,
                     glong offset,
                     TpHandleRepoIface *handle_repo,
                     TpHandle self_handle)
{
  TpGroupMixin *mixin;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    TP_GROUP_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = TP_GROUP_MIXIN (obj);

  mixin->handle_repo = handle_repo;
  mixin->self_handle = self_handle;

  mixin->group_flags = 0;

  mixin->members = tp_handle_set_new (handle_repo);
  mixin->local_pending = tp_handle_set_new (handle_repo);
  mixin->remote_pending = tp_handle_set_new (handle_repo);

  mixin->priv = g_slice_new0 (TpGroupMixinPrivate);
  mixin->priv->handle_owners = g_hash_table_new (NULL, NULL);
  mixin->priv->local_pending_info = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify)local_pending_info_free);
  mixin->priv->actors = tp_handle_set_new (handle_repo);
}

static void
handle_owners_foreach_unref (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  TpGroupMixin *mixin = user_data;

  tp_handle_unref (mixin->handle_repo, GPOINTER_TO_UINT (key));
  tp_handle_unref (mixin->handle_repo, GPOINTER_TO_UINT (value));
}

/**
 * tp_group_mixin_finalize:
 * @obj: An object implementing the group interface using this mixin
 *
 * Unreference handles and free resources used by this mixin.
 */
void
tp_group_mixin_finalize (GObject *obj)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  tp_handle_set_destroy (mixin->priv->actors);

  g_hash_table_foreach (mixin->priv->handle_owners,
                        handle_owners_foreach_unref,
                        mixin);

  g_hash_table_destroy (mixin->priv->handle_owners);
  g_hash_table_destroy (mixin->priv->local_pending_info);

  g_slice_free (TpGroupMixinPrivate, mixin->priv);

  tp_handle_set_destroy (mixin->members);
  tp_handle_set_destroy (mixin->local_pending);
  tp_handle_set_destroy (mixin->remote_pending);
}

/**
 * tp_group_mixin_get_self_handle:
 * @obj: An object implementing the group mixin using this interface
 * @ret: Used to return the local user's handle in this group
 * @error: Unused
 *
 * Set the guint pointed to by ret to the local user's handle in this
 * group, or to 0 if the local user is not present in this group.
 *
 * Returns: %TRUE.
 */
gboolean
tp_group_mixin_get_self_handle (GObject *obj,
                                guint *ret,
                                GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  if (tp_handle_set_is_member (mixin->members, mixin->self_handle) ||
      tp_handle_set_is_member (mixin->local_pending, mixin->self_handle) ||
      tp_handle_set_is_member (mixin->remote_pending, mixin->self_handle))
    {
      *ret = mixin->self_handle;
    }
  else
    {
      *ret = 0;
    }

  return TRUE;
}

static void
tp_group_mixin_get_self_handle_async (TpSvcChannelInterfaceGroup *obj,
                                      DBusGMethodInvocation *context)
{
  guint ret;
  GError *error = NULL;

  if (tp_group_mixin_get_self_handle ((GObject *)obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_self_handle (
          context, ret);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_get_group_flags:
 * @obj: An object implementing the group mixin using this interface
 * @ret: Used to return the flags
 * @error: Unused
 *
 * Set the guint pointed to by ret to this group's flags, to be
 * interpreted according to TpChannelGroupFlags.
 *
 * Returns: %TRUE
 */
gboolean
tp_group_mixin_get_group_flags (GObject *obj,
                                guint *ret,
                                GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = mixin->group_flags;

  return TRUE;
}

static void
tp_group_mixin_get_group_flags_async (TpSvcChannelInterfaceGroup *obj,
                                      DBusGMethodInvocation *context)
{
  guint ret;
  GError *error = NULL;

  if (tp_group_mixin_get_group_flags ((GObject *)obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_group_flags (
          context, ret);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_add_members:
 * @obj: An object implementing the group interface using this mixin
 * @contacts: A GArray of guint representing contacts
 * @message: A message associated with the addition request, if supported
 * @error: Used to return an error if %FALSE is returned
 *
 * Request that the given contacts be added to the group as if in response
 * to user action. If the group's flags prohibit this, raise
 * PermissionDenied. If any of the handles is invalid, raise InvalidHandle.
 * Otherwise attempt to add the contacts by calling the callbacks provided
 * by the channel implementation.
 *
 * Returns: %TRUE on success
 */
gboolean
tp_group_mixin_add_members (GObject *obj,
                            const GArray *contacts,
                            const gchar *message,
                            GError **error)
{
  TpGroupMixinClass *mixin_cls = TP_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  guint i;
  TpHandle handle;

  /* reject invalid handles */
  if (!tp_handles_are_valid (mixin->handle_repo, contacts, FALSE, error))
    return FALSE;

  /* check that adding is allowed by flags */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_ADD) == 0 &&
          !tp_handle_set_is_member (mixin->local_pending, handle))
        {
          DEBUG ("handle %u cannot be added to members without "
              "GROUP_FLAG_CAN_ADD", handle);

          g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
              "handle %u cannot be added to members without "
              "GROUP_FLAG_CAN_ADD", handle);

          return FALSE;
        }
    }

  /* add handle by handle */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if (tp_handle_set_is_member (mixin->members, handle))
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

static void
tp_group_mixin_add_members_async (TpSvcChannelInterfaceGroup *obj,
                                  const GArray *contacts,
                                  const gchar *message,
                                  DBusGMethodInvocation *context)
{
  GError *error = NULL;

  if (tp_group_mixin_add_members ((GObject *)obj, contacts, message, &error))
    {
      tp_svc_channel_interface_group_return_from_add_members (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_remove_members:
 * @obj: An object implementing the group interface using this mixin
 * @contacts: A GArray of guint representing contacts
 * @message: A message to be sent to those contacts, if supported
 * @error: Used to return an error if %FALSE is returned
 *
 * Request that the given contacts be removed from the group as if in response
 * to user action. If the group's flags prohibit this, raise
 * PermissionDenied. If any of the handles is invalid, raise InvalidHandle.
 * If any of the handles is absent from the group, raise NotAvailable.
 * Otherwise attempt to remove the contacts by calling the callbacks provided
 * by the channel implementation.
 *
 * Returns: %TRUE on success
 */
gboolean
tp_group_mixin_remove_members (GObject *obj,
                               const GArray *contacts,
                               const gchar *message,
                               GError **error)
{
  TpGroupMixinClass *mixin_cls = TP_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  guint i;
  TpHandle handle;

  /* reject invalid handles */
  if (!tp_handles_are_valid (mixin->handle_repo, contacts, FALSE, error))
    return FALSE;

  /* check removing is allowed by flags */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if (tp_handle_set_is_member (mixin->members, handle))
        {
          if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_REMOVE) == 0)
            {
              DEBUG ("handle %u cannot be removed from members without "
                  "GROUP_FLAG_CAN_REMOVE", handle);

              g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                  "handle %u cannot be removed from members without "
                  "GROUP_FLAG_CAN_REMOVE", handle);

              return FALSE;
            }
        }
      else if (tp_handle_set_is_member (mixin->remote_pending, handle))
        {
          if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_RESCIND) == 0)
            {
              DEBUG ("handle %u cannot be removed from remote pending "
                  "without GROUP_FLAG_CAN_RESCIND", handle);

              g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                  "handle %u cannot be removed from remote pending without "
                  "GROUP_FLAG_CAN_RESCIND", handle);

              return FALSE;
            }
        }
      else if (!tp_handle_set_is_member (mixin->local_pending, handle))
        {
          DEBUG ("handle %u is not a current or pending member",
                   handle);

          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "handle %u is not a current or pending member", handle);

          return FALSE;
        }
    }

  /* remove handle by handle */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if (!mixin_cls->remove_member (obj, handle, message, error))
        {
          return FALSE;
        }
    }

  return TRUE;
}

static void
tp_group_mixin_remove_members_async (TpSvcChannelInterfaceGroup *obj,
                                     const GArray *contacts,
                                     const gchar *message,
                                     DBusGMethodInvocation *context)
{
  GError *error = NULL;

  if (tp_group_mixin_remove_members ((GObject *)obj, contacts, message,
        &error))
    {
      tp_svc_channel_interface_group_return_from_remove_members (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_get_members:
 * @obj: An object implementing the group interface using this mixin
 * @ret: Used to return a GArray of guint contact handles
 * @error: Unused
 *
 * Get the group's current members
 *
 * Returns: %TRUE
 */
gboolean
tp_group_mixin_get_members (GObject *obj,
                            GArray **ret,
                            GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = tp_handle_set_to_array (mixin->members);

  return TRUE;
}

static void
tp_group_mixin_get_members_async (TpSvcChannelInterfaceGroup *obj,
                                  DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_members ((GObject *)obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_members (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_get_local_pending_members:
 * @obj: An object implementing the group interface using this mixin
 * @ret: Used to return a GArray of guint contact handles
 * @error: Unused
 *
 * Get the group's local-pending members.
 *
 * Returns: %TRUE
 */
gboolean
tp_group_mixin_get_local_pending_members (GObject *obj,
                                          GArray **ret,
                                          GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = tp_handle_set_to_array (mixin->local_pending);

  return TRUE;
}

static void
tp_group_mixin_get_local_pending_members_async (TpSvcChannelInterfaceGroup *obj,
                                                DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_local_pending_members ((GObject *)obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_local_pending_members (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

typedef struct {
    TpGroupMixin *mixin;
    GPtrArray *array;
} _mixin_and_array_of_info;

static void
local_pending_members_with_info_foreach (TpHandleSet *set,
                                         TpHandle i,
                                         gpointer userdata)
{
  _mixin_and_array_of_info *data = (_mixin_and_array_of_info *)userdata;
  TpGroupMixinPrivate *priv = data->mixin->priv;
  GValue entry = { 0, };
  LocalPendingInfo *info = g_hash_table_lookup (priv->local_pending_info,
                                                GUINT_TO_POINTER(i));
  g_assert (info != NULL);

  g_value_init (&entry, TP_CHANNEL_GROUP_LOCAL_PENDING_WITH_INFO_ENTRY_TYPE);
  g_value_take_boxed (&entry,
      dbus_g_type_specialized_construct (
          TP_CHANNEL_GROUP_LOCAL_PENDING_WITH_INFO_ENTRY_TYPE));

  dbus_g_type_struct_set (&entry,
      0, i,
      1, info->actor,
      2, info->reason,
      3, info->message,
      G_MAXUINT);

  g_ptr_array_add (data->array, g_value_get_boxed (&entry));
}

/**
 * tp_group_mixin_get_local_pending_members_with_info:
 * @obj: An object implementing the group interface using this mixin
 * @ret: Used to return a GPtrArray of D-Bus structures each containing
 *  the handle of a local-pending contact, the handle of a contact
 *  responsible for adding them to the group (or 0), the reason code
 *  and a related message (e.g. their request to join the group)
 * @error: Unused
 *
 * Get the group's local-pending members and information about their
 * requests to join the channel.
 *
 * Returns: %TRUE
 */
gboolean
tp_group_mixin_get_local_pending_members_with_info (
                                               GObject *obj,
                                               GPtrArray **ret,
                                               GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  _mixin_and_array_of_info data = { mixin, NULL };

  *ret = g_ptr_array_new ();
  data.array = *ret;

  tp_handle_set_foreach (mixin->local_pending,
      local_pending_members_with_info_foreach, &data);

  return TRUE;
}

static void
tp_group_mixin_get_local_pending_members_with_info_async (
                                                TpSvcChannelInterfaceGroup *obj,
                                                DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_local_pending_members_with_info ((GObject *)obj, &ret,
        &error))
    {
      guint i;
      tp_svc_channel_interface_group_return_from_get_local_pending_members_with_info (
          context, ret);
      for (i = 0 ; i < ret->len; i++) {
        g_value_array_free (g_ptr_array_index (ret,i));
      }
      g_ptr_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_get_remote_pending_members:
 * @obj: An object implementing the group interface using this mixin
 * @ret: Used to return a GArray of guint representing the handles
 *  of the group's remote pending members
 * @error: Unused
 *
 * Get the group's remote-pending members.
 *
 * Returns: %TRUE
 */
gboolean
tp_group_mixin_get_remote_pending_members (GObject *obj,
                                           GArray **ret,
                                           GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = tp_handle_set_to_array (mixin->remote_pending);

  return TRUE;
}

static void
tp_group_mixin_get_remote_pending_members_async (TpSvcChannelInterfaceGroup *obj,
                                                 DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_remote_pending_members ((GObject *)obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_remote_pending_members (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_get_all_members:
 * @obj: An object implementing the group interface using this mixin
 * @members: Used to return a GArray of guint representing the handles
 *  of the group's members
 * @local_pending: Used to return a GArray of guint representing the handles
 *  of the group's local pending members
 * @remote_pending: Used to return a GArray of guint representing the handles
 *  of the group's remote pending members
 * @error: Unused
 *
 * Get the group's current and pending members.
 *
 * Returns: %TRUE
 */
gboolean
tp_group_mixin_get_all_members (GObject *obj,
                                GArray **members,
                                GArray **local_pending,
                                GArray **remote_pending,
                                GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *members = tp_handle_set_to_array (mixin->members);
  *local_pending = tp_handle_set_to_array (mixin->local_pending);
  *remote_pending = tp_handle_set_to_array (mixin->remote_pending);

  return TRUE;
}

static void
tp_group_mixin_get_all_members_async (TpSvcChannelInterfaceGroup *obj,
                                      DBusGMethodInvocation *context)
{
  GArray *mem, *local, *remote;
  GError *error = NULL;

  if (tp_group_mixin_get_all_members ((GObject *)obj, &mem, &local, &remote,
        &error))
    {
      tp_svc_channel_interface_group_return_from_get_all_members (
          context, mem, local, remote);
      g_array_free (mem, TRUE);
      g_array_free (local, TRUE);
      g_array_free (remote, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_group_mixin_get_handle_owners:
 * @obj: An object implementing the group interface with this mixin
 * @handles: An array of guint representing locally valid handles
 * @ret: Used to return an array of guint representing globally valid
 *  handles, or 0 where unavailable, if %TRUE is returned
 * @error: Used to return an error if %FALSE is returned
 *
 * If the mixin has the flag %TP_CHANNEL_GROUP_FLAG_CHANNEL_SPECIFIC_HANDLES,
 * return the global owners of the given local handles, or 0 where
 * unavailable.
 *
 * Returns: %TRUE (setting @ret) on success, %FALSE (setting @error) on
 * failure
 */
gboolean
tp_group_mixin_get_handle_owners (GObject *obj,
                                  const GArray *handles,
                                  GArray **ret,
                                  GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpGroupMixinPrivate *priv = mixin->priv;
  guint i;

  if ((mixin->group_flags &
        TP_CHANNEL_GROUP_FLAG_CHANNEL_SPECIFIC_HANDLES) == 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "channel doesn't have channel specific handles");

      return FALSE;
    }

  if (!tp_handles_are_valid (mixin->handle_repo, handles, FALSE, error))
    {
      return FALSE;
    }

  *ret = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), handles->len);

  for (i = 0; i < handles->len; i++)
    {
      TpHandle local_handle = g_array_index (handles, TpHandle, i);
      TpHandle owner_handle;

      if (!tp_handle_set_is_member (mixin->members, local_handle))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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

static void
tp_group_mixin_get_handle_owners_async (TpSvcChannelInterfaceGroup *obj,
                                        const GArray *handles,
                                        DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_handle_owners ((GObject *)obj, handles, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_handle_owners (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
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

  str = g_string_new ("[" TP_ANSI_BOLD_OFF);

  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_ADD);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_REMOVE);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_REJECT);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND);

  g_string_append (str, TP_ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

/**
 * tp_group_mixin_change_flags:
 * @obj: An object implementing the groups interface using this mixin
 * @add: Flags to be added
 * @remove: Flags to be removed
 *
 * Request a change to be made to the flags. Emits the
 * GroupFlagsChanged signal with the changes which were actually made.
 *
 * It is an error to set any of the same bits in both @add and @remove.
 */
void
tp_group_mixin_change_flags (GObject *obj,
                             TpChannelGroupFlags add,
                             TpChannelGroupFlags remove)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpChannelGroupFlags added, removed;

  /* It's meaningless to want to add and remove the same capability */
  g_return_if_fail ((add & remove) == 0);

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

          printf (TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE
                  "%s: emitting group flags changed\n"
                  "  added    : %s\n"
                  "  removed  : %s\n"
                  "  flags now: %s\n" TP_ANSI_RESET,
                  G_STRFUNC, str_added, str_removed, str_flags);

          fflush (stdout);

          g_free (str_added);
          g_free (str_removed);
          g_free (str_flags);
        }

      tp_svc_channel_interface_group_emit_group_flags_changed (obj, added,
          removed);
    }
}

static gchar *
member_array_to_string (TpHandleRepoIface *repo,
                        const GArray *array)
{
  GString *str;
  guint i;

  str = g_string_new ("[" TP_ANSI_BOLD_OFF);

  for (i = 0; i < array->len; i++)
    {
      TpHandle handle;
      const gchar *handle_str;

      handle = g_array_index (array, guint, i);
      handle_str = tp_handle_inspect (repo, handle);

      g_string_append_printf (str, "%s%u (%s)",
          (i > 0) ? "\n              " : "",
          handle, handle_str);
    }

  g_string_append (str, TP_ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

static void remove_handle_owners_if_exist (GObject *obj,
    GArray *array);

typedef struct {
    TpGroupMixin *mixin;
    LocalPendingInfo *info;
} _mixin_and_info;

static void
local_pending_added_foreach (guint i,
                             gpointer userdata)
{
  _mixin_and_info *data = (_mixin_and_info *)userdata;
  TpGroupMixinPrivate *priv = data->mixin->priv;

  g_hash_table_insert (priv->local_pending_info,
      GUINT_TO_POINTER (i),
      local_pending_info_new (data->mixin->handle_repo,
      data->info->actor, data->info->reason, data->info->message));
}

static void
local_pending_added (TpGroupMixin *mixin,
                    TpIntSet *added,
                    TpHandle actor,
                    guint reason,
                    const gchar *message)
{
  LocalPendingInfo info;
  _mixin_and_info data = { mixin, &info };
  info.actor = actor;
  info.reason = reason;
  info.message = message;

  tp_intset_foreach (added, local_pending_added_foreach, &data);
}

static void
local_pending_remove_foreach (guint i,
                             gpointer userdata)
{
  TpGroupMixin *mixin = (TpGroupMixin *) userdata;
  TpGroupMixinPrivate *priv = mixin->priv;

  g_hash_table_remove (priv->local_pending_info, GUINT_TO_POINTER(i));
}

static void
local_pending_remove (TpGroupMixin *mixin,
                     TpIntSet *removed)
{
  tp_intset_foreach (removed, local_pending_remove_foreach, mixin);
}

/**
 * tp_group_mixin_change_members:
 * @obj: An object implementing the group interface using this mixin
 * @message: A message to be sent to the affected contacts if possible
 * @add: A set of contact handles to be added to the members (if not
 *  already present) and removed from local pending and remote pending
 *  (if present)
 * @remove: A set of contact handles to be removed from members,
 *  local pending or remote pending, wherever they are present
 * @add_local_pending: A set of contact handles to be added to local pending,
 *  and removed from members and remote pending
 * @add_remote_pending: A set of contact handles to be added to remote pending,
 *  and removed from members and local pending
 * @actor: The handle of the contact responsible for this change
 * @reason: The reason for this change
 *
 * Change the sets of members as given by the arguments, and emit the
 * MembersChanged signal if the changes were not a no-op.
 *
 * This function must be called in response to events on the underlying
 * IM protocol, and must not be called in direct response to user input;
 * it does not respect the permissions flags, but changes the group directly.
 *
 * If any two of add, remove, add_local_pending and add_remote_pending have
 * a non-empty intersection, the result is undefined. Don't do that.
 *
 * Returns: %TRUE if the group was changed and the MembersChanged signal
 *  was emitted; %FALSE if nothing actually changed and the signal was
 *  suppressed.
 */
gboolean
tp_group_mixin_change_members (GObject *obj,
                               const gchar *message,
                               TpIntSet *add,
                               TpIntSet *remove,
                               TpIntSet *add_local_pending,
                               TpIntSet *add_remote_pending,
                               TpHandle actor,
                               TpChannelGroupChangeReason reason)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpIntSet *new_add, *new_remove, *new_local_pending,
           *new_remote_pending, *tmp, *tmp2, *empty;
  gboolean ret;

  empty = tp_intset_new ();

  if (add == NULL)
    add = empty;

  if (remove == NULL)
    remove = empty;

  if (add_local_pending == NULL)
    add_local_pending = empty;

  if (add_remote_pending == NULL)
    add_remote_pending = empty;

  /* members + add */
  new_add = tp_handle_set_update (mixin->members, add);

  /* members - remove */
  new_remove = tp_handle_set_difference_update (mixin->members, remove);

  /* members - add_local_pending */
  tmp = tp_handle_set_difference_update (mixin->members, add_local_pending);
  tp_intset_destroy (tmp);

  /* members - add_remote_pending */
  tmp = tp_handle_set_difference_update (mixin->members, add_remote_pending);
  tp_intset_destroy (tmp);


  /* local pending + add_local_pending */
  new_local_pending = tp_handle_set_update (mixin->local_pending,
      add_local_pending);
  local_pending_added (mixin, add_local_pending, actor, reason, message);

  /* local pending - add */
  tmp = tp_handle_set_difference_update (mixin->local_pending, add);
  local_pending_remove (mixin, tmp);
  tp_intset_destroy (tmp);

  /* local pending - remove */
  tmp = tp_handle_set_difference_update (mixin->local_pending, remove);
  local_pending_remove (mixin, tmp);

  tmp2 = tp_intset_union (new_remove, tmp);
  tp_intset_destroy (new_remove);
  tp_intset_destroy (tmp);
  new_remove = tmp2;

  /* local pending - add_remote_pending */
  tmp = tp_handle_set_difference_update (mixin->local_pending,
      add_remote_pending);
  local_pending_remove (mixin, tmp);
  tp_intset_destroy (tmp);


  /* remote pending + add_remote_pending */
  new_remote_pending = tp_handle_set_update (mixin->remote_pending,
      add_remote_pending);

  /* remote pending - add */
  tmp = tp_handle_set_difference_update (mixin->remote_pending, add);
  tp_intset_destroy (tmp);

  /* remote pending - remove */
  tmp = tp_handle_set_difference_update (mixin->remote_pending, remove);
  tmp2 = tp_intset_union (new_remove, tmp);
  tp_intset_destroy (new_remove);
  tp_intset_destroy (tmp);
  new_remove = tmp2;

  /* remote pending - local_pending */
  tmp = tp_handle_set_difference_update (mixin->remote_pending,
      add_local_pending);
  tp_intset_destroy (tmp);

  if (tp_intset_size (new_add) > 0 ||
      tp_intset_size (new_remove) > 0 ||
      tp_intset_size (new_local_pending) > 0 ||
      tp_intset_size (new_remote_pending) > 0)
    {
      GArray *arr_add, *arr_remove, *arr_local, *arr_remote;
      gchar *add_str, *rem_str, *local_str, *remote_str;

      /* translate intsets to arrays */
      arr_add = tp_intset_to_array (new_add);
      arr_remove = tp_intset_to_array (new_remove);
      arr_local = tp_intset_to_array (new_local_pending);
      arr_remote = tp_intset_to_array (new_remote_pending);

      /* remove any handle owner mappings */
      remove_handle_owners_if_exist (obj, arr_remove);

      if (DEBUGGING)
        {
          add_str = member_array_to_string (mixin->handle_repo, arr_add);
          rem_str = member_array_to_string (mixin->handle_repo, arr_remove);
          local_str = member_array_to_string (mixin->handle_repo, arr_local);
          remote_str = member_array_to_string (mixin->handle_repo, arr_remote);

          printf (TP_ANSI_BOLD_ON TP_ANSI_FG_CYAN
                  "%s: emitting members changed\n"
                  "  message       : \"%s\"\n"
                  "  added         : %s\n"
                  "  removed       : %s\n"
                  "  local_pending : %s\n"
                  "  remote_pending: %s\n"
                  "  actor         : %u\n"
                  "  reason        : %u: %s\n" TP_ANSI_RESET,
                  G_STRFUNC, message, add_str, rem_str, local_str, remote_str,
                  actor, reason, group_change_reason_str (reason));

          fflush (stdout);

          g_free (add_str);
          g_free (rem_str);
          g_free (local_str);
          g_free (remote_str);
        }

      if (actor)
        {
          tp_handle_set_add (mixin->priv->actors, actor);
        }
      /* emit signal */
      tp_svc_channel_interface_group_emit_members_changed (obj, message,
          arr_add, arr_remove, arr_local, arr_remote, actor, reason);

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
  tp_intset_destroy (new_add);
  tp_intset_destroy (new_remove);
  tp_intset_destroy (new_local_pending);
  tp_intset_destroy (new_remote_pending);
  tp_intset_destroy (empty);

  return ret;
}

/**
 * tp_group_mixin_add_handle_owner:
 * @obj: A GObject implementing the group interface with this mixin
 * @local_handle: A contact handle valid within this group (may not be 0)
 * @owner_handle: A contact handle valid globally (may not be 0)
 *
 * Note that the given local handle is an alias within this group
 * for the given globally-valid handle. It will be returned from subsequent
 * GetHandleOwner queries where appropriate.
 */
void
tp_group_mixin_add_handle_owner (GObject *obj,
                                 TpHandle local_handle,
                                 TpHandle owner_handle)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpGroupMixinPrivate *priv = mixin->priv;

  g_return_if_fail (local_handle != 0);
  g_return_if_fail (owner_handle != 0);

  g_hash_table_insert (priv->handle_owners, GUINT_TO_POINTER (local_handle),
                       GUINT_TO_POINTER (owner_handle));

  tp_handle_ref (mixin->handle_repo, local_handle);
  tp_handle_ref (mixin->handle_repo, owner_handle);
}

static void
remove_handle_owners_if_exist (GObject *obj,
                               GArray *array)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpGroupMixinPrivate *priv = mixin->priv;
  guint i;

  for (i = 0; i < array->len; i++)
    {
      TpHandle handle = g_array_index (array, guint, i);
      gpointer local_handle, owner_handle;

      if (g_hash_table_lookup_extended (priv->handle_owners,
                                        GUINT_TO_POINTER (handle),
                                        &local_handle,
                                        &owner_handle))
        {
          tp_handle_unref (mixin->handle_repo,
              GPOINTER_TO_UINT (local_handle));
          tp_handle_unref (mixin->handle_repo,
              GPOINTER_TO_UINT (owner_handle));

          g_hash_table_remove (priv->handle_owners, GUINT_TO_POINTER (handle));
        }
    }
}

/**
 * tp_group_mixin_iface_init:
 * @g_iface: A #TpSvcChannelInterfaceGroupClass
 * @iface_data: Unused
 *
 * Fill in the vtable entries needed to implement the group interface using
 * this mixin. This function should usually be called via
 * G_IMPLEMENT_INTERFACE.
 */
void
tp_group_mixin_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceGroupClass *klass =
    (TpSvcChannelInterfaceGroupClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_group_implement_##x (klass,\
    tp_group_mixin_##x##_async)
  IMPLEMENT(add_members);
  IMPLEMENT(get_all_members);
  IMPLEMENT(get_group_flags);
  IMPLEMENT(get_handle_owners);
  IMPLEMENT(get_local_pending_members);
  IMPLEMENT(get_local_pending_members_with_info);
  IMPLEMENT(get_members);
  IMPLEMENT(get_remote_pending_members);
  IMPLEMENT(get_self_handle);
  IMPLEMENT(remove_members);
#undef IMPLEMENT
}
