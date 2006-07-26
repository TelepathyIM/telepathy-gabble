/*
 * properties-mixin.c - Source for GabblePropertiesMixin
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_FLAG GABBLE_DEBUG_PROPERTIES

#include "ansi.h"
#include "debug.h"
#include "properties-mixin.h"
#include "properties-mixin-signals-marshal.h"
#include "telepathy-errors.h"

struct _GabblePropertiesContext {
    GabblePropertiesMixinClass *mixin_cls;
    GabblePropertiesMixin *mixin;

    DBusGMethodInvocation *dbus_ctx;
    guint32 remaining;
    GValue **values;
};

struct _GabblePropertiesMixinPrivate {
    GObject *object;
    GabblePropertiesContext context;
};

/**
 * gabble_properties_mixin_class_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
gabble_properties_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("PropertiesMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * gabble_properties_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
gabble_properties_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("PropertiesMixinOffsetQuark");
  return offset_quark;
}

void gabble_properties_mixin_class_init (GObjectClass *obj_cls,
                                         glong offset,
                                         const GabblePropertySignature *signatures,
                                         guint num_properties,
                                         GabblePropertiesSetFunc set_func)
{
  GabblePropertiesMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
                    GABBLE_PROPERTIES_MIXIN_CLASS_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (obj_cls);

  mixin_cls->signatures = signatures;
  mixin_cls->num_props = num_properties;

  mixin_cls->set_properties = set_func;

  mixin_cls->properties_changed_signal_id =
    g_signal_new ("properties-changed",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  properties_mixin_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_VALUE, G_TYPE_INVALID)))));

  mixin_cls->property_flags_changed_signal_id =
    g_signal_new ("property-flags-changed",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  properties_mixin_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID)))));
}

void gabble_properties_mixin_init (GObject *obj, glong offset)
{
  GabblePropertiesMixinClass *mixin_cls;
  GabblePropertiesMixin *mixin;
  GabblePropertiesContext *ctx;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    GABBLE_PROPERTIES_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = GABBLE_PROPERTIES_MIXIN (obj);
  mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));

  mixin->properties = g_new0 (GabbleProperty, mixin_cls->num_props);

  mixin->priv = g_new0 (GabblePropertiesMixinPrivate, 1);
  mixin->priv->object = obj;

  ctx = &mixin->priv->context;
  ctx->mixin_cls = mixin_cls;
  ctx->mixin = mixin;
  ctx->values = g_new0 (GValue *, mixin_cls->num_props);
}

void gabble_properties_mixin_finalize (GObject *obj)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GabblePropertiesContext *ctx = &mixin->priv->context;
  guint i;

  for (i = 0; i < mixin_cls->num_props; i++)
    {
      GabbleProperty *prop = &mixin->properties[i];

      if (prop->value)
        {
          g_value_unset (prop->value);
          g_free (prop->value);
        }

      if (ctx->values[i])
        {
          g_value_unset (ctx->values[i]);
        }
    }

  g_free (ctx->values);

  g_free (mixin->priv);

  g_free (mixin->properties);
}

gboolean
gabble_properties_mixin_list_properties (GObject *obj, GPtrArray **ret, GError **error)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  guint i;

  *ret = g_ptr_array_sized_new (mixin_cls->num_props);

  for (i = 0; i < mixin_cls->num_props; i++)
    {
      const GabblePropertySignature *sig = &mixin_cls->signatures[i];
      GabbleProperty *prop = &mixin->properties[i];
      const gchar *dbus_sig;
      GValue val = { 0, };

      switch (sig->type) {
        case G_TYPE_BOOLEAN:
          dbus_sig = "b";
          break;
        case G_TYPE_INT:
          dbus_sig = "i";
          break;
        case G_TYPE_UINT:
          dbus_sig = "u";
          break;
        case G_TYPE_STRING:
          dbus_sig = "s";
          break;
        default:
          g_assert_not_reached ();
      };

      g_value_init (&val, TP_TYPE_PROPERTY_INFO_STRUCT);
      g_value_take_boxed (&val,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_INFO_STRUCT));

      dbus_g_type_struct_set (&val,
          0, i,
          1, sig->name,
          2, dbus_sig,
          3, prop->flags,
          G_MAXUINT);

      g_ptr_array_add (*ret, g_value_get_boxed (&val));
    }

  return TRUE;
}

gboolean
gabble_properties_mixin_get_properties (GObject *obj, const GArray *properties, GPtrArray **ret, GError **error)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  guint i;

  /* Check input property identifiers */
  for (i = 0; i < properties->len; i++)
    {
      guint prop_id = g_array_index (properties, guint, i);

      /* Valid? */
      if (prop_id >= mixin_cls->num_props)
        {
          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                "invalid property identifier %d", prop_id);

          return FALSE;
        }

      /* Permitted? */
      if (!(mixin->properties[prop_id].flags & TP_PROPERTY_FLAG_READ))
        {
          *error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                                "permission denied for property identifier %d", prop_id);

          return FALSE;
        }
    }

  /* If we got this far, return the actual values */
  *ret = g_ptr_array_sized_new (properties->len);

  for (i = 0; i < properties->len; i++)
    {
      guint prop_id = g_array_index (properties, guint, i);
      GValue val_struct = { 0, };

      /* id/value struct */
      g_value_init (&val_struct, TP_TYPE_PROPERTY_VALUE_STRUCT);
      g_value_take_boxed (&val_struct,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_VALUE_STRUCT));

      dbus_g_type_struct_set (&val_struct,
          0, prop_id,
          1, mixin->properties[prop_id].value,
          G_MAXUINT);

      g_ptr_array_add (*ret, g_value_get_boxed (&val_struct));
    }

  return TRUE;
}

gboolean
gabble_properties_mixin_set_properties (GObject *obj, const GPtrArray *properties, DBusGMethodInvocation *context)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GabblePropertiesContext *ctx = &mixin->priv->context;
  gboolean result;
  GError *error;
  guint i;

  /* Is another SetProperties request already in progress? */
  if (ctx->dbus_ctx)
    {
      error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                           "A SetProperties request is already in progress");
      goto ERROR;
    }

  ctx->dbus_ctx = context;
  result = TRUE;
  error = NULL;

  /* Check input property identifiers */
  for (i = 0; i < properties->len; i++)
    {
      GValue val_struct = { 0, };
      guint prop_id;
      GValue *prop_val;

      g_value_init (&val_struct, TP_TYPE_PROPERTY_VALUE_STRUCT);
      g_value_set_static_boxed (&val_struct, g_ptr_array_index (properties, i));

      dbus_g_type_struct_get (&val_struct,
          0, &prop_id,
          1, &prop_val,
          G_MAXUINT);

      /* Valid? */
      if (prop_id >= mixin_cls->num_props)
        {
          g_value_unset (prop_val);

          error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                               "invalid property identifier %d", prop_id);
          goto ERROR;
        }

      /* Store the value in the context */
      ctx->remaining |= 1 << prop_id;
      ctx->values[prop_id] = prop_val;

      /* Permitted? */
      if (!(mixin->properties[prop_id].flags & TP_PROPERTY_FLAG_WRITE))
        {
          error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                               "permission denied for property identifier %d", prop_id);
          goto ERROR;
        }

      /* Compatible type? */
      if (!g_value_type_compatible (G_VALUE_TYPE (prop_val),
                                    mixin_cls->signatures[prop_id].type))
        {
          error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                               "incompatible value type for property identifier %d",
                               prop_id);
          goto ERROR;
        }
    }

  if (mixin_cls->set_properties)
    {
      if (mixin_cls->set_properties (obj, ctx, &error))
        goto OUT;
    }
  else
    {
      gabble_properties_context_return (ctx, NULL);
      goto OUT;
    }

ERROR:
  gabble_properties_context_return (ctx, error);
  result = FALSE;

OUT:
  return result;
}

gboolean
gabble_properties_mixin_has_property (GObject *obj, const gchar *name,
                                      guint *property)
{
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  guint i;

  for (i = 0; i < mixin_cls->num_props; i++)
    {
      if (strcmp (mixin_cls->signatures[i].name, name) == 0)
        {
          if (property)
            *property = i;

          return TRUE;
        }
    }

  return FALSE;
}

gboolean
gabble_properties_context_has (GabblePropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  return (ctx->values[property] != NULL);
}

gboolean
gabble_properties_context_has_other_than (GabblePropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  return ((ctx->remaining & ~(1 << property)) != 0);
}

const GValue *
gabble_properties_context_get (GabblePropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  return ctx->values[property];
}

guint
gabble_properties_context_get_value_count (GabblePropertiesContext *ctx)
{
  guint i, n;

  n = 0;
  for (i = 0; i < ctx->mixin_cls->num_props; i++)
    {
      if (ctx->values[i])
        n++;
    }

  return n;
}

void
gabble_properties_context_remove (GabblePropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  ctx->remaining &= ~(1 << property);
}

void
gabble_properties_context_return (GabblePropertiesContext *ctx, GError *error)
{
  GObject *obj = ctx->mixin->priv->object;
  GArray *changed_props_val, *changed_props_flags;
  int i;

  DEBUG_FUNC ("%s", (error) ? "failure" : "success");

  changed_props_val = changed_props_flags = NULL;

  for (i = 0; i < ctx->mixin_cls->num_props; i++)
    {
      if (ctx->values[i])
        {
          if (!error)
            {
              gabble_properties_mixin_change_value (obj, i, ctx->values[i],
                                                    &changed_props_val);

              gabble_properties_mixin_change_flags (obj, i,
                  TP_PROPERTY_FLAG_READ, 0, &changed_props_flags);
            }

          g_value_unset (ctx->values[i]);
          ctx->values[i] = NULL;
        }
    }

  if (!error)
    {
      gabble_properties_mixin_emit_changed (obj, &changed_props_val);
      gabble_properties_mixin_emit_flags (obj, &changed_props_flags);

      dbus_g_method_return (ctx->dbus_ctx);
    }
  else
    {
      dbus_g_method_return_error (ctx->dbus_ctx, error);
      g_error_free (error);
    }

  ctx->dbus_ctx = NULL;
  ctx->remaining = 0;
}

gboolean
gabble_properties_context_return_if_done (GabblePropertiesContext *ctx)
{
  if (ctx->remaining == 0)
    {
      gabble_properties_context_return (ctx, NULL);
      return TRUE;
    }

  return FALSE;
}

#define RPTS_APPEND_FLAG_IF_SET(flag) \
  if (flags & flag) \
    { \
      if (i++ > 0) \
        g_string_append (str, "|"); \
      g_string_append (str, #flag + 17); \
    }

static gchar *
property_flags_to_string (TpPropertyFlags flags)
{
  gint i = 0;
  GString *str;

  str = g_string_new ("[" ANSI_BOLD_OFF);

  RPTS_APPEND_FLAG_IF_SET (TP_PROPERTY_FLAG_READ);
  RPTS_APPEND_FLAG_IF_SET (TP_PROPERTY_FLAG_WRITE);

  g_string_append (str, ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

static gboolean
values_are_equal (const GValue *v1, const GValue *v2)
{
  GType type = G_VALUE_TYPE (v1);
  const gchar *s1, *s2;

  switch (type) {
    case G_TYPE_BOOLEAN:
      return (g_value_get_boolean (v1) == g_value_get_boolean (v2));

    case G_TYPE_STRING:
      s1 = g_value_get_string (v1);
      s2 = g_value_get_string (v2);

      /* are they both NULL? */
      if (s1 == s2)
        return TRUE;

      /* is one of them NULL? */
      if (s1 == NULL || s2 == NULL)
        return FALSE;

      return (strcmp (s1, s2) == 0);

    case G_TYPE_UINT:
      return (g_value_get_uint (v1) == g_value_get_uint (v2));

    case G_TYPE_INT:
      return (g_value_get_int (v1) == g_value_get_int (v2));
  }

  return FALSE;
}

void
gabble_properties_mixin_change_value (GObject *obj, guint prop_id,
                                      const GValue *new_value,
                                      GArray **props)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GabbleProperty *prop;

  g_assert (prop_id < mixin_cls->num_props);

  prop = &mixin->properties[prop_id];

  if (prop->value)
    {
      if (values_are_equal (prop->value, new_value))
        return;
    }
  else
    {
      prop->value = g_new0 (GValue, 1);
      g_value_init (prop->value, mixin_cls->signatures[prop_id].type);
    }

  g_value_copy (new_value, prop->value);

  if (props)
    {
      guint i;

      if (*props == NULL)
        {
          *props = g_array_sized_new (FALSE, FALSE, sizeof (guint),
                                      mixin_cls->num_props);
        }

      for (i = 0; i < (*props)->len; i++)
        {
          if (g_array_index (*props, guint, i) == prop_id)
            return;
        }

      g_array_append_val (*props, prop_id);
    }
  else
    {
      GArray *changed_props = g_array_sized_new (FALSE, FALSE,
                                                 sizeof (guint), 1);
      g_array_append_val (changed_props, prop_id);

      gabble_properties_mixin_emit_changed (obj, &changed_props);
    }
}

void
gabble_properties_mixin_change_flags (GObject *obj, guint prop_id,
                                      TpPropertyFlags add,
                                      TpPropertyFlags remove,
                                      GArray **props)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GabbleProperty *prop;
  guint prev_flags;

  g_assert (prop_id < mixin_cls->num_props);

  prop = &mixin->properties[prop_id];

  prev_flags = prop->flags;

  prop->flags |= add;
  prop->flags &= ~remove;

  if (prop->flags == prev_flags)
    return;

  if (props)
    {
      gint i;

      if (*props == NULL)
        {
          *props = g_array_sized_new (FALSE, FALSE, sizeof (guint),
                                      mixin_cls->num_props);
        }

      for (i = 0; i < (*props)->len; i++)
        {
          if (g_array_index (*props, guint, i) == prop_id)
            return;
        }

      g_array_append_val (*props, prop_id);
    }
  else
    {
      GArray *changed_props = g_array_sized_new (FALSE, FALSE,
                                                 sizeof (guint), 1);
      g_array_append_val (changed_props, prop_id);

      gabble_properties_mixin_emit_flags (obj, &changed_props);
    }
}

void
gabble_properties_mixin_emit_changed (GObject *obj, GArray **props)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GPtrArray *prop_arr;
  GValue prop_list = { 0, };
  guint i;

  if (*props == NULL)
    return;

  if ((*props)->len == 0)
    return;

  prop_arr = g_ptr_array_sized_new ((*props)->len);

  printf (ANSI_BOLD_ON ANSI_FG_CYAN
          "%s: emitting properties changed for propert%s:\n",
          G_STRFUNC, ((*props)->len > 1) ? "ies" : "y");

  for (i = 0; i < (*props)->len; i++)
    {
      GValue prop_val = { 0, };
      guint prop_id = g_array_index (*props, guint, i);

      g_value_init (&prop_val, TP_TYPE_PROPERTY_VALUE_STRUCT);
      g_value_take_boxed (&prop_val,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_VALUE_STRUCT));

      dbus_g_type_struct_set (&prop_val,
          0, prop_id,
          1, mixin->properties[prop_id].value,
          G_MAXUINT);

      g_ptr_array_add (prop_arr, g_value_get_boxed (&prop_val));

      printf ("  %s\n", mixin_cls->signatures[prop_id].name);
    }

  printf (ANSI_RESET);
  fflush (stdout);

  g_signal_emit (obj, mixin_cls->properties_changed_signal_id, 0, prop_arr);

  g_value_init (&prop_list, TP_TYPE_PROPERTY_VALUE_LIST);
  g_value_take_boxed (&prop_list, prop_arr);
  g_value_unset (&prop_list);

  g_array_free (*props, TRUE);
  *props = NULL;
}

void
gabble_properties_mixin_emit_flags (GObject *obj, GArray **props)
{
  GabblePropertiesMixin *mixin = GABBLE_PROPERTIES_MIXIN (obj);
  GabblePropertiesMixinClass *mixin_cls = GABBLE_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GPtrArray *prop_arr;
  GValue prop_list = { 0, };
  guint i;

  if (*props == NULL)
    return;

  if ((*props)->len == 0)
    return;

  prop_arr = g_ptr_array_sized_new ((*props)->len);

  printf (ANSI_BOLD_ON ANSI_FG_WHITE
          "%s: emitting properties flags changed for propert%s:\n",
          G_STRFUNC, ((*props)->len > 1) ? "ies" : "y");

  for (i = 0; i < (*props)->len; i++)
    {
      GValue prop_val = { 0, };
      guint prop_id = g_array_index (*props, guint, i);
      guint prop_flags;
      gchar *str_flags;

      prop_flags = mixin->properties[prop_id].flags;

      g_value_init (&prop_val, TP_TYPE_PROPERTY_FLAGS_STRUCT);
      g_value_take_boxed (&prop_val,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_FLAGS_STRUCT));

      dbus_g_type_struct_set (&prop_val,
          0, prop_id,
          1, prop_flags,
          G_MAXUINT);

      g_ptr_array_add (prop_arr, g_value_get_boxed (&prop_val));

      str_flags = property_flags_to_string (prop_flags);

      printf ("  %s's flags now: %s\n",
              mixin_cls->signatures[prop_id].name, str_flags);

      g_free (str_flags);
    }

  printf (ANSI_RESET);
  fflush (stdout);

  g_signal_emit (obj, mixin_cls->property_flags_changed_signal_id, 0, prop_arr);

  g_value_init (&prop_list, TP_TYPE_PROPERTY_FLAGS_LIST);
  g_value_take_boxed (&prop_list, prop_arr);
  g_value_unset (&prop_list);

  g_array_free (*props, TRUE);
  *props = NULL;
}

