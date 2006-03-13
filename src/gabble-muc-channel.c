/*
 * gabble-muc-channel.c - Source for GabbleMucChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#include "allocator.h"
#include "gabble-connection.h"

#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-muc-channel.h"
#include "gabble-muc-channel-signals-marshal.h"

#include "gabble-muc-channel-glue.h"

#define MAX_PENDING_MESSAGES 256
#define MAX_MESSAGE_SIZE 8*1024 - 1

G_DEFINE_TYPE(GabbleMucChannel, gabble_muc_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    PASSWORD_FLAGS_CHANGED,
    RECEIVED,
    SENT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMucChannelPrivate GabbleMucChannelPrivate;

struct _GabbleMucChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;

  GabbleHandle handle;
  const gchar *jid;

  gchar *self_jid;

  guint recv_id;
  GQueue *pending_messages;

  gboolean closed;
  gboolean dispose_has_run;
};

/* pending message */
typedef struct _GabbleMucPendingMessage GabbleMucPendingMessage;

struct _GabbleMucPendingMessage
{
  guint id;
  time_t timestamp;
  GabbleHandle sender;
  TpChannelTextMessageType type;
  gchar *text;
};

#define GABBLE_MUC_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelPrivate))

static void
gabble_muc_channel_init (GabbleMucChannel *obj)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  priv->pending_messages = g_queue_new ();
}

static void send_join_request (GabbleMucChannel *channel);

static GObject *
gabble_muc_channel_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMucChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  GabbleHandle self_handle_primary, self_handle;
  gboolean valid;
  const gchar *self_jid;
  gchar *username, *server;
  GError *error;
  GIntSet *empty, *set;

  obj = G_OBJECT_CLASS (gabble_muc_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (GABBLE_MUC_CHANNEL (obj));

  handles = _gabble_connection_get_handles (priv->conn);
  valid = gabble_connection_get_self_handle (priv->conn, &self_handle_primary, &error);
  g_assert (valid);

  /* ref our room handle */
  valid = gabble_handle_ref (handles, TP_HANDLE_TYPE_ROOM, priv->handle);
  g_assert (valid);

  /* get the room's jid */
  priv->jid = gabble_handle_inspect (handles, TP_HANDLE_TYPE_ROOM, priv->handle);

  /* generate our own jid in the room */
  self_jid = gabble_handle_inspect (handles, TP_HANDLE_TYPE_CONTACT, self_handle_primary);
  gabble_handle_decode_jid (self_jid, &username, &server, NULL);
  priv->self_jid = g_strdup_printf ("%s/%s", priv->jid, username);
  g_free (username);
  g_free (server);
  self_handle = gabble_handle_for_contact (handles, priv->self_jid, TRUE);

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* initialize group mixin */
  gabble_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleMucChannel, group),
                           handles, self_handle);

  /* automatically add ourself to remote pending */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, self_handle);

  gabble_group_mixin_change_members (obj, "", empty, empty, empty, set);

  g_intset_destroy (empty);
  g_intset_destroy (set);

  /* seek to enter the room */
  send_join_request (GABBLE_MUC_CHANNEL (obj));

  return obj;
}

static LmHandlerResult
join_request_reply_cb (GabbleConnection *conn,
                       LmMessage *sent_msg,
                       LmMessage *reply_msg,
                       GObject *object,
                       gpointer user_data)
{
  g_debug ("%s: %s", G_STRFUNC, lm_message_node_to_string (reply_msg->node));

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
send_join_request (GabbleMucChannel *channel)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  LmMessageNode *node;
  GError *error;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (channel);

  /* build the message */
  msg = lm_message_new (priv->self_jid, LM_MESSAGE_TYPE_PRESENCE);

  node = lm_message_node_add_child (msg->node, "x", NULL);
  lm_message_node_set_attribute (node, "xmlns", "http://jabber.org/protocol/muc");

  /* send it */
  if (!_gabble_connection_send_with_reply (priv->conn, msg,
                                           join_request_reply_cb,
                                           G_OBJECT (channel), NULL,
                                           &error))
    {
      /* TODO: do something clever here */
      g_warning ("%s: _gabble_connection_send_with_reply failed", G_STRFUNC);
      g_error_free (error);
    }
  else
    {
      g_debug ("%s: join request sent", G_STRFUNC);
    }

  lm_message_unref (msg);
}

static void
gabble_muc_channel_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
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
gabble_muc_channel_set_property (GObject     *object,
                                 guint        property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      if (priv->object_path)
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

static void gabble_muc_channel_dispose (GObject *object);
static void gabble_muc_channel_finalize (GObject *object);
static gboolean gabble_muc_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);
static gboolean gabble_muc_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);

static void
gabble_muc_channel_class_init (GabbleMucChannelClass *gabble_muc_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_muc_channel_class, sizeof (GabbleMucChannelPrivate));

  object_class->constructor = gabble_muc_channel_constructor;

  object_class->get_property = gabble_muc_channel_get_property;
  object_class->set_property = gabble_muc_channel_set_property;

  object_class->dispose = gabble_muc_channel_dispose;
  object_class->finalize = gabble_muc_channel_finalize;

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

  param_spec = g_param_spec_uint ("handle", "Room handle",
                                  "The GabbleHandle representing the room "
                                  "with whom this channel communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[PASSWORD_FLAGS_CHANGED] =
    g_signal_new ("password-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  gabble_group_mixin_class_init (object_class,
                                 G_STRUCT_OFFSET (GabbleMucChannelClass, group_class),
                                 gabble_muc_channel_add_member,
                                 gabble_muc_channel_remove_member);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_muc_channel_class), &dbus_glib_gabble_muc_channel_object_info);
}

void
gabble_muc_channel_dispose (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose (object);
}

static void _gabble_muc_pending_free (GabbleMucPendingMessage *msg);

void
gabble_muc_channel_finalize (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);
  GabbleMucPendingMessage *msg;
  GabbleHandleRepo *handles;

  /* free any data held directly by the object here */
  handles = _gabble_connection_get_handles (priv->conn);
  gabble_handle_unref (handles, TP_HANDLE_TYPE_ROOM, priv->handle);

  g_free (priv->object_path);
  g_free (priv->self_jid);

  while ((msg = g_queue_pop_head (priv->pending_messages)))
    {
      _gabble_muc_pending_free (msg);
    }

  g_queue_free (priv->pending_messages);

  G_OBJECT_CLASS (gabble_muc_channel_parent_class)->finalize (object);
}

/**
 * _gabble_muc_pending_get_alloc
 *
 * Returns a GabbleAllocator for creating up to 256 pending messages, but no
 * more.
 */
static GabbleAllocator *
_gabble_muc_pending_get_alloc ()
{
  static GabbleAllocator *alloc = NULL;

  if (alloc == NULL)
    alloc = gabble_allocator_new (sizeof(GabbleMucPendingMessage), MAX_PENDING_MESSAGES);

  return alloc;
}

#define _gabble_muc_pending_new() \
  (ga_new (_gabble_muc_pending_get_alloc (), GabbleMucPendingMessage))
#define _gabble_muc_pending_new0() \
  (ga_new0 (_gabble_muc_pending_get_alloc (), GabbleMucPendingMessage))

/**
 * _gabble_muc_pending_free
 *
 * Free up a GabbleMucPendingMessage struct.
 */
static void _gabble_muc_pending_free (GabbleMucPendingMessage *msg)
{
  if (msg->text)
    g_free (msg->text);

  gabble_allocator_free (_gabble_muc_pending_get_alloc (), msg);
}

/**
 * _gabble_muc_channel_member_presence_updated
 */
void
_gabble_muc_channel_member_presence_updated (GabbleMucChannel *chan,
                                             GabbleHandle handle,
                                             LmMessageNode *pres_node)
{
  GabbleMucChannelPrivate *priv;
  GQuark data_key;
  ContactPresence *cp;
  GIntSet *empty, *set;
  GabbleGroupMixin *mixin;

  g_debug (G_STRFUNC);

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  mixin = GABBLE_GROUP_MIXIN (chan);

  /* get presence */
  data_key = _get_contact_presence_quark ();
  cp = gabble_handle_get_qdata (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT,
                                handle, data_key);

  /* update channel members according to presence */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, handle);

  if (cp->presence_id != GABBLE_PRESENCE_OFFLINE)
    {
      if (!handle_set_is_member (mixin->members, handle))
        {
          gabble_group_mixin_change_members (G_OBJECT (chan), "", set, empty,
                                             empty, empty);
        }
    }
  else
    {
      gabble_group_mixin_change_members (G_OBJECT (chan), "", empty, set,
                                         empty, empty);
    }

  g_intset_destroy (empty);
  g_intset_destroy (set);
}

/**
 * _gabble_muc_channel_receive
 */
gboolean
_gabble_muc_channel_receive (GabbleMucChannel *chan,
                             TpChannelTextMessageType type,
                             GabbleHandle sender,
                             time_t timestamp,
                             const gchar *text,
                             LmMessageNode *msg_node)
{
  GabbleMucChannelPrivate *priv;
  GabbleMucPendingMessage *msg;
  gsize len;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (sender == priv->handle)
    {
      g_debug ("%s: ignoring message from the channel itself, support "
               "for subject etc. needs a Telepathy interface",
               G_STRFUNC);
      return TRUE;
    }

  msg = _gabble_muc_pending_new0 ();

  if (msg == NULL)
    {
      g_debug ("%s: no more pending messages available, giving up", G_STRFUNC);

      /* TODO: something clever here */

      return FALSE;
    }

  len = strlen (text);

  if (len > MAX_MESSAGE_SIZE)
    {
      g_debug ("%s: message exceeds maximum size, truncating", G_STRFUNC);

      /* TODO: something clever here */

      len = MAX_MESSAGE_SIZE;
    }

  msg->text = g_try_malloc (len + 1);

  if (msg->text == NULL)
    {
      g_debug ("%s: unable to allocate message, giving up", G_STRFUNC);

      _gabble_muc_pending_free (msg);

      /* TODO: something clever here */

      return FALSE;
    }

  g_strlcpy (msg->text, text, len + 1);

  msg->id = priv->recv_id++;
  msg->timestamp = timestamp;
  msg->sender = sender;
  msg->type = type;

  g_queue_push_tail (priv->pending_messages, msg);

  g_signal_emit (chan, signals[RECEIVED], 0,
                 msg->id,
                 msg->timestamp,
                 msg->sender,
                 msg->type,
                 msg->text);

  g_debug ("%s: queued message %u", G_STRFUNC, msg->id);

  return FALSE;
}

static gint
compare_pending_message (gconstpointer haystack,
                         gconstpointer needle)
{
  const GabbleMucPendingMessage *msg = haystack;
  guint id = GPOINTER_TO_UINT (needle);

  return (msg->id != id);
}

/**
 * gabble_muc_channel_acknowledge_pending_message
 *
 * Implements DBus method AcknowledgePendingMessage
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_acknowledge_pending_message (GabbleMucChannel *obj, guint id, GError **error)
{
  GabbleMucChannelPrivate *priv;
  GList *node;
  GabbleMucPendingMessage *msg;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  node = g_queue_find_custom (priv->pending_messages,
                              GUINT_TO_POINTER (id),
                              compare_pending_message);

  if (node == NULL)
    {
      g_debug ("%s: invalid message id %u", G_STRFUNC, id);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid message id %u", id);

      return FALSE;
    }

  msg = node->data;

  g_debug ("%s: acknowleding message id %u", G_STRFUNC, id);

  g_queue_remove (priv->pending_messages, msg);

  _gabble_muc_pending_free (msg);

  return TRUE;
}


/**
 * gabble_muc_channel_add_members
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
gboolean gabble_muc_channel_add_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_add_members (G_OBJECT (obj), contacts, message, error);
}


/**
 * gabble_muc_channel_close
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
gboolean gabble_muc_channel_close (GabbleMucChannel *obj, GError **error)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  priv->closed = TRUE;

  g_debug ("%s called on %p", G_STRFUNC, obj);
  g_signal_emit(obj, signals[CLOSED], 0);

  return TRUE;
}


/**
 * gabble_muc_channel_get_channel_type
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
gboolean gabble_muc_channel_get_channel_type (GabbleMucChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_TEXT);

  return TRUE;
}


/**
 * gabble_muc_channel_get_group_flags
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
gboolean gabble_muc_channel_get_group_flags (GabbleMucChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_group_flags (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_handle
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
gboolean gabble_muc_channel_get_handle (GabbleMucChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_ROOM;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_muc_channel_get_interfaces
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
gboolean gabble_muc_channel_get_interfaces (GabbleMucChannel *obj, gchar *** ret, GError **error)
{
  const gchar *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
      NULL
  };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_muc_channel_get_local_pending_members
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
gboolean gabble_muc_channel_get_local_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_local_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_members
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
gboolean gabble_muc_channel_get_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_password
 *
 * Implements DBus method GetPassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_password (GabbleMucChannel *obj, gchar ** ret, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "not yet implemented");

  return FALSE;
}


/**
 * gabble_muc_channel_get_password_flags
 *
 * Implements DBus method GetPasswordFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_password_flags (GabbleMucChannel *obj, guint* ret, GError **error)
{
  /* FIXME */

  return TRUE;
}


/**
 * gabble_muc_channel_get_remote_pending_members
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
gboolean gabble_muc_channel_get_remote_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_remote_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_self_handle
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
gboolean gabble_muc_channel_get_self_handle (GabbleMucChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_self_handle (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_list_pending_messages (GabbleMucChannel *obj, GPtrArray ** ret, GError **error)
{
  GabbleMucChannelPrivate *priv;
  guint count;
  GPtrArray *messages;
  GList *cur;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  count = g_queue_get_length (priv->pending_messages);
  messages = g_ptr_array_sized_new (count);

  for (cur = g_queue_peek_head_link (priv->pending_messages);
       cur != NULL;
       cur = cur->next)
    {
      GabbleMucPendingMessage *msg = cur->data;
      GValue val = { 0, };

      g_value_init (&val, TP_TYPE_PENDING_MESSAGE_STRUCT);
      g_value_take_boxed (&val,
          dbus_g_type_specialized_construct (TP_TYPE_PENDING_MESSAGE_STRUCT));

      dbus_g_type_struct_set (&val,
                              0, msg->id,
                              1, msg->timestamp,
                              2, msg->sender,
                              3, msg->type,
                              4, msg->text,
                              G_MAXUINT);

      g_ptr_array_add (messages, g_value_get_boxed (&val));
    }

  *ret = messages;

  return TRUE;
}


/**
 * gabble_muc_channel_provide_password
 *
 * Implements DBus method ProvidePassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_provide_password (GabbleMucChannel *obj, const gchar * password, gboolean* ret, GError **error)
{
  /* FIXME */

  return TRUE;
}


/**
 * gabble_muc_channel_remove_members
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
gboolean gabble_muc_channel_remove_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_remove_members (G_OBJECT (obj), contacts, message, error);
}

/**
 * gabble_muc_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_send (GabbleMucChannel *obj, guint type, const gchar * text, GError **error)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  gboolean result;
  time_t timestamp;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      g_debug ("%s: invalid message type %u", G_STRFUNC, type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid message type: %u", type);

      return FALSE;
    }

  /* TODO: send different message types */

  msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_MESSAGE,
                                      LM_MESSAGE_SUB_TYPE_GROUPCHAT);
  lm_message_node_add_child (msg->node, "body", text);

  result = _gabble_connection_send (priv->conn, msg, error);
  lm_message_unref (msg);

  if (!result)
    return FALSE;

  timestamp = time (NULL);

  g_signal_emit (obj, signals[SENT], 0,
                 timestamp,
                 type,
                 text);

  return TRUE;
}


/**
 * gabble_muc_channel_set_password
 *
 * Implements DBus method SetPassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_set_password (GabbleMucChannel *obj, const gchar * password, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "not yet implemented");

  return FALSE;
}


static gboolean
gabble_muc_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "not yet implemented");

  return FALSE;
}

static gboolean
gabble_muc_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "not yet implemented");

  return FALSE;
}

