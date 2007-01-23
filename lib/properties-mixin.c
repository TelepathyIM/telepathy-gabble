/*
 * properties-mixin.c - Source for TpPropertiesMixin
 * Copyright (C) 2006-2007 Collabora Ltd.
 * Copyright (C) 2006-2007 Nokia Corporation
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

#include "telepathy-glib/properties-mixin.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_FLAG TP_DEBUG_PROPERTIES

#include <telepathy-glib/debug-ansi.h>
#include "debug.h"
#include <telepathy-glib/errors.h>

struct _TpPropertiesContext {
    TpPropertiesMixinClass *mixin_cls;
    TpPropertiesMixin *mixin;

    DBusGMethodInvocation *dbus_ctx;
    guint32 remaining;
    GValue **values;
};

struct _TpPropertiesMixinPrivate {
    GObject *object;
    TpPropertiesContext context;
};

/**
 * tp_properties_mixin_class_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
tp_properties_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TpPropertiesMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * tp_properties_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
tp_properties_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TpPropertiesMixinOffsetQuark");
  return offset_quark;
}

void tp_properties_mixin_class_init (GObjectClass *obj_cls,
                                         glong offset,
                                         const TpPropertySignature *signatures,
                                         guint num_properties,
                                         TpPropertiesSetFunc set_func)
{
  TpPropertiesMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
                    TP_PROPERTIES_MIXIN_CLASS_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin_cls = TP_PROPERTIES_MIXIN_CLASS (obj_cls);

  mixin_cls->signatures = signatures;
  mixin_cls->num_props = num_properties;

  mixin_cls->set_properties = set_func;
}

void tp_properties_mixin_init (GObject *obj, glong offset)
{
  TpPropertiesMixinClass *mixin_cls;
  TpPropertiesMixin *mixin;
  TpPropertiesContext *ctx;

  g_assert (G_IS_OBJECT (obj));

  g_assert (TP_IS_SVC_PROPERTIES_INTERFACE (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    TP_PROPERTIES_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = TP_PROPERTIES_MIXIN (obj);
  mixin_cls = TP_PROPERTIES_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));

  mixin->properties = g_new0 (TpProperty, mixin_cls->num_props);

  mixin->priv = g_new0 (TpPropertiesMixinPrivate, 1);
  mixin->priv->object = obj;

  ctx = &mixin->priv->context;
  ctx->mixin_cls = mixin_cls;
  ctx->mixin = mixin;
  ctx->values = g_new0 (GValue *, mixin_cls->num_props);
}

void tp_properties_mixin_finalize (GObject *obj)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  TpPropertiesContext *ctx = &mixin->priv->context;
  guint i;

  for (i = 0; i < mixin_cls->num_props; i++)
    {
      TpProperty *prop = &mixin->properties[i];

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
tp_properties_mixin_list_properties (GObject *obj, GPtrArray **ret, GError **error)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  guint i;

  *ret = g_ptr_array_sized_new (mixin_cls->num_props);

  for (i = 0; i < mixin_cls->num_props; i++)
    {
      const TpPropertySignature *sig = &mixin_cls->signatures[i];
      TpProperty *prop = &mixin->properties[i];
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
          continue;
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
tp_properties_mixin_get_properties (GObject *obj, const GArray *properties, GPtrArray **ret, GError **error)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  guint i;

  /* Check input property identifiers */
  for (i = 0; i < properties->len; i++)
    {
      guint prop_id = g_array_index (properties, guint, i);

      /* Valid? */
      if (prop_id >= mixin_cls->num_props)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "invalid property identifier %d", prop_id);

          return FALSE;
        }

      /* Permitted? */
      if (!tp_properties_mixin_is_readable (obj, prop_id))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
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

void
tp_properties_mixin_set_properties (GObject *obj,
                                        const GPtrArray *properties,
                                        DBusGMethodInvocation *context)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  TpPropertiesContext *ctx = &mixin->priv->context;
  GError *error = NULL;
  guint i;

  /* Is another SetProperties request already in progress? */
  if (ctx->dbus_ctx)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                           "A SetProperties request is already in progress");
      goto ERROR;
    }

  ctx->dbus_ctx = context;
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

          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                               "invalid property identifier %d", prop_id);
          goto ERROR;
        }

      /* Store the value in the context */
      ctx->remaining |= 1 << prop_id;
      ctx->values[prop_id] = prop_val;

      /* Permitted? */
      if (!tp_properties_mixin_is_writable (obj, prop_id))
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                               "permission denied for property identifier %d", prop_id);
          goto ERROR;
        }

      /* Compatible type? */
      if (!g_value_type_compatible (G_VALUE_TYPE (prop_val),
                                    mixin_cls->signatures[prop_id].type))
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                               "incompatible value type for property identifier %d",
                               prop_id);
          goto ERROR;
        }
    }

  if (mixin_cls->set_properties)
    {
      if (mixin_cls->set_properties (obj, ctx, &error))
        return;
    }
  else
    {
      tp_properties_context_return (ctx, NULL);
      return;
    }

ERROR:
  tp_properties_context_return (ctx, error);
}

gboolean
tp_properties_mixin_has_property (GObject *obj, const gchar *name,
                                      guint *property)
{
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
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
tp_properties_context_has (TpPropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  return (ctx->values[property] != NULL);
}

gboolean
tp_properties_context_has_other_than (TpPropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  return ((ctx->remaining & ~(1 << property)) != 0);
}

const GValue *
tp_properties_context_get (TpPropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  return ctx->values[property];
}

guint
tp_properties_context_get_value_count (TpPropertiesContext *ctx)
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
tp_properties_context_remove (TpPropertiesContext *ctx, guint property)
{
  g_assert (property < ctx->mixin_cls->num_props);

  ctx->remaining &= ~(1 << property);
}

void
tp_properties_context_return (TpPropertiesContext *ctx, GError *error)
{
  GObject *obj = ctx->mixin->priv->object;
  GArray *changed_props_val, *changed_props_flags;
  guint i;

  DEBUG ("%s", (error) ? "failure" : "success");

  changed_props_val = changed_props_flags = NULL;

  for (i = 0; i < ctx->mixin_cls->num_props; i++)
    {
      if (ctx->values[i])
        {
          if (!error)
            {
              tp_properties_mixin_change_value (obj, i, ctx->values[i],
                                                    &changed_props_val);

              tp_properties_mixin_change_flags (obj, i,
                  TP_PROPERTY_FLAG_READ, 0, &changed_props_flags);
            }

          g_value_unset (ctx->values[i]);
          ctx->values[i] = NULL;
        }
    }

  if (!error)
    {
      tp_properties_mixin_emit_changed (obj, &changed_props_val);
      tp_properties_mixin_emit_flags (obj, &changed_props_flags);

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
tp_properties_context_return_if_done (TpPropertiesContext *ctx)
{
  if (ctx->remaining == 0)
    {
      tp_properties_context_return (ctx, NULL);
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

  str = g_string_new ("[" TP_ANSI_BOLD_OFF);

  RPTS_APPEND_FLAG_IF_SET (TP_PROPERTY_FLAG_READ);
  RPTS_APPEND_FLAG_IF_SET (TP_PROPERTY_FLAG_WRITE);

  g_string_append (str, TP_ANSI_BOLD_ON "]");

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
tp_properties_mixin_change_value (GObject *obj, guint prop_id,
                                      const GValue *new_value,
                                      GArray **props)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  TpProperty *prop;

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

      tp_properties_mixin_emit_changed (obj, &changed_props);
    }
}

void
tp_properties_mixin_change_flags (GObject *obj, guint prop_id,
                                      TpPropertyFlags add,
                                      TpPropertyFlags remove,
                                      GArray **props)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  TpProperty *prop;
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

      tp_properties_mixin_emit_flags (obj, &changed_props);
    }
}

void
tp_properties_mixin_emit_changed (GObject *obj, GArray **props)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GPtrArray *prop_arr;
  GValue prop_list = { 0, };
  guint i;

  if (*props == NULL)
    return;

  if ((*props)->len == 0)
    return;

  prop_arr = g_ptr_array_sized_new ((*props)->len);

  if (DEBUGGING)
    printf (TP_ANSI_BOLD_ON TP_ANSI_FG_CYAN
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

      if (DEBUGGING)
        printf ("  %s\n", mixin_cls->signatures[prop_id].name);
    }

  if (DEBUGGING)
    {
      printf (TP_ANSI_RESET);
      fflush (stdout);
    }

  tp_svc_properties_interface_emit_properties_changed (
      (TpSvcPropertiesInterface *)obj, prop_arr);

  g_value_init (&prop_list, TP_TYPE_PROPERTY_VALUE_LIST);
  g_value_take_boxed (&prop_list, prop_arr);
  g_value_unset (&prop_list);

  g_array_free (*props, TRUE);
  *props = NULL;
}

void
tp_properties_mixin_emit_flags (GObject *obj, GArray **props)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));
  GPtrArray *prop_arr;
  GValue prop_list = { 0, };
  guint i;

  if (*props == NULL)
    return;

  if ((*props)->len == 0)
    return;

  prop_arr = g_ptr_array_sized_new ((*props)->len);

  if (DEBUGGING)
    printf (TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE
            "%s: emitting properties flags changed for propert%s:\n",
            G_STRFUNC, ((*props)->len > 1) ? "ies" : "y");

  for (i = 0; i < (*props)->len; i++)
    {
      GValue prop_val = { 0, };
      guint prop_id = g_array_index (*props, guint, i);
      guint prop_flags;

      prop_flags = mixin->properties[prop_id].flags;

      g_value_init (&prop_val, TP_TYPE_PROPERTY_FLAGS_STRUCT);
      g_value_take_boxed (&prop_val,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_FLAGS_STRUCT));

      dbus_g_type_struct_set (&prop_val,
          0, prop_id,
          1, prop_flags,
          G_MAXUINT);

      g_ptr_array_add (prop_arr, g_value_get_boxed (&prop_val));

      if (DEBUGGING)
        {
          gchar *str_flags = property_flags_to_string (prop_flags);

          printf ("  %s's flags now: %s\n",
                  mixin_cls->signatures[prop_id].name, str_flags);

          g_free (str_flags);
        }
    }

  if (DEBUGGING)
    {
      printf (TP_ANSI_RESET);
      fflush (stdout);
    }

  tp_svc_properties_interface_emit_property_flags_changed (
      (TpSvcPropertiesInterface *)obj, prop_arr);

  g_value_init (&prop_list, TP_TYPE_PROPERTY_FLAGS_LIST);
  g_value_take_boxed (&prop_list, prop_arr);
  g_value_unset (&prop_list);

  g_array_free (*props, TRUE);
  *props = NULL;
}

gboolean
tp_properties_mixin_is_readable (GObject *obj, guint prop_id)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));

  if (prop_id >= mixin_cls->num_props)
    return FALSE;

  return ((mixin->properties[prop_id].flags & TP_PROPERTY_FLAG_READ) != 0);
}

gboolean
tp_properties_mixin_is_writable (GObject *obj, guint prop_id)
{
  TpPropertiesMixin *mixin = TP_PROPERTIES_MIXIN (obj);
  TpPropertiesMixinClass *mixin_cls = TP_PROPERTIES_MIXIN_CLASS (
                                            G_OBJECT_GET_CLASS (obj));

  if (prop_id >= mixin_cls->num_props)
    return FALSE;

  return ((mixin->properties[prop_id].flags & TP_PROPERTY_FLAG_WRITE) != 0);
}

/**
 * get_properties
 *
 * Implements D-Bus method GetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
get_properties (TpSvcPropertiesInterface *iface,
                const GArray *properties,
                DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error = NULL;
  gboolean ok = tp_properties_mixin_get_properties (G_OBJECT (iface), properties,
      &ret, &error);
  if (!ok)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }
  tp_svc_properties_interface_return_from_get_properties (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}


/**
 * list_properties
 *
 * Implements D-Bus method ListProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
list_properties (TpSvcPropertiesInterface *iface,
                 DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error = NULL;
  gboolean ok = tp_properties_mixin_list_properties (G_OBJECT (iface), &ret,
      &error);
  if (!ok)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
  tp_svc_properties_interface_return_from_list_properties (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}


/**
 * set_properties
 *
 * Implements D-Bus method SetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
set_properties (TpSvcPropertiesInterface *iface,
                const GPtrArray *properties,
                DBusGMethodInvocation *context)
{
  tp_properties_mixin_set_properties (G_OBJECT (iface), properties, context);
}


void
tp_properties_mixin_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcPropertiesInterfaceClass *klass = (TpSvcPropertiesInterfaceClass *)g_iface;

  klass->get_properties = get_properties;
  klass->list_properties = list_properties;
  klass->set_properties = set_properties;
}
