/*
 * gabble-call-content.h - Header for GabbleCallContent
 * Copyright (C) 2009 Collabora Ltd.
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

#ifndef __GABBLE_CALL_CONTENT_H__
#define __GABBLE_CALL_CONTENT_H__

#include <glib-object.h>

#include <telepathy-yell/base-call-content.h>
#include "jingle-content.h"
#include "call-member-content.h"

G_BEGIN_DECLS

typedef struct _GabbleCallContent GabbleCallContent;
typedef struct _GabbleCallContentPrivate GabbleCallContentPrivate;
typedef struct _GabbleCallContentClass GabbleCallContentClass;

struct _GabbleCallContentClass {
    TpyBaseCallContentClass parent_class;
};

struct _GabbleCallContent {
    TpyBaseCallContent parent;

    GabbleCallContentPrivate *priv;
};

GType gabble_call_content_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_CONTENT \
  (gabble_call_content_get_type ())
#define GABBLE_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      GABBLE_TYPE_CALL_CONTENT, GabbleCallContent))
#define GABBLE_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
    GABBLE_TYPE_CALL_CONTENT, GabbleCallContentClass))
#define GABBLE_IS_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_CONTENT))
#define GABBLE_IS_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_CONTENT))
#define GABBLE_CALL_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
    GABBLE_TYPE_CALL_CONTENT, GabbleCallContentClass))

JingleMediaType gabble_call_content_get_media_type (GabbleCallContent *self);
GList *gabble_call_content_get_local_codecs (GabbleCallContent *self);
void gabble_call_content_new_offer (GabbleCallContent *self,
    GabbleCallMemberContent *content);

void gabble_call_content_add_member_content (GabbleCallContent *self,
    GabbleCallMemberContent *content);

GList *gabble_call_content_get_member_contents (GabbleCallContent *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_CALL_CONTENT_H__*/
