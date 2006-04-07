/*
 * gabble-roster-channel.c - Source for GabbleRosterChannel
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

#include "gabble-connection.h"
#include "gintset.h"
#include "handle-set.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-roster-channel.h"
#include "gabble-roster-channel-glue.h"
#include "gabble-roster-channel-signals-marshal.h"

G_DEFINE_TYPE(GabbleRosterChannel, gabble_roster_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    MEMBERS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleRosterChannelPrivate GabbleRosterChannelPrivate;

struct _GabbleRosterChannelPrivate
{
  GabbleConnection *connection;
  char *object_path;
  GabbleHandle handle;

  TpChannelGroupFlags group_flags;
  GabbleHandleSet *members;
  GabbleHandleSet *local_pending;
  GabbleHandleSet *remote_pending;

  gboolean dispose_has_run;
};

#define GABBLE_ROSTER_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannelPrivate))

static void
gabble_roster_channel_init (GabbleRosterChannel *obj)
{
  /* GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj); */

  /* allocate any data required by the object here */
}

static GObject *
gabble_roster_channel_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleRosterChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  gboolean valid;

  obj = G_OBJECT_CLASS (gabble_roster_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));

  handles = _gabble_connection_get_handles (priv->connection);
  valid = gabble_handle_ref (handles, TP_HANDLE_TYPE_LIST, priv->handle);
  g_assert (valid);

  priv->members = handle_set_new (handles, TP_HANDLE_TYPE_CONTACT);
  priv->local_pending = handle_set_new (handles, TP_HANDLE_TYPE_CONTACT);
  priv->remote_pending = handle_set_new (handles, TP_HANDLE_TYPE_CONTACT);

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_roster_channel_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_LIST);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_roster_channel_set_property (GObject     *object,
                                    guint        property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_roster_channel_dispose (GObject *object);
static void gabble_roster_channel_finalize (GObject *object);

static void
gabble_roster_channel_class_init (GabbleRosterChannelClass *gabble_roster_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roster_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_roster_channel_class, sizeof (GabbleRosterChannelPrivate));

  object_class->constructor = gabble_roster_channel_constructor;

  object_class->get_property = gabble_roster_channel_get_property;
  object_class->set_property = gabble_roster_channel_set_property;

  object_class->dispose = gabble_roster_channel_dispose;
  object_class->finalize = gabble_roster_channel_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "Roster channel object.",
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

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roster_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GROUP_FLAGS_CHANGED] =
    g_signal_new ("group-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roster_channel_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[MEMBERS_CHANGED] =
    g_signal_new ("members-changed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roster_channel_marshal_VOID__STRING_BOXED_BOXED_BOXED_BOXED,
                  G_TYPE_NONE, 5, G_TYPE_STRING, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_roster_channel_class), &dbus_glib_gabble_roster_channel_object_info);
}

void
gabble_roster_channel_dispose (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_emit(self, signals[CLOSED], 0);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose (object);
}

void
gabble_roster_channel_finalize (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);
  GabbleHandleRepo *handles;

  /* free any data held directly by the object here */

  g_free (priv->object_path);

  handles = _gabble_connection_get_handles (priv->connection);
  gabble_handle_unref (handles, TP_HANDLE_TYPE_CONTACT, priv->handle);

  handle_set_destroy (priv->members);
  handle_set_destroy (priv->local_pending);
  handle_set_destroy (priv->remote_pending);

  G_OBJECT_CLASS (gabble_roster_channel_parent_class)->finalize (object);
}


/**
 * _gabble_roster_channel_change_group_flags:
 *
 * Request a change to be made to the flags set on this channel. Emits
 * the signal with the changes which were made.
 */
void
_gabble_roster_channel_change_group_flags (GabbleRosterChannel *chan,
                                           TpChannelGroupFlags add,
                                           TpChannelGroupFlags remove)
{
  GabbleRosterChannelPrivate *priv;
  TpChannelGroupFlags added, removed;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (chan));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);

  added = add & ~priv->group_flags;
  priv->group_flags |= added;

  removed = remove & priv->group_flags;
  priv->group_flags &= ~removed;

  g_debug ("%s: emitting group flags changed, added 0x%X, removed 0x%X", G_STRFUNC, added, removed);

  g_signal_emit(chan, signals[GROUP_FLAGS_CHANGED], 0, added, removed);
}


/**
 * _gabble_roster_channel_change_members:
 *
 * Request members to be added, removed or marked as local or remote pending.
 * Changes member sets, references, and emits the MembersChanged signal.
 */
void
_gabble_roster_channel_change_members (GabbleRosterChannel *chan,
                                       const char *message,
                                       GIntSet *add,
                                       GIntSet *remove,
                                       GIntSet *local_pending,
                                       GIntSet *remote_pending)
{
  GabbleRosterChannelPrivate *priv;
  GIntSet *new_add, *new_remove, *new_local_pending,
          *new_remote_pending, *tmp, *tmp2;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (chan));
  g_assert (add != NULL);
  g_assert (remove != NULL);
  g_assert (local_pending != NULL);
  g_assert (remote_pending != NULL);

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);


  /* members + add */
  new_add = handle_set_update (priv->members, add);

  /* members - remove */
  new_remove = handle_set_difference_update (priv->members, remove);

  /* members - local_pending */
  tmp = handle_set_difference_update (priv->members, local_pending);
  g_intset_destroy (tmp);

  /* members - remote_pending */
  tmp = handle_set_difference_update (priv->members, remote_pending);
  g_intset_destroy (tmp);


  /* local pending + local_pending */
  new_local_pending = handle_set_update (priv->local_pending, local_pending);

  /* local pending - add */
  tmp = handle_set_difference_update (priv->local_pending, add);
  g_intset_destroy (tmp);

  /* local pending - remove */
  tmp = handle_set_difference_update (priv->local_pending, remove);
  tmp2 = g_intset_union (new_remove, tmp);
  g_intset_destroy (new_remove);
  g_intset_destroy (tmp);
  new_remove = tmp2;

  /* local pending - remote_pending */
  tmp = handle_set_difference_update (priv->local_pending, remote_pending);
  g_intset_destroy (tmp);


  /* remote pending + remote_pending */
  new_remote_pending = handle_set_update (priv->remote_pending, remote_pending);

  /* remote pending - add */
  tmp = handle_set_difference_update (priv->remote_pending, add);
  g_intset_destroy (tmp);

  /* remote pending - remove */
  tmp = handle_set_difference_update (priv->remote_pending, remove);
  tmp2 = g_intset_union (new_remove, tmp);
  g_intset_destroy (new_remove);
  g_intset_destroy (tmp);
  new_remove = tmp2;

  /* remote pending - local_pending */
  tmp = handle_set_difference_update (priv->remote_pending, local_pending);
  g_intset_destroy (tmp);

  if (g_intset_size (new_add) > 0 ||
      g_intset_size (new_remove) > 0 ||
      g_intset_size (new_local_pending) > 0 ||
      g_intset_size (new_remote_pending) > 0)
    {
      GArray *arr_add, *arr_remove, *arr_local, *arr_remote;

      /* translate intsets to arrays */
      arr_add = g_intset_to_array (new_add);
      arr_remove = g_intset_to_array (new_remove);
      arr_local = g_intset_to_array (new_local_pending);
      arr_remote = g_intset_to_array (new_remote_pending);

      /* emit signal */
      g_signal_emit(chan, signals[MEMBERS_CHANGED], 0,
                    message,
                    arr_add, arr_remove,
                    arr_local, arr_remote);

      /* free arrays */
      g_array_free (arr_add, TRUE);
      g_array_free (arr_remove, TRUE);
      g_array_free (arr_local, TRUE);
      g_array_free (arr_remote, TRUE);
    }
  else
    {
      g_debug ("%s: not emitting signal, nothing changed", G_STRFUNC);
    }

  /* free intsets */
  g_intset_destroy (new_add);
  g_intset_destroy (new_remove);
  g_intset_destroy (new_local_pending);
  g_intset_destroy (new_remote_pending);
}


/**
 * gabble_roster_channel_add_members
 *
 * Implements DBus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_add_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandleRepo *repo;
  int i;
  GabbleHandle handle;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);
  repo = _gabble_connection_get_handles (priv->connection);

  /* reject invalid handles */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if (!gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, handle))
        {
          g_debug ("%s: invalid handle %u", G_STRFUNC, handle);

          *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
              "invalid handle %u", handle);

          return FALSE;
        }
    }

  /* publish list */
  if (gabble_handle_for_list_publish (repo) == priv->handle)
    {
      /* reject handles who are not locally pending */
      for (i = 0; i < contacts->len; i++)
        {
          handle = g_array_index (contacts, GabbleHandle, i);

          if (!handle_set_is_member (priv->local_pending, handle))
            {
              g_debug ("%s: can't add members to publish list %u", G_STRFUNC, handle);

              *error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                  "can't add members to publish list %u", handle);

              return FALSE;
            }
        }

      /* send <presence type="subscribed"> messages */
      for (i = 0; i < contacts->len; i++)
        {
          LmMessage *message;
          const char *contact;
          gboolean result;

          handle = g_array_index (contacts, GabbleHandle, i);
          contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

          message = lm_message_new_with_sub_type (contact,
              LM_MESSAGE_TYPE_PRESENCE,
              LM_MESSAGE_SUB_TYPE_SUBSCRIBED);
          result = _gabble_connection_send (priv->connection, message, error);
          lm_message_unref (message);

          if (!result)
            return FALSE;
        }
    }
  /* subscribe list */
  else if (gabble_handle_for_list_subscribe (repo) == priv->handle)
    {
      /* send <presence type="subscribe">, but skip existing members */
      for (i = 0; i < contacts->len; i++)
        {
          LmMessage *message;
          const char *contact;
          gboolean result;

          handle = g_array_index (contacts, GabbleHandle, i);
          contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

          if (handle_set_is_member (priv->members, handle))
            {
              g_debug ("%s: already subscribed to handle %u, skipping", G_STRFUNC, handle);

              continue;
            }

          message = lm_message_new_with_sub_type (contact,
              LM_MESSAGE_TYPE_PRESENCE,
              LM_MESSAGE_SUB_TYPE_SUBSCRIBE);
          result = _gabble_connection_send (priv->connection, message, error);
          lm_message_unref (message);

          if (!result)
            return FALSE;
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
}


/**
 * gabble_roster_channel_close
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
gboolean gabble_roster_channel_close (GabbleRosterChannel *obj, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "you may not close contact list channels");

  return FALSE;
}


/**
 * gabble_roster_channel_get_channel_type
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
gboolean gabble_roster_channel_get_channel_type (GabbleRosterChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);

  return TRUE;
}


/**
 * gabble_roster_channel_get_group_flags
 *
 * Implements DBus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_group_flags (GabbleRosterChannel *obj, guint* ret, GError **error)
{
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  *ret = priv->group_flags;

  return TRUE;
}


/**
 * gabble_roster_channel_get_handle
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
gboolean gabble_roster_channel_get_handle (GabbleRosterChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_LIST;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_roster_channel_get_interfaces
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
gboolean gabble_roster_channel_get_interfaces (GabbleRosterChannel *obj, gchar *** ret, GError **error)
{
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_roster_channel_get_local_pending_members
 *
 * Implements DBus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_local_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  *ret = handle_set_to_array (priv->local_pending);

  return TRUE;
}


/**
 * gabble_roster_channel_get_members
 *
 * Implements DBus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  *ret = handle_set_to_array (priv->members);

  return TRUE;
}


/**
 * gabble_roster_channel_get_remote_pending_members
 *
 * Implements DBus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_remote_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  *ret = handle_set_to_array (priv->remote_pending);

  return TRUE;
}


/**
 * gabble_roster_channel_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_self_handle (GabbleRosterChannel *obj, guint* ret, GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandle handle;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  if (!gabble_connection_get_self_handle (priv->connection, &handle, error))
    return FALSE;

  if (!handle_set_is_member (priv->members, handle))
    {
      *ret = 0;
    }
  else
    {
      *ret = handle;
    }

  return TRUE;
}


/**
 * gabble_roster_channel_remove_members
 *
 * Implements DBus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_remove_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandleRepo *repo;
  int i;
  GabbleHandle handle;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);
  repo = _gabble_connection_get_handles (priv->connection);

  /* reject invalid handles */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, GabbleHandle, i);

      if (!gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, handle))
        {
          g_debug ("%s: invalid handle %u", G_STRFUNC, handle);

          *error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
              "invalid handle %u", handle);

          return FALSE;
        }
    }

  /* publish list */
  if (gabble_handle_for_list_publish (repo) == priv->handle)
    {
      /* reject handles who are not members or locally pending */
      for (i = 0; i < contacts->len; i++)
        {
          handle = g_array_index (contacts, GabbleHandle, i);

          if (!handle_set_is_member (priv->members, handle) ||
              !handle_set_is_member (priv->local_pending, handle))
            {
              g_debug ("%s: handle isn't present to remove from publish list %u", G_STRFUNC, handle);

              *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                  "handle isn't present to remove from publish list %u", handle);

              return FALSE;
            }
        }

      /* send <presence type="unsubscribed"> messages */
      for (i = 0; i < contacts->len; i++)
        {
          LmMessage *message;
          const char *contact;
          gboolean result;

          handle = g_array_index (contacts, GabbleHandle, i);
          contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

          message = lm_message_new_with_sub_type (contact,
              LM_MESSAGE_TYPE_PRESENCE,
              LM_MESSAGE_SUB_TYPE_SUBSCRIBED);
          result = _gabble_connection_send (priv->connection, message, error);
          lm_message_unref (message);

          if (!result)
            return FALSE;
        }
    }
  /* subscribe list */
  else if (gabble_handle_for_list_subscribe (repo) == priv->handle)
    {
      /* reject handles who are not members or remote pending */
      for (i = 0; i < contacts->len; i++)
        {
          handle = g_array_index (contacts, GabbleHandle, i);

          if (!handle_set_is_member (priv->members, handle) ||
              !handle_set_is_member (priv->remote_pending, handle))
            {
              g_debug ("%s: handle isn't present to remove from subscribe list %u", G_STRFUNC, handle);

              *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                  "handle isn't present to remove from subscribe list %u", handle);

              return FALSE;
            }
        }

      /* send <presence type="unsubscribe"> */
      for (i = 0; i < contacts->len; i++)
        {
          LmMessage *message;
          const char *contact;
          gboolean result;

          handle = g_array_index (contacts, GabbleHandle, i);
          contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

          message = lm_message_new_with_sub_type (contact,
              LM_MESSAGE_TYPE_PRESENCE,
              LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);
          result = _gabble_connection_send (priv->connection, message, error);
          lm_message_unref (message);

          if (!result)
            return FALSE;
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
}

