/*
 * slacker.h - header for Maemo device state monitor
 * Copyright ©2010 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef GABBLE_SLACKER_H
#define GABBLE_SLACKER_H

#include <glib-object.h>

typedef struct _GabbleSlacker GabbleSlacker;
typedef struct _GabbleSlackerClass GabbleSlackerClass;
typedef struct _GabbleSlackerPrivate GabbleSlackerPrivate;

struct _GabbleSlackerClass {
    GObjectClass parent_class;
};

struct _GabbleSlacker {
    GObject parent;

    GabbleSlackerPrivate *priv;
};

GType gabble_slacker_get_type (void);

GabbleSlacker *gabble_slacker_new (void);
gboolean gabble_slacker_is_inactive (GabbleSlacker *self);

/* TYPE MACROS */
#define GABBLE_TYPE_SLACKER \
  (gabble_slacker_get_type ())
#define GABBLE_SLACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_SLACKER, GabbleSlacker))
#define GABBLE_SLACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_SLACKER,\
                           GabbleSlackerClass))
#define GABBLE_IS_SLACKER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_SLACKER))
#define GABBLE_IS_SLACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_SLACKER))
#define GABBLE_SLACKER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_SLACKER, \
                              GabbleSlackerClass))

#endif /* GABBLE_SLACKER_H */
