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

#include "telepathy-constants.h"
#include "telepathy-interfaces.h"
#include "telepathy-helpers.h"
#include "gabble-connection.h"
#include "gabble-disco.h"
#include "gabble-roomlist-channel.h"
#include "gabble-roomlist-channel-signals-marshal.h"

#include "gabble-roomlist-channel-glue.h"


#define TP_TYPE_ROOM_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), \
      G_TYPE_INVALID))

#define TP_TYPE_ROOM_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_ROOM_STRUCT))

G_DEFINE_TYPE(GabbleRoomlistChannel, gabble_roomlist_channel, G_TYPE_OBJECT)

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
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
/*  PROP_HANDLE_TYPE,
  PROP_HANDLE,*/
  PROP_DISCO,
  PROP_CONFERENCE_SERVER,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleRoomlistChannelPrivate GabbleRoomlistChannelPrivate;

struct _GabbleRoomlistChannelPrivate
{
  GabbleConnection *connection;
  GabbleDisco *disco;
  gchar *object_path;
  gchar *conference_server;

  gboolean closed;
  gboolean listing;

  GHashTable *remaining_rooms;

  gboolean dispose_has_run;
};

#define GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_ROOMLIST_CHANNEL, GabbleRoomlistChannelPrivate))

static void
gabble_roomlist_channel_init (GabbleRoomlistChannel *obj)
{
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (obj);

  priv->remaining_rooms = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_free);

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
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
      break;
/* TODO: do we need conference server handles??
 * case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
*/
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

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      if (priv->object_path)
        g_free (priv->object_path);

      priv->object_path = g_value_dup_string (value);
      break;
      /*
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
      */
    case PROP_DISCO:
      priv->disco = g_value_get_object (value);
      break;
    case PROP_CONFERENCE_SERVER:
      priv->conference_server = g_value_dup_string (value);
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

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "IM channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                    "The D-Bus interface representing the "
                                    "type of this channel.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHANNEL_TYPE, param_spec);
/*
  param_spec = g_param_spec_uint ("handle-type", "Contact handle type",
                                  "The TpHandleType representing a "
                                  "contact handle.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READABLE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle", "Contact handle",
                                  "The GabbleHandle representing the contact "
                                  "with whom this channel communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);
*/

  param_spec = g_param_spec_object ("disco", "GabbleDisco object",
                                    "GabbleDisco object to use for discovery.",
                                    GABBLE_TYPE_DISCO,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_WRITABLE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DISCO, param_spec);

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
                  gabble_roomlist_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GOT_ROOMS] =
    g_signal_new ("got-rooms",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roomlist_channel_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)), G_TYPE_INVALID)))));

  signals[LISTING_ROOMS] =
    g_signal_new ("listing-rooms",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roomlist_channel_marshal_VOID__BOOLEAN,
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

  /* release any references held by the object here */

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

  g_hash_table_destroy (priv->remaining_rooms);
  G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->finalize (object);
}

GabbleRoomlistChannel *
gabble_roomlist_channel_new (GabbleConnection *conn,
                             GabbleDisco *disco,
                             const gchar *object_path,
                             const gchar *conference_server)
{
  g_return_val_if_fail ( GABBLE_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail ( GABBLE_IS_DISCO (disco), NULL);
  g_return_val_if_fail ( object_path != NULL , NULL);
  g_return_val_if_fail ( conference_server != NULL , NULL);

  return GABBLE_ROOMLIST_CHANNEL (
      g_object_new (GABBLE_TYPE_ROOMLIST_CHANNEL,
                    "connection", conn,
                    "disco", disco,
                    "object-path", object_path,
                    "conference-server", conference_server));
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
room_info_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
          LmMessageNode *result, GError *error,
          gpointer user_data)
{
  GabbleRoomlistChannel *chan = user_data;
  GabbleRoomlistChannelPrivate *priv;
  LmMessageNode *identity, *feature, *field, *value_node;
  const char *category, *type, *var, *name, *namespace, *value;
  GabbleHandle handle;
  GHashTable *keys;
  GValue room = {0,};
  GPtrArray *rooms ;
  GValue *tmp;
  gboolean is_muc;

  #define INSERT_KEY(hash, name, type, type2, value) \
    do {\
      tmp = g_new0 (GValue, 1); \
      g_value_init (tmp, (type)); \
      g_value_set_##type2 (tmp, (value)); \
      g_hash_table_insert (hash, (name), tmp); \
    } while (0)

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (chan));
  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  g_hash_table_remove (priv->remaining_rooms, jid);

  if (error)
    {
      g_debug ("%s: got error %s", G_STRFUNC, error->message);
      goto done;
    }
  g_debug ("%s: got %s", G_STRFUNC, lm_message_node_to_string (result));


  keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                (GDestroyNotify) destroy_value);
  identity = lm_message_node_get_child (result, "identity");
  if (identity)
    {
      category = lm_message_node_get_attribute (identity, "category");
      type = lm_message_node_get_attribute (identity, "type");
      name = lm_message_node_get_attribute (identity, "name");
      g_debug ("%s: got room identity, name=%s, category=%s, type=%s", 
               G_STRFUNC, name, category, type);
      if (category && 0 == strcmp (category, "conference")
                   && 0 == strcmp (type, "text"))
        {
          is_muc = FALSE;
          for (feature = result->children; feature; feature = feature->next)
            {
              g_debug ("%s: Got child %s", G_STRFUNC, 
                       lm_message_node_to_string (feature));
              if (0 == strcmp (feature->name, "feature"))
                {
                  var = lm_message_node_get_attribute (feature, "var");
                  if (!var)
                    continue;
                  if (0 == strcmp (var, "http://jabber.org/protocol/muc"))
                    is_muc = TRUE;
                  else if (0 == strcmp (var, "muc_membersonly"))
                    INSERT_KEY (keys, "invite-only", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_open"))
                    INSERT_KEY (keys, "invite-only", G_TYPE_BOOLEAN, boolean, FALSE);
                  else if (0 == strcmp (var, "muc_passwordprotected"))
                    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_unsecured"))
                    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, FALSE);
                  else if (0 == strcmp (var, "muc_hidden"))
                    INSERT_KEY (keys, "hidden", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_public"))
                    INSERT_KEY (keys, "hidden", G_TYPE_BOOLEAN, boolean, FALSE);
                  else if (0 == strcmp (var, "muc_membersonly"))
                    INSERT_KEY (keys, "members-only", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_open"))
                    INSERT_KEY (keys, "members-only", G_TYPE_BOOLEAN, boolean, FALSE);
                  else if (0 == strcmp (var, "muc_moderated"))
                    INSERT_KEY (keys, "moderated", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_unmoderated"))
                    INSERT_KEY (keys, "moderated", G_TYPE_BOOLEAN, boolean, FALSE);
                  else if (0 == strcmp (var, "muc_nonanonymous"))
                    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, FALSE);
                  else if (0 == strcmp (var, "muc_anonymous"))
                    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_semianonymous"))
                    INSERT_KEY (keys, "semi-anonymous", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_persistant"))
                    INSERT_KEY (keys, "persistant", G_TYPE_BOOLEAN, boolean, TRUE);
                  else if (0 == strcmp (var, "muc_temporary"))
                    INSERT_KEY (keys, "persistant", G_TYPE_BOOLEAN, boolean, FALSE);

                }
              if (0 == strcmp (feature->name, "x"))
                {
                  namespace = lm_message_node_get_attribute (feature, "xmlns");
                  if (namespace && 0 == strcmp (namespace, "jabber:x:data"))
                    {
                      for (field = feature->children;
                           field; field = field->next)
                        {
                          if (0 != strcmp (field->name, "field"))
                            continue;
                          var = lm_message_node_get_attribute (field, "var");
                          value_node = lm_message_node_get_child (field, "value");
                          value = lm_message_node_get_value (value_node);
                          if (!value)
                            continue;

                          if (0 == strcmp (var, "muc#roominfo_description"))
                            {
                              INSERT_KEY (keys, "description",
                                          G_TYPE_STRING, string, value);
                            }
                          if (0 == strcmp (var, "muc#roominfo_occupants"))
                            {
                              INSERT_KEY (keys, "members", G_TYPE_UINT, uint,
                                (guint) g_ascii_strtoull (value, NULL, 10));
                            }
                           if (0 == strcmp (var, "muc#roominfo_lang"))
                            {
                              INSERT_KEY (keys, "language", G_TYPE_STRING,
                                          string, value);
                            }
                        }
                    }
                }
            }
          if (is_muc)
            {
              INSERT_KEY (keys, "name", G_TYPE_STRING, string, name);
              g_debug ("%s:emitting new room signal for %s", G_STRFUNC,jid);

              handle = gabble_handle_for_room (
                  _gabble_connection_get_handles (priv->connection),jid);

              g_value_init (&room, TP_TYPE_ROOM_STRUCT);
              g_value_take_boxed (&room,
                  dbus_g_type_specialized_construct (TP_TYPE_ROOM_STRUCT));


              dbus_g_type_struct_set (&room,
                  0, handle,
                  1, "org.freedesktop.Telepathy.Channel.Type.Text",
                  2, keys,
                  G_MAXUINT);

              rooms = g_ptr_array_sized_new(1);
              g_ptr_array_add (rooms, g_value_get_boxed (&room));
              g_signal_emit (chan, signals[GOT_ROOMS], 0, rooms);
              g_ptr_array_free (rooms, TRUE);
              g_value_unset (&room);
            }
        }
    }
done:
  if (g_hash_table_size (priv->remaining_rooms) == 0)
    {
      priv->listing=FALSE;
      g_signal_emit (chan, signals [LISTING_ROOMS], 0, FALSE);
    }
  return;
}


static void
rooms_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
          LmMessageNode *result, GError *error,
          gpointer user_data)
{
  LmMessageNode *iter;
  GabbleRoomlistChannel *chan = user_data;
  GabbleRoomlistChannelPrivate *priv;
  const char *item_jid, *name;
  gpointer key, value;
  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (chan));
  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  if (error)
    {
      g_debug ("%s: got error %s", G_STRFUNC, error->message);
      return;
    }
  g_debug ("%s: got %s", G_STRFUNC, lm_message_node_to_string (result));

  iter = result->children;

  for (; iter; iter = iter->next)
    {
      if (0 == strcmp (iter->name, "item"))
        {
          item_jid = lm_message_node_get_attribute (iter, "jid");
          name = lm_message_node_get_attribute (iter, "name");
          if (item_jid && name)
            {
              if (! g_hash_table_lookup_extended (priv->remaining_rooms, item_jid, &key, &value))
                {
                  g_hash_table_insert (priv->remaining_rooms,
                                       g_strdup(item_jid), g_strdup(name));

                  gabble_disco_request (priv->disco, GABBLE_DISCO_TYPE_INFO,
                                        item_jid, NULL,
                                        room_info_cb, chan, G_OBJECT(chan), NULL);
                }
            }
        }
    }
}

/************************* DBUS Method definitions **************************/

/**
 * gabble_roomlist_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roomlist_channel_close (GabbleRoomlistChannel *obj, GError **error)
{
  GabbleRoomlistChannelPrivate *priv;

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (obj));

  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (obj);
  priv->closed = TRUE;

  g_debug ("%s called on %p", G_STRFUNC, obj);
  g_signal_emit(obj, signals[CLOSED], 0);

  return TRUE;
}


/**
 * gabble_roomlist_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roomlist_channel_get_channel_type (GabbleRoomlistChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
  return TRUE;
}


/**
 * gabble_roomlist_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roomlist_channel_get_handle (GabbleRoomlistChannel *obj, guint* ret, guint* ret1, GError **error)
{
  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (obj));

  *ret = 0;
  *ret1 = 0;

  return TRUE;
}


/**
 * gabble_roomlist_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roomlist_channel_get_interfaces (GabbleRoomlistChannel *obj, gchar *** ret, GError **error)
{
  const char *interfaces[] = { NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_roomlist_channel_get_listing_rooms
 *
 * Implements DBus method GetListingRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roomlist_channel_get_listing_rooms (GabbleRoomlistChannel *obj, gboolean* ret, GError **error)
{
  GabbleRoomlistChannelPrivate *priv;

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (obj));

  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (obj);
  *ret = priv->listing;
  return TRUE;
}


/**
 * gabble_roomlist_channel_list_rooms
 *
 * Implements DBus method ListRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roomlist_channel_list_rooms (GabbleRoomlistChannel *obj, GError **error)
{
  GabbleRoomlistChannelPrivate *priv;

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (obj));

  priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (obj);

  priv->listing = TRUE;
  g_signal_emit (obj, signals[LISTING_ROOMS], 0, TRUE);
  gabble_disco_request (priv->disco, GABBLE_DISCO_TYPE_ITEMS,
                        priv->conference_server, NULL,
                        rooms_cb, obj, G_OBJECT(obj), NULL);
  return TRUE;
}

