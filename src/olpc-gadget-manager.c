/*
 * olpc-gadget-manager - ChannelManager for Gadget views
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

#include "config.h"
#include "olpc-gadget-manager.h"

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_OLPC

#include <extensions/extensions.h>

#include "caps-channel-manager.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "olpc-activity.h"
#include "olpc-activity-view.h"
#include "olpc-buddy-view.h"
#include "util.h"


static void channel_manager_iface_init (gpointer, gpointer);


G_DEFINE_TYPE_WITH_CODE (GabbleOlpcGadgetManager, gabble_olpc_gadget_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};


struct _GabbleOlpcGadgetManagerPrivate
{
  GabbleConnection *conn;

  guint next_view_number;
  GHashTable *channels;

  gboolean dispose_has_run;
};


static void
gabble_olpc_gadget_manager_init (GabbleOlpcGadgetManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
        GABBLE_TYPE_OLPC_GADGET_MANAGER, GabbleOlpcGadgetManagerPrivate);

  /* view id guint => GabbleOlpcView */
  self->priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);
}


static void
gabble_olpc_gadget_manager_close_all (GabbleOlpcGadgetManager *self)
{
  DEBUG ("%p", self);

  /* Use a temporary variable because we don't want
   * olpc_gadget_channel_closed_cb to remove the channel from the hash table a
   * second time */
  if (self->priv->channels != NULL)
    {
      GHashTable *tmp = self->priv->channels;
      self->priv->channels = NULL;
      g_hash_table_destroy (tmp);
    }
}

static void
gabble_olpc_gadget_manager_dispose (GObject *object)
{
  GabbleOlpcGadgetManager *self = GABBLE_OLPC_GADGET_MANAGER (object);

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("running");
  self->priv->dispose_has_run = TRUE;

  gabble_olpc_gadget_manager_close_all (self);
  g_assert (self->priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_olpc_gadget_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_olpc_gadget_manager_parent_class)->dispose (object);
}


static void
gabble_olpc_gadget_manager_get_property (GObject *object,
                                         guint property_id,
                                         GValue *value,
                                         GParamSpec *pspec)
{
  GabbleOlpcGadgetManager *self = GABBLE_OLPC_GADGET_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->conn);
      break;
    }
}


static void
gabble_olpc_gadget_manager_set_property (GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
  GabbleOlpcGadgetManager *self = GABBLE_OLPC_GADGET_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_assert (self->priv->conn == NULL);
      self->priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gabble_olpc_gadget_manager_class_init (GabbleOlpcGadgetManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (GabbleOlpcGadgetManagerPrivate));

  object_class->dispose = gabble_olpc_gadget_manager_dispose;

  object_class->get_property = gabble_olpc_gadget_manager_get_property;
  object_class->set_property = gabble_olpc_gadget_manager_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this olpc-gadget manager.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}


static void
gabble_olpc_gadget_manager_foreach_channel (TpChannelManager *manager,
                                            TpExportableChannelFunc foreach,
                                            gpointer user_data)
{
  GabbleOlpcGadgetManager *self = GABBLE_OLPC_GADGET_MANAGER (manager);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->priv->channels);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (value);

      foreach (channel, user_data);
    }
}


static const gchar * const olpc_gadget_channel_view_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    NULL
};

static const gchar * const olpc_gadget_channel_buddy_view_allowed_properties[] =
{
    GABBLE_IFACE_OLPC_CHANNEL_INTERFACE_VIEW ".MaxSize",
    GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW ".Properties",
    GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW ".Alias",
    NULL
};

static const gchar * const olpc_gadget_channel_activity_view_allowed_properties[] =
{
    GABBLE_IFACE_OLPC_CHANNEL_INTERFACE_VIEW ".MaxSize",
    GABBLE_IFACE_OLPC_CHANNEL_TYPE_ACTIVITY_VIEW ".Properties",
    GABBLE_IFACE_OLPC_CHANNEL_TYPE_ACTIVITY_VIEW ".Participants",
    NULL
};

static void
gabble_olpc_gadget_manager_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table;
  GValue *value;

  /* BuddyView */
  table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

  func (manager, table, olpc_gadget_channel_buddy_view_allowed_properties,
      user_data);

  g_hash_table_destroy (table);

  /* ActivityView */
  table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value,
      GABBLE_IFACE_OLPC_CHANNEL_TYPE_ACTIVITY_VIEW);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

  func (manager, table, olpc_gadget_channel_activity_view_allowed_properties,
      user_data);

  g_hash_table_destroy (table);
}

static gboolean
remove_channel_foreach (gpointer key,
                        gpointer value,
                        gpointer channel)
{
  return value == channel;
}

static void
olpc_gadget_channel_closed_cb (GabbleOlpcView *channel,
                               gpointer user_data)
{
  GabbleOlpcGadgetManager *self = GABBLE_OLPC_GADGET_MANAGER (user_data);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (channel));

  if (self->priv->channels != NULL)
    {
      g_hash_table_foreach_remove (self->priv->channels, remove_channel_foreach,
          channel);
    }
}

static GabbleOlpcView *
create_buddy_view_channel (GabbleOlpcGadgetManager *self,
                           GHashTable *request_properties,
                           GError **error)
{
  TpBaseConnection *conn = (TpBaseConnection *) self->priv->conn;
  GabbleOlpcView *channel;
  guint max_size;
  gboolean valid;
  gchar *object_path;
  const gchar *alias;
  GHashTable *properties;

  if (self->priv->conn->olpc_gadget_buddy == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Gadget is not available");
      return NULL;
    }

  if (tp_asv_get_uint32 (request_properties,
       TP_IFACE_CHANNEL ".TargetHandleType", NULL) != 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Views channels can't have a target handle");
      return NULL;
    }

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          olpc_gadget_channel_view_fixed_properties,
          olpc_gadget_channel_buddy_view_allowed_properties,
          error))
    return NULL;

  max_size = tp_asv_get_uint32 (request_properties,
      GABBLE_IFACE_OLPC_CHANNEL_INTERFACE_VIEW ".MaxSize", &valid);
  if (!valid)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "MaxSize property is mandatory");
      return NULL;
    }

  if (max_size == 0)
    {

      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "max have to be greater than 0");
      return NULL;
    }

  properties = tp_asv_get_boxed (request_properties,
      GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW ".Properties",
      TP_HASH_TYPE_STRING_VARIANT_MAP);

  alias = tp_asv_get_string (request_properties,
      GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW ".Alias");

  object_path = g_strdup_printf ("%s/OlpcBuddyViewChannel%u", conn->object_path,
      self->priv->next_view_number++);

  channel = GABBLE_OLPC_VIEW (gabble_olpc_buddy_view_new (self->priv->conn,
        object_path, self->priv->next_view_number, max_size, properties,
        alias));

  g_free (object_path);

  return channel;
}

static GabbleOlpcView *
create_activity_view_channel (GabbleOlpcGadgetManager *self,
                              GHashTable *request_properties,
                              GError **error)
{
  TpBaseConnection *conn = (TpBaseConnection *) self->priv->conn;
  GabbleOlpcView *channel;
  guint max_size;
  gboolean valid;
  gchar *object_path;
  GHashTable *properties;
  GArray *participants;

  if (self->priv->conn->olpc_gadget_activity == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Gadget is not available");
      return NULL;
    }

  if (tp_asv_get_uint32 (request_properties,
       TP_IFACE_CHANNEL ".TargetHandleType", NULL) != 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Views channels can't have a target handle");
      return NULL;
    }

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          olpc_gadget_channel_view_fixed_properties,
          olpc_gadget_channel_activity_view_allowed_properties,
          error))
    return NULL;

  max_size = tp_asv_get_uint32 (request_properties,
      GABBLE_IFACE_OLPC_CHANNEL_INTERFACE_VIEW ".MaxSize", &valid);
  if (!valid)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "MaxSize property is mandatory");
      return NULL;
    }

  if (max_size == 0)
    {

      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "max have to be greater than 0");
      return NULL;
    }

  properties = tp_asv_get_boxed (request_properties,
      GABBLE_IFACE_OLPC_CHANNEL_TYPE_ACTIVITY_VIEW ".Properties",
      TP_HASH_TYPE_STRING_VARIANT_MAP);

  participants = tp_asv_get_boxed (request_properties,
      GABBLE_IFACE_OLPC_CHANNEL_TYPE_ACTIVITY_VIEW ".Participants",
      GABBLE_ARRAY_TYPE_HANDLE);

  object_path = g_strdup_printf ("%s/OlpcActivityViewChannel%u",
      conn->object_path, self->priv->next_view_number++);

  channel = GABBLE_OLPC_VIEW (gabble_olpc_activity_view_new (self->priv->conn,
        object_path, self->priv->next_view_number, max_size, properties,
        participants));

  g_free (object_path);

  return channel;
}

static gboolean
gabble_olpc_gadget_manager_handle_request (TpChannelManager *manager,
                                           gpointer request_token,
                                           GHashTable *request_properties)
{
  GabbleOlpcGadgetManager *self = GABBLE_OLPC_GADGET_MANAGER (manager);
  GabbleOlpcView *channel = NULL;
  GError *error = NULL;
  GSList *request_tokens;

  if (!tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        GABBLE_IFACE_OLPC_CHANNEL_TYPE_BUDDY_VIEW))
    {
      channel = create_buddy_view_channel (self, request_properties, &error);
    }
  else if (!tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        GABBLE_IFACE_OLPC_CHANNEL_TYPE_ACTIVITY_VIEW))
    {
      channel = create_activity_view_channel (self, request_properties, &error);
    }
  else
    {
      return FALSE;
    }

  if (channel == NULL)
    {
      /* Something went wrong */
      tp_channel_manager_emit_request_failed (self, request_token,
          error->domain, error->code, error->message);
      g_error_free (error);
      return TRUE;
    }

  if (!gabble_olpc_view_send_request (channel, &error))
    {
      DEBUG ("view_send_request failed: %s", error->message);
      tp_channel_manager_emit_request_failed (self, request_token,
          error->domain, error->code, error->message);
      g_error_free (error);
      return TRUE;
    }

  g_signal_connect (channel, "closed",
      (GCallback) olpc_gadget_channel_closed_cb, self);
  g_hash_table_insert (self->priv->channels,
      GUINT_TO_POINTER (self->priv->next_view_number), channel);

  request_tokens = g_slist_prepend (NULL, request_token);
  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (channel), request_tokens);
  g_slist_free (request_tokens);

  return TRUE;
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_olpc_gadget_manager_foreach_channel;
  iface->foreach_channel_class = gabble_olpc_gadget_manager_foreach_channel_class;
  iface->request_channel = gabble_olpc_gadget_manager_handle_request;
  iface->create_channel = gabble_olpc_gadget_manager_handle_request;
  iface->ensure_channel = gabble_olpc_gadget_manager_handle_request;
}

static gboolean
find_view_having_properties_for_buddy (gpointer id,
                                       gpointer value,
                                       gpointer buddy)
{
  GabbleOlpcView *view = GABBLE_OLPC_VIEW (value);
  TpHandle handle = GPOINTER_TO_UINT (buddy);

  return gabble_olpc_view_get_buddy_properties (view, handle) != NULL;
}

GHashTable *
gabble_olpc_gadget_manager_find_buddy_properties (
    GabbleOlpcGadgetManager *self,
    TpHandle buddy)
{
  GabbleOlpcView *view;

  view = g_hash_table_find (self->priv->channels,
      find_view_having_properties_for_buddy, GUINT_TO_POINTER (buddy));
  if (view == NULL)
    return NULL;

  return gabble_olpc_view_get_buddy_properties (view, buddy);
}

GabbleOlpcView *
gabble_olpc_gadget_manager_get_view (GabbleOlpcGadgetManager *self,
                                     guint id)
{
  return g_hash_table_lookup (self->priv->channels, GUINT_TO_POINTER (id));
}

struct find_activities_of_buddy_ctx
{
  TpHandle buddy;
  GHashTable *activities;
};

static void
find_activities_of_buddy (TpHandle contact,
                          GabbleOlpcView *view,
                          struct find_activities_of_buddy_ctx *ctx)
{
  GPtrArray *act;
  guint i;

  act = gabble_olpc_view_get_buddy_activities (view, ctx->buddy);

  for (i = 0; i < act->len; i++)
    {
      GabbleOlpcActivity *activity;

      activity = g_ptr_array_index (act, i);
      g_hash_table_insert (ctx->activities, GUINT_TO_POINTER (activity->room),
          activity);
    }

  g_ptr_array_free (act, TRUE);
}

static void
copy_activity_to_array (TpHandle room,
                        GabbleOlpcActivity *activity,
                        GPtrArray *activities)
{
  GValue gvalue = { 0 };

  if (activity->id == NULL)
    {
      DEBUG ("... activity #%u has no ID, skipping", room);
      return;
    }

  g_value_init (&gvalue, GABBLE_STRUCT_TYPE_ACTIVITY);
  g_value_take_boxed (&gvalue, dbus_g_type_specialized_construct
      (GABBLE_STRUCT_TYPE_ACTIVITY));
  dbus_g_type_struct_set (&gvalue,
      0, activity->id,
      1, activity->room,
      G_MAXUINT);
  DEBUG ("... activity #%u (ID %s)",
      activity->room, activity->id);
  g_ptr_array_add (activities, g_value_get_boxed (&gvalue));
}

GPtrArray *
gabble_olpc_gadget_manager_find_buddy_activities (GabbleOlpcGadgetManager *self,
                                                  TpHandle contact)
{
  GPtrArray *result;
  struct find_activities_of_buddy_ctx ctx;

  result = g_ptr_array_new ();

  /* We use a hash table first so we won't add twice the same activity */
  ctx.activities = g_hash_table_new (g_direct_hash, g_direct_equal);
  ctx.buddy = contact;

  g_hash_table_foreach (self->priv->channels, (GHFunc) find_activities_of_buddy,
      &ctx);

  /* Now compute the result array using the hash table */
  g_hash_table_foreach (ctx.activities, (GHFunc) copy_activity_to_array,
      result);

  g_hash_table_destroy (ctx.activities);

  return result;
}

void
gabble_olpc_gadget_manager_close_all_views (GabbleOlpcGadgetManager *self)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->priv->channels);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      GabbleOlpcView *view = GABBLE_OLPC_VIEW (value);

      /* disconnect the signal as we can't modify the hash table
       * while we are iterating over it */
      g_signal_handlers_disconnect_by_func (view,
          G_CALLBACK (olpc_gadget_channel_closed_cb), self);

      gabble_olpc_view_close (view);

      /* remove the channel from the hash table */
      g_hash_table_iter_remove (&iter);
    }
}
