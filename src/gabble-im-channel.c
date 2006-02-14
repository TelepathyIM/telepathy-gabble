/*
 * gabble-im-channel.c - Source for GabbleIMChannel
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
#include <loudmouth/loudmouth.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "allocator.h"
#include "gabble-connection.h"
#include "handles.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-im-channel.h"
#include "gabble-im-channel-glue.h"
#include "gabble-im-channel-signals-marshal.h"

#define MAX_PENDING_MESSAGES 256
#define MAX_MESSAGE_SIZE 8*1024 - 1

G_DEFINE_TYPE(GabbleIMChannel, gabble_im_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
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
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleIMChannelPrivate GabbleIMChannelPrivate;

struct _GabbleIMChannelPrivate
{
  GabbleConnection *connection;
  char *object_path;
  GabbleHandle handle;

  guint recv_id;
  GQueue *pending_messages;

  gboolean closed;
  gboolean dispose_has_run;
};

/* pending message */
typedef struct _GabbleIMPendingMessage GabbleIMPendingMessage;

struct _GabbleIMPendingMessage
{
  guint id;
  time_t timestamp;
  GabbleHandle sender;
  TpChannelTextMessageType type;
  char *text;
};

#define GABBLE_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_IM_CHANNEL, GabbleIMChannelPrivate))

static void
gabble_im_channel_init (GabbleIMChannel *obj)
{
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);

  priv->pending_messages = g_queue_new ();
}

static GObject *
gabble_im_channel_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  GabbleIMChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  gboolean valid;

  obj = G_OBJECT_CLASS (gabble_im_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (GABBLE_IM_CHANNEL (obj));

  handles = _gabble_connection_get_handles (priv->connection);
  valid = gabble_handle_ref (handles, TP_HANDLE_TYPE_CONTACT, priv->handle);
  g_assert (valid);

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_im_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
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
gabble_im_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
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


static void gabble_im_channel_dispose (GObject *object);
static void gabble_im_channel_finalize (GObject *object);

static void
gabble_im_channel_class_init (GabbleIMChannelClass *gabble_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_im_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_im_channel_class, sizeof (GabbleIMChannelPrivate));

  object_class->constructor = gabble_im_channel_constructor;

  object_class->get_property = gabble_im_channel_get_property;
  object_class->set_property = gabble_im_channel_set_property;

  object_class->dispose = gabble_im_channel_dispose;
  object_class->finalize = gabble_im_channel_finalize;

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
                  G_OBJECT_CLASS_TYPE (gabble_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_im_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (gabble_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_im_channel_marshal_VOID__INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (gabble_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_im_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_im_channel_class), &dbus_glib_gabble_im_channel_object_info);
}

void
gabble_im_channel_dispose (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    g_signal_emit(self, signals[CLOSED], 0);

  if (G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose (object);
}

static void _gabble_im_pending_free (GabbleIMPendingMessage *msg);

void
gabble_im_channel_finalize (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);
  GabbleIMPendingMessage *msg;
  GabbleHandleRepo *handles;

  /* free any data held directly by the object here */

  handles = _gabble_connection_get_handles (priv->connection);
  gabble_handle_unref (handles, TP_HANDLE_TYPE_CONTACT, priv->handle);

  g_free (priv->object_path);

  while ((msg = g_queue_pop_head(priv->pending_messages)))
    {
      _gabble_im_pending_free (msg);
    }

  g_queue_free (priv->pending_messages);

  G_OBJECT_CLASS (gabble_im_channel_parent_class)->finalize (object);
}

/**
 * _gabble_im_pending_get_alloc
 *
 * Returns a GabbleAllocator for creating up to 256 pending messages, but no
 * more.
 */
static GabbleAllocator *
_gabble_im_pending_get_alloc ()
{
  static GabbleAllocator *alloc = NULL;

  if (alloc == NULL)
    alloc = gabble_allocator_new (sizeof(GabbleIMPendingMessage), MAX_PENDING_MESSAGES);

  return alloc;
}

#define _gabble_im_pending_new() \
  (ga_new (_gabble_im_pending_get_alloc (), GabbleIMPendingMessage))
#define _gabble_im_pending_new0() \
  (ga_new0 (_gabble_im_pending_get_alloc (), GabbleIMPendingMessage))

/**
 * _gabble_im_pending_free
 *
 * Free up a GabbleIMPendingMessage struct.
 */
static void _gabble_im_pending_free (GabbleIMPendingMessage *msg)
{
  if (msg->text)
    g_free (msg->text);

  gabble_allocator_free (_gabble_im_pending_get_alloc (), msg);
}

/**
 * _gabble_im_channel_receive
 *
 */
gboolean _gabble_im_channel_receive (GabbleIMChannel *chan,
                                     TpChannelTextMessageType type,
                                     GabbleHandle sender,
                                     time_t timestamp,
                                     const char *text)
{
  GabbleIMChannelPrivate *priv;
  GabbleIMPendingMessage *msg;
  gsize len;

  g_assert (GABBLE_IS_IM_CHANNEL (chan));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  msg = _gabble_im_pending_new0 ();

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

      _gabble_im_pending_free (msg);

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
  GabbleIMPendingMessage *msg = (GabbleIMPendingMessage *) haystack;
  guint id = GPOINTER_TO_INT (needle);

  return (msg->id != id);
}

/**
 * gabble_im_channel_acknowledge_pending_message
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
gboolean gabble_im_channel_acknowledge_pending_message (GabbleIMChannel *obj, guint id, GError **error)
{
  GabbleIMChannelPrivate *priv;
  GList *node;
  GabbleIMPendingMessage *msg;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);

  node = g_queue_find_custom (priv->pending_messages,
                              GINT_TO_POINTER (id),
                              compare_pending_message);

  if (node == NULL)
    {
      g_debug ("%s: invalid message id %u", G_STRFUNC, id);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid message id %u", id);

      return FALSE;
    }

  msg = (GabbleIMPendingMessage *) node->data;

  g_debug ("%s: acknowleding message id %u", G_STRFUNC, id);

  g_queue_remove (priv->pending_messages, msg);

  _gabble_im_pending_free (msg);

  return TRUE;
}


/**
 * gabble_im_channel_close
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
gboolean gabble_im_channel_close (GabbleIMChannel *obj, GError **error)
{
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);
  priv->closed = TRUE;

  g_debug ("%s called on %p", G_STRFUNC, obj);
  g_signal_emit(obj, signals[CLOSED], 0);

  return TRUE;
}


/**
 * gabble_im_channel_get_channel_type
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
gboolean gabble_im_channel_get_channel_type (GabbleIMChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_TEXT);

  return TRUE;
}


/**
 * gabble_im_channel_get_handle
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
gboolean gabble_im_channel_get_handle (GabbleIMChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_CONTACT;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_im_channel_get_interfaces
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
gboolean gabble_im_channel_get_interfaces (GabbleIMChannel *obj, gchar *** ret, GError **error)
{
  const char *interfaces[] = { NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_im_channel_list_pending_messages
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
gboolean gabble_im_channel_list_pending_messages (GabbleIMChannel *obj, GPtrArray ** ret, GError **error)
{
  GabbleIMChannelPrivate *priv;
  guint count;
  GPtrArray *messages;
  GList *cur;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);

  count = g_queue_get_length (priv->pending_messages);
  messages = g_ptr_array_sized_new (count);

  for (cur = g_queue_peek_head_link(priv->pending_messages);
       cur != NULL;
       cur = cur->next)
    {
      GabbleIMPendingMessage *msg = (GabbleIMPendingMessage *) cur->data;
      GValueArray *vals;

      vals = g_value_array_new (5);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), msg->id);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 1), msg->timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 2), msg->sender);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 3), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 3), msg->type);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 4), G_TYPE_STRING);
      g_value_set_string (g_value_array_get_nth (vals, 4), msg->text);

      g_ptr_array_add (messages, vals);
    }

  *ret = messages;

  return TRUE;
}


/**
 * gabble_im_channel_send
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
gboolean gabble_im_channel_send (GabbleIMChannel *obj, guint type, const gchar * text, GError **error)
{
  GabbleIMChannelPrivate *priv;
  LmMessage *msg;
  const char *recipient;
  gboolean result;
  time_t timestamp;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      g_debug ("%s: invalid message type %u", G_STRFUNC, type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid message type: %u", type);

      return FALSE;
    }

  /* TODO: send different message types */

  recipient = gabble_handle_inspect (
      _gabble_connection_get_handles (priv->connection),
      TP_HANDLE_TYPE_CONTACT,
      priv->handle);

  msg = lm_message_new (recipient, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_node_add_child (msg->node, "body", text);

  /* TODO: send with callback? */

  result = _gabble_connection_send (priv->connection, msg, error);
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

