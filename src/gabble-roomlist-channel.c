/*
 * gabble-roomlist-channel.c - Source for GabbleRoomlistChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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
#include <stdlib.h>
#include <string.h>

#define DEBUG_FLAG GABBLE_DEBUG_ROOMLIST

#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "handles.h"
#include "handle-set.h"
#include <telepathy-glib/tp-enums.h>
#include <telepathy-glib/tp-interfaces.h>
#include <telepathy-glib/tp-helpers.h>
#include <telepathy-glib/tp-channel-iface.h>
#include "namespaces.h"
#include "util.h"

#include "gabble-roomlist-channel.h"
#include "gabble-roomlist-channel-glue.h"
#include "gabble-roomlist-channel-signals-marshal.h"

#define TP_TYPE_ROOM_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), \
      G_TYPE_INVALID))

#define TP_TYPE_ROOM_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_ROOM_STRUCT))

G_DEFINE_TYPE_WITH_CODE (GabbleRoomlistChannel, gabble_roomlist_channel,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* signal enum */
enum
{
    CLOSED,
    GOT_ROOMS,
    LISTING_ROOMS,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_CONFERENCE_SERVER,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleRoomlistChannelPrivate GabbleRoomlistChannelPrivate;

struct _GabbleRoomlistChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  gchar *conference_server;

  gboolean closed;
  gboolean listing;

  gpointer disco_pipeline;
  GabbleHandleSet *signalled_rooms;

  GPtrArray *pending_room_signals;
  guint timer_source_id;

  gboolean dispose_has_run;
};

#define GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleRoomlistChannelPrivate *)obj->priv)

#define ROOM_SIGNAL_INTERVAL 300

static gboolean emit_room_signal (gpointer data);

static void
gabble_roomlist_channel_init (GabbleRoomlistChannel *self)
{
  GabbleRoomlistChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_ROOMLIST_CHANNEL, GabbleRoomlistChannelPrivate);

  self->priv = priv;

  priv->pending_room_signals = g_ptr_array_new ();
}


static GObject *
gabble_roomlist_channel_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  GabbleRoomlistChannelPrivate *priv;
  DBusGConnection *bus;

  obj = G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (GABBLE_ROOMLIST_CHANNEL (obj));

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_roomlist_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleRoomlistChannel *chan = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_CONFERENCE_SERVER:
      g_value_set_string (value, priv->conference_server);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_roomlist_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GabbleRoomlistChannel *chan = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);
  GabbleHandleSet *new_signalled_rooms;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_HANDLE_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      new_signalled_rooms = handle_set_new (priv->conn->handles, TP_HANDLE_TYPE_ROOM);
      if (priv->signalled_rooms != NULL)
        {
          const TpIntSet *add;
          TpIntSet *tmp;
          add = handle_set_peek (priv->signalled_rooms);
          tmp = handle_set_update (new_signalled_rooms, add);
          handle_set_destroy (priv->signalled_rooms);
          tp_intset_destroy (tmp);
        }
      priv->signalled_rooms = new_signalled_rooms;
      break;
    case PROP_CONFERENCE_SERVER:
      g_free (priv->conference_server);
      priv->conference_server = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_roomlist_channel_dispose (GObject *object);
static void gabble_roomlist_channel_finalize (GObject *object);

static void
gabble_roomlist_channel_class_init (GabbleRoomlistChannelClass *gabble_roomlist_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roomlist_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_roomlist_channel_class, sizeof (GabbleRoomlistChannelPrivate));

  object_class->constructor = gabble_roomlist_channel_constructor;

  object_class->get_property = gabble_roomlist_channel_get_property;
  object_class->set_property = gabble_roomlist_channel_set_property;

  object_class->dispose = gabble_roomlist_channel_dispose;
  object_class->finalize = gabble_roomlist_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "room list channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("conference-server",
                                    "Name of conference server to use",
                                    "Name of the XMPP conference server "
                                    "on which to list rooms",
                                    "",
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_WRITABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONFERENCE_SERVER,
                                   param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GOT_ROOMS] =
    g_signal_new ("got-rooms",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)), G_TYPE_INVALID)))));

  signals[LISTING_ROOMS] =
    g_signal_new ("listing-rooms",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_roomlist_channel_class), &dbus_glib_gabble_roomlist_channel_object_info);
}

void
gabble_roomlist_channel_dispose (GObject *object)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->listing)
    {
      emit_room_signal (object);
      g_signal_emit (object, signals [LISTING_ROOMS], 0, FALSE);
      priv->listing = FALSE;
    }

  if (!priv->closed)
    {
      g_signal_emit (object, signals[CLOSED], 0);
      priv->closed = TRUE;
    }

  if (priv->disco_pipeline != NULL)
    {
      gabble_disco_pipeline_destroy (priv->disco_pipeline);
      priv->disco_pipeline = NULL;
    }

  if (priv->timer_source_id)
    {
      g_source_remove (priv->timer_source_id);
      priv->timer_source_id = 0;
    }

  g_assert (priv->pending_room_signals != NULL);
  g_assert (priv->pending_room_signals->len == 0);
  g_ptr_array_free (priv->pending_room_signals, TRUE);
  priv->pending_room_signals = NULL;

  if (G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->dispose (object);
}

void
gabble_roomlist_channel_finalize (GObject *object)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  g_free (priv->object_path);
  g_free (priv->conference_server);

  if (priv->signalled_rooms != NULL)
    handle_set_destroy (priv->signalled_rooms);

  G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->finalize (object);
}

GabbleRoomlistChannel *
_gabble_roomlist_channel_new (GabbleConnection *conn,
                              const gchar *object_path,
                              const gchar *conference_server)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (conference_server != NULL, NULL);

  return GABBLE_ROOMLIST_CHANNEL (
      g_object_new (GABBLE_TYPE_ROOMLIST_CHANNEL,
                    "connection", conn,
                    "object-path", object_path,
                    "conference-server", conference_server, NULL));
}

static gboolean
emit_room_signal (gpointer data)
{
  GabbleRoomlistChannel *chan = data;
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  if (!priv->listing)
      return FALSE;

  if (priv->pending_room_signals->len == 0)
      return TRUE;

  g_signal_emit (chan, signals[GOT_ROOMS], 0, priv->pending_room_signals);

  while (priv->pending_room_signals->len != 0)
    {
      gpointer boxed = g_ptr_array_index (priv->pending_room_signals, 0);
      g_boxed_free (TP_TYPE_ROOM_STRUCT, boxed);
      g_ptr_array_remove_index_fast (priv->pending_room_signals, 0);
    }

  return TRUE;
}

/**
 * destroy_value:
 * @data: a GValue to destroy
 *
 * destroys a GValue allocated on the heap
 */
static void
destroy_value (GValue *value)
{
  g_value_unset (value);
  g_free (value);
}

static void
room_info_cb (gpointer pipeline, GabbleDiscoItem *item, gpointer user_data)
{
  GabbleRoomlistChannel *chan = user_data;
  GabbleRoomlistChannelPrivate *priv;
  const char *jid, *category, *type, *var, *name;
  TpHandle handle;
  GHashTable *keys;
  GValue room = {0,};
  GValue *tmp;
  gpointer k, v;

  #define INSERT_KEY(hash, name, type, type2, value) \
    do {\
      tmp = g_new0 (GValue, 1); \
      g_value_init (tmp, (type)); \
      g_value_set_##type2 (tmp, (value)); \
      g_hash_table_insert (hash, (name), tmp); \
    } while (0)

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (chan));
  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  jid = item->jid;
  name = item->name;
  category = item->category;
  type = item->type;

  if (0 != strcmp (category, "conference") ||
      0 != strcmp (type, "text"))
    return;

  if (!g_hash_table_lookup_extended (item->features, "http://jabber.org/protocol/muc", &k, &v))
    {
      /* not muc */
      return;
    }

  DEBUG ("got room identity, name=%s, category=%s, type=%s", name, category, type);

  keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                (GDestroyNotify) destroy_value);

  INSERT_KEY (keys, "name", G_TYPE_STRING, string, name);

  if (g_hash_table_lookup_extended (item->features, "muc_membersonly", &k, &v))
    INSERT_KEY (keys, "invite-only", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_open", &k, &v))
    INSERT_KEY (keys, "invite-only", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_passwordprotected", &k, &v))
    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_unsecure", &k, &v))
    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_unsecured", &k, &v))
    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_hidden", &k, &v))
    INSERT_KEY (keys, "hidden", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_public", &k, &v))
    INSERT_KEY (keys, "hidden", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_membersonly", &k, &v))
    INSERT_KEY (keys, "members-only", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_open", &k, &v))
    INSERT_KEY (keys, "members-only", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_moderated", &k, &v))
    INSERT_KEY (keys, "moderated", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_unmoderated", &k, &v))
    INSERT_KEY (keys, "moderated", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_nonanonymous", &k, &v))
    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_anonymous", &k, &v))
    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_semianonymous", &k, &v))
    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_persistent", &k, &v))
    INSERT_KEY (keys, "persistent", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_temporary", &k, &v))
    INSERT_KEY (keys, "persistent", G_TYPE_BOOLEAN, boolean, FALSE);

  var = g_hash_table_lookup (item->features, "muc#roominfo_description");
  if (var != NULL)
    INSERT_KEY (keys, "description", G_TYPE_STRING, string, var);

  var = g_hash_table_lookup (item->features, "muc#roominfo_occupants");
  if (var != NULL)
    INSERT_KEY (keys, "members", G_TYPE_UINT, uint,
                (guint) g_ascii_strtoull (var, NULL, 10));

  var = g_hash_table_lookup (item->features, "muc#roominfo_lang");
  if (var != NULL)
    INSERT_KEY (keys, "language", G_TYPE_STRING, string, var);

  handle = gabble_handle_for_room (priv->conn->handles, jid);

  handle_set_add (priv->signalled_rooms, handle);

  g_value_init (&room, TP_TYPE_ROOM_STRUCT);
  g_value_take_boxed (&room,
      dbus_g_type_specialized_construct (TP_TYPE_ROOM_STRUCT));

  dbus_g_type_struct_set (&room,
      0, handle,
      1, "org.freedesktop.Telepathy.Channel.Type.Text",
      2, keys,
      G_MAXUINT);

  DEBUG ("adding new room signal data to pending: %s", jid);
  g_ptr_array_add (priv->pending_room_signals, g_value_get_boxed (&room));
  g_hash_table_destroy (keys);
}

static void
rooms_end_cb (gpointer data, gpointer user_data)
{
  GabbleRoomlistChannel *chan = user_data;
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  emit_room_signal (chan);

  priv->listing = FALSE;
  g_signal_emit (chan, signals[LISTING_ROOMS], 0, FALSE);

  g_source_remove (priv->timer_source_id);
  priv->timer_source_id = 0;
}


/************************* D-Bus Method definitions **************************/

/**
 * gabble_roomlist_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_close (GabbleRoomlistChannel *self,
                               GError **error)
{
  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  DEBUG ("called on %p", self);

  g_object_run_dispose (G_OBJECT (self));

  return TRUE;
}


/**
 * gabble_roomlist_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_channel_type (GabbleRoomlistChannel *self,
                                          gchar **ret,
                                          GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
  return TRUE;
}


/**
 * gabble_roomlist_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_handle (GabbleRoomlistChannel *self,
                                    guint *ret,
                                    guint *ret1,
                                    GError **error)
{
  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  *ret = 0;
  *ret1 = 0;

  return TRUE;
}


/**
 * gabble_roomlist_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_interfaces (GabbleRoomlistChannel *self,
                                        gchar ***ret,
                                        GError **error)
{
  const char *interfaces[] = { NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_roomlist_channel_get_listing_rooms
 *
 * Implements D-Bus method GetListingRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_listing_rooms (GabbleRoomlistChannel *self,
                                           gboolean *ret,
                                           GError **error)
{
  GabbleRoomlistChannelPrivate *priv;

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  *ret = priv->listing;
  return TRUE;
}


/**
 * gabble_roomlist_channel_list_rooms
 *
 * Implements D-Bus method ListRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_list_rooms (GabbleRoomlistChannel *self,
                                    GError **error)
{
  GabbleRoomlistChannelPrivate *priv;

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  priv->listing = TRUE;
  g_signal_emit (self, signals[LISTING_ROOMS], 0, TRUE);

  if (priv->disco_pipeline == NULL)
    priv->disco_pipeline = gabble_disco_pipeline_init (priv->conn->disco,
        room_info_cb, rooms_end_cb, self);

  gabble_disco_pipeline_run (priv->disco_pipeline, priv->conference_server);

  priv->timer_source_id = g_timeout_add (ROOM_SIGNAL_INTERVAL,
      emit_room_signal, self);

  return TRUE;
}

