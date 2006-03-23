/*
 * gabble-disco.h - Headers for Gabble service discovery
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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
 *
 * -- LETS DISCO!!!  \o/ \o_ _o/ /\o/\ _/o/- -\o\_ --
 */

#ifndef __GABBLE_DISCO_H__
#define __GABBLE_DISCO_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

G_BEGIN_DECLS

typedef enum
{
  GABBLE_DISCO_TYPE_INFO,
  GABBLE_DISCO_TYPE_ITEMS
} GabbleDiscoType;

typedef struct _GabbleDisco GabbleDisco;
typedef struct _GabbleDiscoClass GabbleDiscoClass;
typedef struct _GabbleDiscoRequest GabbleDiscoRequest;

/**
 * GabbleDiscoError:
 * @GABBLE_DISCO_ERROR_TIMEOUT: The DISCO request timed out
 * @GABBLE_DISCO_ERROR_NOT_FOUND: The requested item was not found
 * @GABBLE_DISCO_ERROR_UNAVAILABLE: The requested service was unavailiable
 */
typedef enum
{
  GABBLE_DISCO_ERROR_CANCELLED,
  GABBLE_DISCO_ERROR_TIMEOUT,
  GABBLE_DISCO_ERROR_NOT_FOUND,
  GABBLE_DISCO_ERROR_UNAVAILABLE
} GabbleDiscoError;

GQuark gabble_disco_error_quark (void);
#define GABBLE_DISCO_ERROR gabble_disco_error_quark ()

GType gabble_disco_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_DISCO \
  (gabble_disco_get_type())
#define GABBLE_DISCO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_DISCO, GabbleDisco))
#define GABBLE_DISCO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_DISCO, GabbleDiscoClass))
#define GABBLE_IS_DISCO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_DISCO))
#define GABBLE_IS_DISCO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_DISCO))
#define GABBLE_DISCO_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_DISCO, GabbleDiscoClass))

struct _GabbleDiscoClass {
    GObjectClass parent_class;
};

struct _GabbleDisco {
    GObject parent;
    gpointer priv;
};

typedef void (*GabbleDiscoCb)(GabbleDisco *self, const gchar *jid, const gchar *node, LmMessageNode *query_result, GError* error, gpointer user_data);

GabbleDisco *gabble_disco_new (GabbleConnection *);
GabbleDiscoRequest *gabble_disco_request (GabbleDisco *self,
    GabbleDiscoType type, const gchar *jid, const char *node,
    GabbleDiscoCb callback, gpointer user_data, GObject *object,
    GError **error);

void gabble_disco_cancel_request (GabbleDiscoRequest *);


G_END_DECLS

#endif
