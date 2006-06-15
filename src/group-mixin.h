/*
 * group-mixin.h - Header for GabbleGroupMixin
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

#ifndef __GABBLE_GROUP_MIXIN_H__
#define __GABBLE_GROUP_MIXIN_H__

#include "handles.h"
#include "handle-set.h"

G_BEGIN_DECLS

typedef struct _GabbleGroupMixinClass GabbleGroupMixinClass;
typedef struct _GabbleGroupMixin GabbleGroupMixin;
typedef struct _GabbleGroupMixinPrivate GabbleGroupMixinPrivate;

typedef gboolean (*GabbleGroupMixinAddMemberFunc) (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);
typedef gboolean (*GabbleGroupMixinRemMemberFunc) (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);

struct _GabbleGroupMixinClass {
  GabbleGroupMixinAddMemberFunc add_member;
  GabbleGroupMixinRemMemberFunc remove_member;

  guint group_flags_changed_signal_id;
  guint members_changed_signal_id;
};

struct _GabbleGroupMixin {
  GabbleHandleRepo *handle_repo;
  GabbleHandle self_handle;

  TpChannelGroupFlags group_flags;

  GabbleHandleSet *members;
  GabbleHandleSet *local_pending;
  GabbleHandleSet *remote_pending;

  GabbleGroupMixinPrivate *priv;
};

/* TYPE MACROS */
#define GABBLE_GROUP_MIXIN_CLASS_OFFSET_QUARK (gabble_group_mixin_class_get_offset_quark())
#define GABBLE_GROUP_MIXIN_CLASS_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), GABBLE_GROUP_MIXIN_CLASS_OFFSET_QUARK)))
#define GABBLE_GROUP_MIXIN_CLASS(o) ((GabbleGroupMixinClass *)((guchar *) o + GABBLE_GROUP_MIXIN_CLASS_OFFSET (o)))

#define GABBLE_GROUP_MIXIN_OFFSET_QUARK (gabble_group_mixin_get_offset_quark())
#define GABBLE_GROUP_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), GABBLE_GROUP_MIXIN_OFFSET_QUARK)))
#define GABBLE_GROUP_MIXIN(o) ((GabbleGroupMixin *)((guchar *) o + GABBLE_GROUP_MIXIN_OFFSET (o)))

GQuark gabble_group_mixin_class_get_offset_quark (void);
GQuark gabble_group_mixin_get_offset_quark (void);

void gabble_group_mixin_class_init (GObjectClass *obj_cls, glong offset, GabbleGroupMixinAddMemberFunc add_func, GabbleGroupMixinRemMemberFunc rem_func);

void gabble_group_mixin_init (GObject *obj, glong offset, GabbleHandleRepo *handle_repo, GabbleHandle self_handle);
void gabble_group_mixin_finalize (GObject *obj);

gboolean gabble_group_mixin_get_self_handle (GObject *obj, guint *ret, GError **error);
gboolean gabble_group_mixin_get_group_flags (GObject *obj, guint *ret, GError **error);

gboolean gabble_group_mixin_add_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error);
gboolean gabble_group_mixin_remove_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error);

gboolean gabble_group_mixin_get_members (GObject *obj, GArray **ret, GError **error);
gboolean gabble_group_mixin_get_local_pending_members (GObject *obj, GArray **ret, GError **error);
gboolean gabble_group_mixin_get_remote_pending_members (GObject *obj, GArray **ret, GError **error);
gboolean gabble_group_mixin_get_all_members (GObject *obj, GArray **ret, GArray **ret1, GArray **ret2, GError **error);

gboolean gabble_group_mixin_get_handle_owners (GObject *obj, const GArray *handles, GArray **ret, GError **error);

void gabble_group_mixin_change_flags (GObject *obj, TpChannelGroupFlags add, TpChannelGroupFlags remove);
gboolean gabble_group_mixin_change_members (GObject *obj, const gchar *message, GIntSet *add, GIntSet *remove, GIntSet *local_pending, GIntSet *remote_pending);

void gabble_group_mixin_add_handle_owner (GObject *obj, GabbleHandle local_handle, GabbleHandle owner_handle);

G_END_DECLS

#endif /* #ifndef __GABBLE_GROUP_MIXIN_H__ */
