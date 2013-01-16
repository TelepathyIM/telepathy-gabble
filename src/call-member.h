/*
 * call-member.h - Header for CallMember
 * Copyright (C) 2010 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#ifndef __GABBLE_CALL_MEMBER_H__
#define __GABBLE_CALL_MEMBER_H__

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>
#include <wocky/wocky.h>

#include "types.h"
#include "call-member-content.h"

G_BEGIN_DECLS

typedef struct _GabbleCallMemberPrivate GabbleCallMemberPrivate;
typedef struct _GabbleCallMemberClass GabbleCallMemberClass;

struct _GabbleCallMemberClass {
    GObjectClass parent_class;
};

struct _GabbleCallMember {
    GObject parent;
    GabbleCallMemberPrivate *priv;
};

GType gabble_call_member_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_MEMBER \
  (gabble_call_member_get_type ())
#define GABBLE_CALL_MEMBER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CALL_MEMBER, \
    GabbleCallMember))
#define GABBLE_CALL_MEMBER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CALL_MEMBER, \
    GabbleCallMemberClass))
#define GABBLE_IS_CALL_MEMBER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_MEMBER))
#define GABBLE_IS_CALL_MEMBER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_MEMBER))
#define GABBLE_CALL_MEMBER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CALL_MEMBER, \
   GabbleCallMemberClass))

void gabble_call_member_set_session (GabbleCallMember *member,
    WockyJingleSession *session);

TpHandle gabble_call_member_get_handle (
    GabbleCallMember *self);

WockyJingleSession *gabble_call_member_get_session (
    GabbleCallMember *self);

TpCallMemberFlags gabble_call_member_get_flags (
  GabbleCallMember *self);

GList *gabble_call_member_get_contents (GabbleCallMember *self);

GabbleCallMemberContent * gabble_call_member_ensure_content (
    GabbleCallMember *self,
    const gchar *name,
    WockyJingleMediaType mtype);

GabbleCallMemberContent * gabble_call_member_create_content (
    GabbleCallMember *self,
    const gchar *name,
    WockyJingleMediaType mtype,
    WockyJingleContentSenders senders,
    GError **error);

gboolean gabble_call_member_start_session (GabbleCallMember *self,
    const gchar *audio_name,
    const gchar *video_name,
    GError **error);

gboolean gabble_call_member_open_session (GabbleCallMember *self,
    GError **error);

void gabble_call_member_accept (GabbleCallMember *self);

GabbleConnection * gabble_call_member_get_connection (GabbleCallMember *self);

const gchar *gabble_call_member_get_transport_ns (GabbleCallMember *self);

void gabble_call_member_shutdown (GabbleCallMember *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_CALL_MEMBER_H__*/
