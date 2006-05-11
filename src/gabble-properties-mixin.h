/*
 * gabble-properties-mixin.h - Header for GabblePropertiesMixin
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

#ifndef __GABBLE_PROPERTIES_MIXIN_H__
#define __GABBLE_PROPERTIES_MIXIN_H__

#include "handles.h"
#include "handle-set.h"

G_BEGIN_DECLS

struct _GabblePropertySignature {
    gchar *name;
    GType type;
};

typedef struct _GabblePropertySignature GabblePropertySignature;

struct _GabbleProperty {
    GValue *value;
    guint flags;
};

typedef struct _GabbleProperty GabbleProperty;

typedef struct _GabblePropertiesContext GabblePropertiesContext;
typedef gboolean (*GabblePropertiesSetFunc) (GObject *obj, GabblePropertiesContext *ctx, GError **error);

struct _GabblePropertiesMixinClass {
  const GabblePropertySignature *signatures;
  guint num_props;

  GabblePropertiesSetFunc set_properties;

  guint property_flags_changed_signal_id;
  guint properties_changed_signal_id;
};

typedef struct _GabblePropertiesMixinClass GabblePropertiesMixinClass;

typedef struct _GabblePropertiesMixinPrivate GabblePropertiesMixinPrivate;

struct _GabblePropertiesMixin {
    GabbleProperty *properties;

    GabblePropertiesMixinPrivate *priv;
};

typedef struct _GabblePropertiesMixin GabblePropertiesMixin;

/* TYPE MACROS */
#define GABBLE_PROPERTIES_MIXIN_CLASS_OFFSET_QUARK (gabble_properties_mixin_class_get_offset_quark())
#define GABBLE_PROPERTIES_MIXIN_CLASS_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), GABBLE_PROPERTIES_MIXIN_CLASS_OFFSET_QUARK)))
#define GABBLE_PROPERTIES_MIXIN_CLASS(o) ((GabblePropertiesMixinClass *)((guchar *) o + GABBLE_PROPERTIES_MIXIN_CLASS_OFFSET (o)))

#define GABBLE_PROPERTIES_MIXIN_OFFSET_QUARK (gabble_properties_mixin_get_offset_quark())
#define GABBLE_PROPERTIES_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), GABBLE_PROPERTIES_MIXIN_OFFSET_QUARK)))
#define GABBLE_PROPERTIES_MIXIN(o) ((GabblePropertiesMixin *)((guchar *) o + GABBLE_PROPERTIES_MIXIN_OFFSET (o)))

#define TP_TYPE_PROPERTY_INFO_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_INFO_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_INFO_STRUCT))

#define TP_TYPE_PROPERTY_VALUE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_VALUE, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_VALUE_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_VALUE_STRUCT))

#define TP_TYPE_PROPERTY_FLAGS_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
#define TP_TYPE_PROPERTY_FLAGS_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_FLAGS_STRUCT))

GQuark gabble_properties_mixin_class_get_offset_quark (void);
GQuark gabble_properties_mixin_get_offset_quark (void);

void gabble_properties_mixin_class_init (GObjectClass *obj_cls, glong offset, const GabblePropertySignature *signatures, guint num_properties, GabblePropertiesSetFunc set_func);

void gabble_properties_mixin_init (GObject *obj, glong offset);
void gabble_properties_mixin_finalize (GObject *obj);

gboolean gabble_properties_mixin_list_properties (GObject *obj, GPtrArray **ret, GError **error);
gboolean gabble_properties_mixin_get_properties (GObject *obj, const GArray *properties, GPtrArray **ret, GError **error);
gboolean gabble_properties_mixin_set_properties (GObject *obj, const GPtrArray *properties, DBusGMethodInvocation *context);

gboolean gabble_properties_mixin_has_property (GObject *obj, const gchar *name, guint *property);

gboolean gabble_properties_context_has (GabblePropertiesContext *ctx, guint property);
gboolean gabble_properties_context_has_other_than (GabblePropertiesContext *ctx, guint property);
const GValue *gabble_properties_context_get (GabblePropertiesContext *ctx, guint property);
guint gabble_properties_context_get_value_count (GabblePropertiesContext *ctx);
void gabble_properties_context_remove (GabblePropertiesContext *ctx, guint property);
void gabble_properties_context_return (GabblePropertiesContext *ctx, GError *error);
gboolean gabble_properties_context_return_if_done (GabblePropertiesContext *ctx);

void gabble_properties_mixin_change_value (GObject *obj, guint prop_id, const GValue *new_value, GArray **props);
void gabble_properties_mixin_change_flags (GObject *obj, guint prop_id, TpPropertyFlags add, TpPropertyFlags remove, GArray **props);
void gabble_properties_mixin_emit_changed (GObject *obj, GArray **props);
void gabble_properties_mixin_emit_flags (GObject *obj, GArray **props);

G_END_DECLS

#endif /* #ifndef __GABBLE_PROPERTIES_MIXIN_H__ */
