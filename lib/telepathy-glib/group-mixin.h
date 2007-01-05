/*
 * group-mixin.h - Header for TpGroupMixin
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

#ifndef __TP_GROUP_MIXIN_H__
#define __TP_GROUP_MIXIN_H__

#include "telepathy-glib/tp-handle-repo.h"
#include "telepathy-glib/util.h"

G_BEGIN_DECLS

typedef struct _TpGroupMixinClass TpGroupMixinClass;
typedef struct _TpGroupMixin TpGroupMixin;
typedef struct _TpGroupMixinPrivate TpGroupMixinPrivate;

typedef gboolean (*TpGroupMixinAddMemberFunc) (GObject *obj, TpHandle handle, const gchar *message, GError **error);
typedef gboolean (*TpGroupMixinRemMemberFunc) (GObject *obj, TpHandle handle, const gchar *message, GError **error);

struct _TpGroupMixinClass {
  TpGroupMixinAddMemberFunc add_member;
  TpGroupMixinRemMemberFunc remove_member;

  guint group_flags_changed_signal_id;
  guint members_changed_signal_id;
};

struct _TpGroupMixin {
  TpHandleRepoIface *handle_repo;
  TpHandle self_handle;

  TpChannelGroupFlags group_flags;

  TpHandleSet *members;
  TpHandleSet *local_pending;
  TpHandleSet *remote_pending;

  TpGroupMixinPrivate *priv;
};

/* TYPE MACROS */
#define TP_GROUP_MIXIN_CLASS_OFFSET_QUARK (tp_group_mixin_class_get_offset_quark())
#define TP_GROUP_MIXIN_CLASS_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), TP_GROUP_MIXIN_CLASS_OFFSET_QUARK)))
#define TP_GROUP_MIXIN_CLASS(o) ((TpGroupMixinClass *) tp_mixin_offset_cast (o, TP_GROUP_MIXIN_CLASS_OFFSET (o)))

#define TP_GROUP_MIXIN_OFFSET_QUARK (tp_group_mixin_get_offset_quark())
#define TP_GROUP_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), TP_GROUP_MIXIN_OFFSET_QUARK)))
#define TP_GROUP_MIXIN(o) ((TpGroupMixin *) tp_mixin_offset_cast (o, TP_GROUP_MIXIN_OFFSET(o)))

GQuark tp_group_mixin_class_get_offset_quark (void);
GQuark tp_group_mixin_get_offset_quark (void);

void tp_group_mixin_class_init (GObjectClass *obj_cls, glong offset, TpGroupMixinAddMemberFunc add_func, TpGroupMixinRemMemberFunc rem_func);

void tp_group_mixin_init (GObject *obj, glong offset, TpHandleRepoIface *handle_repo, TpHandle self_handle);
void tp_group_mixin_finalize (GObject *obj);

gboolean tp_group_mixin_get_self_handle (GObject *obj, guint *ret, GError **error);
gboolean tp_group_mixin_get_group_flags (GObject *obj, guint *ret, GError **error);

gboolean tp_group_mixin_add_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error);
gboolean tp_group_mixin_remove_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error);

gboolean tp_group_mixin_get_members (GObject *obj, GArray **ret, GError **error);
gboolean tp_group_mixin_get_local_pending_members (GObject *obj, GArray **ret, GError **error);
gboolean tp_group_mixin_get_remote_pending_members (GObject *obj, GArray **ret, GError **error);
gboolean tp_group_mixin_get_all_members (GObject *obj, GArray **ret, GArray **ret1, GArray **ret2, GError **error);

gboolean tp_group_mixin_get_handle_owners (GObject *obj, const GArray *handles, GArray **ret, GError **error);

void tp_group_mixin_change_flags (GObject *obj, TpChannelGroupFlags add, TpChannelGroupFlags remove);
gboolean tp_group_mixin_change_members (GObject *obj, const gchar *message, TpIntSet *add, TpIntSet *remove, TpIntSet *local_pending, TpIntSet *remote_pending, TpHandle actor, guint reason);

void tp_group_mixin_add_handle_owner (GObject *obj, TpHandle local_handle, TpHandle owner_handle);

G_END_DECLS

#endif /* #ifndef __TP_GROUP_MIXIN_H__ */
