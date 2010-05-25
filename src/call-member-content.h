/*
 * call-member-content.h - Header for GabbleCallMemberContent
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

#ifndef __CALL_MEMBER_CONTENT_H__
#define __CALL_MEMBER_CONTENT_H__

#include <glib-object.h>

#include "types.h"
#include "jingle-content.h"
#include "jingle-media-rtp.h"

G_BEGIN_DECLS

typedef struct _GabbleCallMemberContentPrivate GabbleCallMemberContentPrivate;
typedef struct _GabbleCallMemberContentClass GabbleCallMemberContentClass;

struct _GabbleCallMemberContentClass {
    GObjectClass parent_class;
};

struct _GabbleCallMemberContent {
    GObject parent;
    GabbleCallMemberContentPrivate *priv;
};

GType gabble_call_member_content_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_MEMBER_CONTENT \
  (gabble_call_member_content_get_type ())
#define GABBLE_CALL_MEMBER_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CALL_MEMBER_CONTENT, \
    GabbleCallMemberContent))
#define GABBLE_CALL_MEMBER_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CALL_MEMBER_CONTENT, \
    GabbleCallMemberContentClass))
#define GABBLE_IS_CALL_MEMBER_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_MEMBER_CONTENT))
#define GABBLE_IS_CALL_MEMBER_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_MEMBER_CONTENT))
#define GABBLE_CALL_MEMBER_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CALL_MEMBER_CONTENT, \
    GabbleCallMemberContentClass))

GabbleCallMemberContent *gabble_call_member_content_new (const gchar *name,
    JingleMediaType type,
    GabbleCallMember *member);

GabbleCallMemberContent *gabble_call_member_content_from_jingle_content (
    GabbleJingleContent *jingle_content,
    GabbleCallMember *member);

JingleMediaType gabble_call_member_content_get_media_type (
    GabbleCallMemberContent *self);

const gchar *gabble_call_member_content_get_name (
    GabbleCallMemberContent *self);

GabbleJingleContent *gabble_call_member_content_get_jingle_content (
    GabbleCallMemberContent *self);

gboolean gabble_call_member_content_has_jingle_content (
    GabbleCallMemberContent *self);

GList *gabble_call_member_content_get_remote_codecs (
    GabbleCallMemberContent *self);

void gabble_call_member_content_set_remote_codecs (
    GabbleCallMemberContent *self,
    GList *codecs);

GList * gabble_call_member_content_get_remote_codecs (
    GabbleCallMemberContent *self);

GabbleCallMember *gabble_call_member_content_get_member (
    GabbleCallMemberContent *self);

void gabble_call_member_content_set_jingle_content (
    GabbleCallMemberContent *self,
    GabbleJingleContent *content);

void gabble_call_member_content_add_to_session (
    GabbleCallMemberContent *self);

void gabble_call_member_content_remove (
    GabbleCallMemberContent *self);

G_END_DECLS

#endif /* #ifndef __CALL_MEMBER_CONTENT_H__*/
