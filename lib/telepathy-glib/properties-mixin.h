/*
 * properties-mixin.h - Header for TpPropertiesMixin
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

#ifndef __TP_PROPERTIES_MIXIN_H__
#define __TP_PROPERTIES_MIXIN_H__

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/intset.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/util.h>

G_BEGIN_DECLS

/**
 * TpPropertySignature:
 * @name: Name of the property as seen in the Telepathy D-Bus protocol
 * @type: GType which the variant value must have
 *
 * Signature of a property in the Telepathy properties interface.
 */
struct _TpPropertySignature {
    gchar *name;
    GType type;
};

typedef struct _TpPropertySignature TpPropertySignature;

/**
 * TpProperty:
 * @value: The value of the property
 * @flags: #TpPropertyFlags indicating the property's permissions
 *
 * Structure representing the current state of a property.
 */
struct _TpProperty {
    GValue *value;
    guint flags;
};

typedef struct _TpProperty TpProperty;

/**
 * TpPropertiesContext:
 *
 * Opaque pointer representing an incomplete property-setting operation, used
 * in the implementation of SetProperties.
 */
typedef struct _TpPropertiesContext TpPropertiesContext;

/**
 * TpPropertiesSetFunc:
 * @obj: An object with the properties mixin
 * @ctx: A properties context
 * @error: Set to the error if %FALSE is returned
 *
 * A callback used to implement the SetProperties D-Bus method by setting
 * properties in the underlying IM protocol.
 *
 * The callback must either:
 *
 * * return %FALSE to indicate immediate failure
 * * call tp_properties_context_return() with an error to indicate failure
 * * call tp_properties_context_remove() to remove each property from the set
 *   of pending properties, then call tp_properties_context_return_if_done()
 *   or tp_properties_context_return() when all were set
 *
 * Returns: %FALSE on immediate failure, %TRUE otherwise
 */
typedef gboolean (*TpPropertiesSetFunc) (GObject *obj,
    TpPropertiesContext *ctx, GError **error);

/* Doesn't yet exist. For future expansion. */
typedef struct _TpPropertiesMixinClassPrivate TpPropertiesMixinClassPrivate;

/**
 * TpPropertiesMixinClass:
 * @signatures: Array of property signatures, as provided via
 *  tp_properties_mixin_class_init().
 * @num_props: Number of properties supported.
 * @set_properties: Properties setting function, as supplied to
 *  tp_properties_mixin_class_init().
 * @priv: Pointer to opaque private data.
 *
 * Structure to be placed in a GObjectClass-derived structure containing
 * settings for the properties mixin. Initialize it using
 * tp_properties_mixin_class_init().
 *
 * All fields should be considered read-only.
 */
struct _TpPropertiesMixinClass {
  const TpPropertySignature *signatures;
  guint num_props;

  TpPropertiesSetFunc set_properties;

  TpPropertiesMixinClassPrivate *priv;
};

typedef struct _TpPropertiesMixinClass TpPropertiesMixinClass;

typedef struct _TpPropertiesMixinPrivate TpPropertiesMixinPrivate;

/**
 * TpPropertiesMixin:
 * @properties: Array of property values. Should be considered read-only.
 * @priv: Pointer to opaque private data.
 *
 * Structure to be placed in a GObject-derived structure containing
 * instance data for the properties mixin (i.e. the properties). Initialize
 * it using tp_properties_mixin_init().
 */
struct _TpPropertiesMixin {
    TpProperty *properties;

    TpPropertiesMixinPrivate *priv;
};

typedef struct _TpPropertiesMixin TpPropertiesMixin;

/* TYPE MACROS */
#define TP_PROPERTIES_MIXIN_CLASS_OFFSET_QUARK \
  (tp_properties_mixin_class_get_offset_quark ())
#define TP_PROPERTIES_MIXIN_CLASS_OFFSET(o) \
  (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), \
                                      TP_PROPERTIES_MIXIN_CLASS_OFFSET_QUARK)))
#define TP_PROPERTIES_MIXIN_CLASS(o) \
  ((TpPropertiesMixinClass *) tp_mixin_offset_cast (o,\
    TP_PROPERTIES_MIXIN_CLASS_OFFSET (o)))

#define TP_PROPERTIES_MIXIN_OFFSET_QUARK \
  (tp_properties_mixin_get_offset_quark ())
#define TP_PROPERTIES_MIXIN_OFFSET(o) \
  (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), \
                                       TP_PROPERTIES_MIXIN_OFFSET_QUARK)))
#define TP_PROPERTIES_MIXIN(o) \
  ((TpPropertiesMixin *) tp_mixin_offset_cast (o, \
    TP_PROPERTIES_MIXIN_OFFSET (o)))

/**
 * TP_TYPE_PROPERTY_INFO_STRUCT:
 *
 * The GType of the structures with D-Bus signature '(ussu)' returned by
 * ListProperties.
 */
#define TP_TYPE_PROPERTY_INFO_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
/**
 * TP_TYPE_PROPERTY_INFO_LIST:
 *
 * The GType of the return from ListProperties (i.e. a GPtrArray
 * of structures of type TP_TYPE_PROPERTY_INFO_STRUCT), corresponding to
 * D-Bus signature 'a(ussu)'.
 */
#define TP_TYPE_PROPERTY_INFO_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_INFO_STRUCT))

/**
 * TP_TYPE_PROPERTY_VALUE_STRUCT:
 *
 * The GType of the structures with signature '(uv)' returned by
 * GetProperties and emitted in PropertiesChanged.
 */
#define TP_TYPE_PROPERTY_VALUE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_VALUE, \
      G_TYPE_INVALID))
/**
 * TP_TYPE_PROPERTY_VALUE_LIST
 *
 * The GType of the return from GetProperties and the parameter to
 * PropertiesChanged (i.e. a GPtrArray of structures of type
 * TP_TYPE_PROPERTY_VALUE_STRUCT), corresponding to D-Bus signature 'a(uv)'.
 */
#define TP_TYPE_PROPERTY_VALUE_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_VALUE_STRUCT))

/**
 * TP_TYPE_PROPERTY_FLAGS_STRUCT:
 *
 * The GType of the structures with D-Bus signature '(uu)' emitted in
 * PropertyFlagsChanged.
 */
#define TP_TYPE_PROPERTY_FLAGS_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))
/**
 * TP_TYPE_PROPERTY_FLAGS_LIST
 *
 * The GType of the parameter to PropertyFlagsChanged (i.e. a GPtrArray of
 * structures of type TP_TYPE_PROPERTY_FLAGS_STRUCT), corresponding to
 * D-Bus signature 'a(uu)'.
 */
#define TP_TYPE_PROPERTY_FLAGS_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_PROPERTY_FLAGS_STRUCT))

GQuark tp_properties_mixin_class_get_offset_quark (void);
GQuark tp_properties_mixin_get_offset_quark (void);

void tp_properties_mixin_class_init (GObjectClass *obj_cls, glong offset,
    const TpPropertySignature *signatures, guint num_properties,
    TpPropertiesSetFunc set_func);

void tp_properties_mixin_init (GObject *obj, glong offset);
void tp_properties_mixin_finalize (GObject *obj);

gboolean tp_properties_mixin_list_properties (GObject *obj, GPtrArray **ret,
    GError **error);
gboolean tp_properties_mixin_get_properties (GObject *obj,
    const GArray *properties, GPtrArray **ret, GError **error);
void tp_properties_mixin_set_properties (GObject *obj,
    const GPtrArray *properties, DBusGMethodInvocation *context);

gboolean tp_properties_mixin_has_property (GObject *obj, const gchar *name,
    guint *property);

gboolean tp_properties_context_has (TpPropertiesContext *ctx, guint property);
gboolean tp_properties_context_has_other_than (TpPropertiesContext *ctx,
    guint property);
const GValue *tp_properties_context_get (TpPropertiesContext *ctx,
    guint property);
guint tp_properties_context_get_value_count (TpPropertiesContext *ctx);
void tp_properties_context_remove (TpPropertiesContext *ctx, guint property);
void tp_properties_context_return (TpPropertiesContext *ctx, GError *error);
gboolean tp_properties_context_return_if_done (TpPropertiesContext *ctx);

void tp_properties_mixin_change_value (GObject *obj, guint prop_id,
    const GValue *new_value, TpIntSet *props);
void tp_properties_mixin_change_flags (GObject *obj, guint prop_id,
    TpPropertyFlags add, TpPropertyFlags remove, TpIntSet *props);
void tp_properties_mixin_emit_changed (GObject *obj, const TpIntSet *props);
void tp_properties_mixin_emit_flags (GObject *obj, const TpIntSet *props);

gboolean tp_properties_mixin_is_readable (GObject *obj, guint prop_id);
gboolean tp_properties_mixin_is_writable (GObject *obj, guint prop_id);

void tp_properties_mixin_iface_init (gpointer g_iface, gpointer iface_data);

G_END_DECLS

#endif /* #ifndef __TP_PROPERTIES_MIXIN_H__ */
