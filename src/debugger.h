/*
 * debugger.h - header for Telepathy debug interface implementation
 * Copyright (C) 2009 Collabora Ltd.
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

#ifndef _GABBLE_DEBUGGER
#define _GABBLE_DEBUGGER

#include <glib-object.h>

#include <telepathy-glib/properties-mixin.h>
#include <telepathy-glib/dbus-properties-mixin.h>

#include "extensions/extensions.h"

G_BEGIN_DECLS

#define GABBLE_TYPE_DEBUGGER gabble_debugger_get_type()

#define GABBLE_DEBUGGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_DEBUGGER, GabbleDebugger))

#define GABBLE_DEBUGGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_DEBUGGER, GabbleDebuggerClass))

#define GABBLE_IS_DEBUGGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_DEBUGGER))

#define GABBLE_IS_DEBUGGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_DEBUGGER))

#define GABBLE_DEBUGGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_DEBUGGER, GabbleDebuggerClass))

/* On the basis that messages are around 60 bytes on average, and that 50kb is
 * a reasonable maximum size for a frame buffer.
 */

#define DEBUG_MESSAGE_LIMIT 800

typedef struct {
  gdouble timestamp;
  gchar *domain;
  GabbleDebugLevel level;
  gchar *string;
} GabbleDebugMessage;

typedef struct {
  GObject parent;

  gboolean enabled;
  GQueue *messages;
} GabbleDebugger;

typedef struct {
  GObjectClass parent_class;
  TpDBusPropertiesMixinClass dbus_props_class;
} GabbleDebuggerClass;

GType gabble_debugger_get_type (void);

GabbleDebugger *
gabble_debugger_get_singleton (void);

void
gabble_debugger_add_message (GabbleDebugger *self,
    GTimeVal *timestamp,
    const gchar *domain,
    GLogLevelFlags level,
    const gchar *string);

G_END_DECLS

#endif /* _GABBLE_DEBUGGER */
