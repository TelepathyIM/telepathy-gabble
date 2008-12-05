/*
 * roomlist-manager - ChannelManager for room lists
 * Copyright (C) 2006-2008 Collabora Ltd.
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
#include "roomlist-manager.h"

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_MUC

#include "caps-channel-manager.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "roomlist-channel.h"
#include "util.h"


static void channel_manager_iface_init (gpointer, gpointer);


G_DEFINE_TYPE_WITH_CODE (GabbleRoomlistManager, gabble_roomlist_manager,
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


struct _GabbleRoomlistManagerPrivate
{
  GabbleConnection *conn;
  gulong status_changed_id;

  guint next_channel_number;
  GPtrArray *channels;

  gboolean dispose_has_run;
};


static void
gabble_roomlist_manager_init (GabbleRoomlistManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
        GABBLE_TYPE_ROOMLIST_MANAGER, GabbleRoomlistManagerPrivate);

  /* In practice we probably won't have more than one room list at a time */
  self->priv->channels = g_ptr_array_sized_new (2);
}


static void
gabble_roomlist_manager_close_all (GabbleRoomlistManager *self)
{
  DEBUG ("%p", self);

  if (self->priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (self->priv->conn,
          self->priv->status_changed_id);
      self->priv->status_changed_id = 0;
    }

  /* Use a temporary variable because we don't want
   * roomlist_channel_closed_cb to remove the channel from the ptr array a
   * second time */
  if (self->priv->channels != NULL)
    {
      GPtrArray *tmp = self->priv->channels;
      guint i;

      self->priv->channels = NULL;

      for (i = 0; i < tmp->len; i++)
        {
          GabbleRoomlistChannel *channel = g_ptr_array_index (tmp, i);

          DEBUG ("Channel's refcount is %u before unref",
              G_OBJECT (channel)->ref_count);

          g_object_unref (channel);
        }

      g_ptr_array_free (tmp, TRUE);
    }
}


static void
gabble_roomlist_manager_dispose (GObject *object)
{
  GabbleRoomlistManager *self = GABBLE_ROOMLIST_MANAGER (object);

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("running");
  self->priv->dispose_has_run = TRUE;

  gabble_roomlist_manager_close_all (self);
  g_assert (self->priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_roomlist_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roomlist_manager_parent_class)->dispose (object);
}


static void
gabble_roomlist_manager_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  GabbleRoomlistManager *self = GABBLE_ROOMLIST_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->conn);
      break;
    }
}


static void
gabble_roomlist_manager_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  GabbleRoomlistManager *self = GABBLE_ROOMLIST_MANAGER (object);

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
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleRoomlistManager *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      break;

    case TP_CONNECTION_STATUS_CONNECTED:
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      DEBUG ("disconnected, closing room lists");
      gabble_roomlist_manager_close_all (self);
      break;

    default:
      g_assert_not_reached ();
    }
}


static void
gabble_roomlist_manager_constructed (GObject *object)
{
  GabbleRoomlistManager *self = GABBLE_ROOMLIST_MANAGER (object);
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (gabble_roomlist_manager_parent_class)->constructed;

  DEBUG ("%p", self);

  if (chain_up != NULL)
    chain_up (object);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}


static void
gabble_roomlist_manager_class_init (GabbleRoomlistManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (GabbleRoomlistManagerPrivate));

  object_class->constructed = gabble_roomlist_manager_constructed;
  object_class->dispose = gabble_roomlist_manager_dispose;

  object_class->get_property = gabble_roomlist_manager_get_property;
  object_class->set_property = gabble_roomlist_manager_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this room-list manager.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}


static void
gabble_roomlist_manager_foreach_channel (TpChannelManager *manager,
                                         TpExportableChannelFunc foreach,
                                         gpointer user_data)
{
  GabbleRoomlistManager *self = GABBLE_ROOMLIST_MANAGER (manager);
  guint i;

  for (i = 0; i < self->priv->channels->len; i++)
    {
      TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (
          g_ptr_array_index (self->priv->channels, i));

      foreach (channel, user_data);
    }
}


static const gchar * const roomlist_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const roomlist_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL_TYPE_ROOM_LIST ".Server",
    NULL
};


static void
gabble_roomlist_manager_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType", value);

  func (manager, table, roomlist_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}


static void
roomlist_channel_closed_cb (GabbleRoomlistChannel *channel,
                            gpointer user_data)
{
  GabbleRoomlistManager *self = GABBLE_ROOMLIST_MANAGER (user_data);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (channel));

  if (self->priv->channels != NULL)
    {
      g_ptr_array_remove (self->priv->channels, channel);
      g_object_unref (channel);
    }
}


static gboolean
gabble_roomlist_manager_handle_request (TpChannelManager *manager,
                                        gpointer request_token,
                                        GHashTable *request_properties,
                                        gboolean require_new)
{
  GabbleRoomlistManager *self = GABBLE_ROOMLIST_MANAGER (manager);
  TpBaseConnection *conn = (TpBaseConnection *) self->priv->conn;
  GabbleRoomlistChannel *channel = NULL;
  GError *error = NULL;
  GSList *request_tokens;
  const gchar *server;
  gchar *object_path;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
       TP_IFACE_CHANNEL ".TargetHandleType", NULL) != 0)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "RoomList channels can't have a target handle");
      goto error;
    }

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          roomlist_channel_fixed_properties,
          roomlist_channel_allowed_properties,
          &error))
    goto error;

  server = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_ROOM_LIST ".Server");

  if (server == NULL || server[0] == '\0')
    {
      server = _gabble_connection_find_conference_server (self->priv->conn);

      if (server == NULL)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Unable to choose a default conference server");
          goto error;
        }
    }

  if (!require_new)
    {
      guint i;

      for (i = 0; i < self->priv->channels->len; i++)
        {
          gchar *its_server;
          gboolean good;

          channel = g_ptr_array_index (self->priv->channels, i);

          g_object_get (channel,
              "conference-server", &its_server,
              NULL);

          good = !tp_strdiff (its_server, server);

          g_free (its_server);

          if (good)
            {
              tp_channel_manager_emit_request_already_satisfied (self,
                  request_token, TP_EXPORTABLE_CHANNEL (channel));
              return TRUE;
            }
        }
    }

  /* no existing channel is suitable - make a new one */

  /* FIXME: next_channel_number can theoretically wrap around, it would
   * be better for the object path to use the channel's pointer value
   * (but that would require that the channel chose it) */
  object_path = g_strdup_printf ("%s/RoomlistChannel%u", conn->object_path,
      self->priv->next_channel_number++);

  channel = _gabble_roomlist_channel_new (self->priv->conn, object_path,
      server);
  g_signal_connect (channel, "closed", (GCallback) roomlist_channel_closed_cb,
      self);
  g_ptr_array_add (self->priv->channels, channel);

  request_tokens = g_slist_prepend (NULL, request_token);
  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (channel), request_tokens);
  g_slist_free (request_tokens);

  g_free (object_path);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
gabble_roomlist_manager_create_channel (TpChannelManager *manager,
                                        gpointer request_token,
                                        GHashTable *request_properties)
{
  return gabble_roomlist_manager_handle_request (manager, request_token,
      request_properties, TRUE);
}


static gboolean
gabble_roomlist_manager_request_channel (TpChannelManager *manager,
                                         gpointer request_token,
                                         GHashTable *request_properties)
{
  return gabble_roomlist_manager_handle_request (manager, request_token,
      request_properties, FALSE);
}


static gboolean
gabble_roomlist_manager_ensure_channel (TpChannelManager *manager,
                                        gpointer request_token,
                                        GHashTable *request_properties)
{
  return gabble_roomlist_manager_handle_request (manager, request_token,
      request_properties, FALSE);
}


static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_roomlist_manager_foreach_channel;
  iface->foreach_channel_class = gabble_roomlist_manager_foreach_channel_class;
  iface->request_channel = gabble_roomlist_manager_request_channel;
  iface->create_channel = gabble_roomlist_manager_create_channel;
  iface->ensure_channel = gabble_roomlist_manager_ensure_channel;
}
