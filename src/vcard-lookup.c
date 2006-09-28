/*
 * vcard-lookup.c - Source for Gabble vCard lookup helper
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

#define DEBUG_FLAG GABBLE_DEBUG_VCARD_LOOKUP

#include "debug.h"
#include "vcard-lookup.h"
#include "gabble-connection.h"

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

G_DEFINE_TYPE(GabbleVCardLookup, gabble_vcard_lookup, G_TYPE_OBJECT);

typedef struct _GabbleVCardLookupPrivate GabbleVCardLookupPrivate;
struct _GabbleVCardLookupPrivate
{
  GabbleConnection *connection;
};

#define GABBLE_VCARD_LOOKUP_GET_PRIVATE(o)     ((GabbleVCardLookupPrivate*)((o)->priv));

static void
gabble_vcard_lookup_init (GabbleVCardLookup *obj)
{
  GabbleVCardLookupPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_VCARD_LOOKUP, GabbleVCardLookupPrivate);
  obj->priv = priv;

}

static void gabble_vcard_lookup_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gabble_vcard_lookup_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);
static void gabble_vcard_lookup_dispose (GObject *object);
static void gabble_vcard_lookup_finalize (GObject *object);

static void
gabble_vcard_lookup_class_init (GabbleVCardLookupClass *gabble_vcard_lookup_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_vcard_lookup_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_vcard_lookup_class, sizeof (GabbleVCardLookupPrivate));

  object_class->get_property = gabble_vcard_lookup_get_property;
  object_class->set_property = gabble_vcard_lookup_set_property;

  object_class->dispose = gabble_vcard_lookup_dispose;
  object_class->finalize = gabble_vcard_lookup_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "vCard lookup helper object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
gabble_vcard_lookup_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GabbleVCardLookup *chan = GABBLE_VCARD_LOOKUP (object);
  GabbleVCardLookupPrivate *priv = GABBLE_VCARD_LOOKUP_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_vcard_lookup_set_property (GObject     *object,
                                  guint        property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GabbleVCardLookup *chan = GABBLE_VCARD_LOOKUP (object);
  GabbleVCardLookupPrivate *priv = GABBLE_VCARD_LOOKUP_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gabble_vcard_lookup_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (gabble_vcard_lookup_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_vcard_lookup_parent_class)->dispose (object);
}

void
gabble_vcard_lookup_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_vcard_lookup_parent_class)->finalize (object);
}


/**
 * gabble_vcard_lookup_new:
 * @conn: The #GabbleConnection to use for vCard lookup
 *
 * Creates an object to use for Jabber vCard lookup (JEP 0054).
 * There should be one of these per connection
 */
GabbleVCardLookup *
gabble_vcard_lookup_new (GabbleConnection *conn)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);
  return GABBLE_VCARD_LOOKUP (g_object_new (GABBLE_TYPE_VCARD_LOOKUP, "connection", conn, NULL));
}
