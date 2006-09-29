/*
 * vcard-lookup.h - vCard lookup helper for Gabble connections
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
 */

#ifndef __GABBLE_VCARD_LOOKUP_H__
#define __GABBLE_VCARD_LOOKUP_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include "gabble-types.h"

G_BEGIN_DECLS

typedef struct _GabbleVCardLookupClass GabbleVCardLookupClass;
typedef struct _GabbleVCardLookupRequest GabbleVCardLookupRequest;

/**
 * GabbleVCardLookupError:
 * @GABBLE_VCARD_LOOKUP_ERROR_CANCELLED: The vCard request was cancelled
 * @GABBLE_VCARD_LOOKUP_ERROR_TIMEOUT: The vCard request timed out
 * @GABBLE_VCARD_LOOKUP_ERROR_UNKNOWN: An unknown error occured
 */
typedef enum
{
  GABBLE_VCARD_LOOKUP_ERROR_CANCELLED,
  GABBLE_VCARD_LOOKUP_ERROR_TIMEOUT,
  GABBLE_VCARD_LOOKUP_ERROR_UNKNOWN
} GabbleVCardLookupError;

GQuark gabble_vcard_lookup_error_quark (void);
#define GABBLE_VCARD_LOOKUP_ERROR gabble_vcard_lookup_error_quark ()

GType gabble_vcard_lookup_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_VCARD_LOOKUP \
  (gabble_vcard_lookup_get_type())
#define GABBLE_VCARD_LOOKUP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_VCARD_LOOKUP, GabbleVCardLookup))
#define GABBLE_VCARD_LOOKUP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_VCARD_LOOKUP, GabbleVCardLookupClass))
#define GABBLE_IS_VCARD_LOOKUP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_VCARD_LOOKUP))
#define GABBLE_IS_VCARD_LOOKUP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_VCARD_LOOKUP))
#define GABBLE_VCARD_LOOKUP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_VCARD_LOOKUP, GabbleVCardLookupClass))

struct _GabbleVCardLookupClass {
    GObjectClass parent_class;
};

struct _GabbleVCardLookup {
    GObject parent;
    gpointer priv;
};

typedef void (*GabbleVCardLookupCb)(GabbleVCardLookup *self,
                                    GabbleVCardLookupRequest *request,
                                    GabbleHandle handle,
                                    LmMessageNode *vcard,
                                    GError *error,
                                    gpointer user_data);

GabbleVCardLookup *gabble_vcard_lookup_new (GabbleConnection *);

GQuark gabble_vcard_lookup_cache_quark (void);

const gchar *gabble_vcard_lookup_get_cached_alias (GabbleVCardLookup *,
                                                   GabbleHandle);

GabbleVCardLookupRequest *gabble_vcard_lookup_request (GabbleVCardLookup *,
                                                       GabbleHandle,
                                                       guint timeout,
                                                       GabbleVCardLookupCb,
                                                       gpointer user_data,
                                                       GObject *object,
                                                       GError **error);

GabbleVCardLookupRequest *gabble_vcard_lookup_replace (GabbleVCardLookup *,
                                                       LmMessageNode *,
                                                       guint timeout,
                                                       GabbleVCardLookupCb,
                                                       gpointer user_data,
                                                       GObject *object,
                                                       GError **error);

GabbleVCardLookupRequest *gabble_vcard_lookup_edit (GabbleVCardLookup *,
                                                    guint timeout,
                                                    GabbleVCardLookupCb,
                                                    gpointer user_data,
                                                    GObject *object,
                                                    GError **error,
                                                    ...)
                                                   G_GNUC_NULL_TERMINATED;

const gchar *gabble_vcard_lookup_get_cached_alias (GabbleVCardLookup *,
                                                   GabbleHandle);

G_END_DECLS

#endif
