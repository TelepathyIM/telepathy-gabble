/*
 * olpc-activity.h - Header for GabbleOlpcActivity
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __GABBLE_OLPC_ACTIVITY_H__
#define __GABBLE_OLPC_ACTIVITY_H__

#include <glib-object.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/handle-repo.h>

#include "connection.h"

G_BEGIN_DECLS

typedef struct _GabbleOlpcActivity GabbleOlpcActivity;
typedef struct _GabbleOlpcActivityClass GabbleOlpcActivityClass;
typedef struct _GabbleOlpcActivityPrivate GabbleOlpcActivityPrivate;

struct _GabbleOlpcActivityClass {
  GObjectClass parent_class;
};

struct _GabbleOlpcActivity {
  GObject parent;

  TpHandle room;
  gchar *id;
  GHashTable *properties;

  GabbleOlpcActivityPrivate *priv;
};

GType gabble_olpc_activity_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_OLPC_ACTIVITY \
  (gabble_olpc_activity_get_type ())
#define GABBLE_OLPC_ACTIVITY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_OLPC_ACTIVITY,\
                              GabbleOlpcActivity))
#define GABBLE_OLPC_ACTIVITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_OLPC_ACTIVITY,\
                           GabbleOlpcActivityClass))
#define GABBLE_IS_OLPC_ACTIVITY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_OLPC_ACTIVITY))
#define GABBLE_IS_OLPC_ACTIVITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_OLPC_ACTIVITY))
#define GABBLE_OLPC_ACTIVITY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_OLPC_ACTIVITY,\
                              GabbleOlpcActivityClass))

GabbleOlpcActivity * gabble_olpc_activity_new (GabbleConnection *conn,
    TpHandle room);

const gchar * gabble_olpc_activity_get_room (GabbleOlpcActivity *activity);

gboolean gabble_olpc_activity_is_visible (GabbleOlpcActivity *activity);

G_END_DECLS

#endif /* #ifndef __GABBLE_OLPC_ACTIVITY_H__ */
