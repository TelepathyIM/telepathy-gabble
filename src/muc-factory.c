/*
 * muc-factory.c - Source for GabbleMucFactory
 * Copyright (C) 2006 Collabora Ltd.
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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#include "disco.h"
#include "gabble-connection.h"
#include "gabble-presence-cache.h"
#include "gabble-muc-channel.h"
#include "handles.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "telepathy-interfaces.h"
#include "text-mixin.h"
#include "tp-channel-factory-iface.h"

static void gabble_muc_factory_iface_init (gpointer g_iface, gpointer iface_data);
static LmHandlerResult muc_factory_message_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult muc_factory_presence_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);


G_DEFINE_TYPE_WITH_CODE (GabbleMucFactory, gabble_muc_factory, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, gabble_muc_factory_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleMucFactoryPrivate GabbleMucFactoryPrivate;
struct _GabbleMucFactoryPrivate
{
  GabbleConnection *conn;

  LmMessageHandler *message_cb;
  LmMessageHandler *presence_cb;

  GHashTable *channels;

  gboolean dispose_has_run;
};

#define GABBLE_MUC_FACTORY_GET_PRIVATE(o)    (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MUC_FACTORY, GabbleMucFactoryPrivate))

static GObject *gabble_muc_factory_constructor (GType type, guint n_props, GObjectConstructParam *props);

static void
gabble_muc_factory_init (GabbleMucFactory *fac)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                          NULL, g_object_unref);

  priv->message_cb = NULL;
  priv->presence_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

static GObject *
gabble_muc_factory_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMucFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_muc_factory_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MUC_FACTORY_GET_PRIVATE (obj);

  g_assert(priv->conn != NULL);
  g_assert(priv->conn->lmconn != NULL);

  priv->message_cb = lm_message_handler_new (muc_factory_message_cb, obj, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->message_cb,
                                          LM_MESSAGE_TYPE_MESSAGE,
                                          LM_HANDLER_PRIORITY_NORMAL);

  priv->presence_cb = lm_message_handler_new (muc_factory_presence_cb, obj, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->presence_cb,
                                          LM_MESSAGE_TYPE_PRESENCE,
                                          LM_HANDLER_PRIORITY_NORMAL);
  return obj;
}


static void
gabble_muc_factory_dispose (GObject *object)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  g_debug ("%s: dispose called", G_STRFUNC);
  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));

  if (G_OBJECT_CLASS (gabble_muc_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_factory_parent_class)->dispose (object);
}

static void
gabble_muc_factory_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_muc_factory_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_muc_factory_class_init (GabbleMucFactoryClass *gabble_muc_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_muc_factory_class, sizeof (GabbleMucFactoryPrivate));

  object_class->constructor = gabble_muc_factory_constructor;
  object_class->dispose = gabble_muc_factory_dispose;

  object_class->get_property = gabble_muc_factory_get_property;
  object_class->set_property = gabble_muc_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "MUC factory object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static LmMessageNode *
_get_muc_node (LmMessageNode *toplevel_node)
{
  LmMessageNode *node;

  for (node = toplevel_node->children; node; node = node->next)
    if (strcmp (node->name, "x") == 0)
      if (_lm_message_node_has_namespace (node, NS_MUC_USER))
        return node;

  return NULL;
}


static GabbleMucChannel *
get_muc_from_jid (GabbleMucFactory *fac, const gchar *jid)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleHandle handle;
  GabbleMucChannel *chan = NULL;

  if (gabble_handle_for_room_exists (priv->conn->handles, jid, TRUE))
    {
      handle = gabble_handle_for_room (priv->conn->handles, jid);

      chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));
    }

  return chan;
}


/**
 * muc_channel_closed_cb:
 *
 * Signal callback for when a MUC channel is closed. Removes the references
 * that MucFactory holds to them.
 */
static void
muc_channel_closed_cb (GabbleMucChannel *chan, gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleHandle room_handle;

  if (priv->channels)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      g_debug ("%s: removing MUC channel with handle %d", G_STRFUNC,
          room_handle);

      g_hash_table_remove (priv->channels, GINT_TO_POINTER (room_handle));
    }
}

/**
 * new_muc_channel
 */
static GabbleMucChannel *
new_muc_channel (GabbleMucFactory *fac, GabbleHandle handle)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleMucChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)) == NULL);

  object_path = g_strdup_printf ("%s/MucChannel%u", priv->conn->object_path, handle);

  g_debug ("new_muc_channel: creating new chan, object path %s", object_path);

  chan = g_object_new (GABBLE_TYPE_MUC_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  g_signal_connect (chan, "closed", (GCallback) muc_channel_closed_cb, fac);

  g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

  g_signal_emit_by_name (fac, "new-channel", chan);

  g_free (object_path);

  return chan;
}


/**
 * muc_factory_message_cb:
 *
 * Called by loudmouth when we get an incoming <message>.
 * We filter only groupchat and MUC messages, ignoring the rest.
 */
static LmHandlerResult
muc_factory_message_cb (LmMessageHandler *handler,
                        LmConnection *connection,
                        LmMessage *message,
                        gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  const gchar *from, *body, *body_offset;
  time_t stamp;
  TpChannelTextMessageType msgtype;
  LmMessageNode *node;
  GabbleHandle room_handle, handle;
  GabbleMucChannel *chan;

  if (!gabble_text_mixin_parse_incoming_message (message, &from, &stamp, &msgtype, &body, &body_offset))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* does it have a muc subnode? */
  node = _get_muc_node (message->node);
  if (node)
    {
      /* and an invitation? */
      node = lm_message_node_get_child (node, "invite");
      if (node)
        {
          LmMessageNode *reason_node;
          const gchar *invite_from, *reason;
          GabbleHandle inviter_handle;

          invite_from = lm_message_node_get_attribute (node, "from");
          if (invite_from == NULL)
            {
              HANDLER_DEBUG (message->node, "got a MUC invitation message "
                             "without a from field on the invite node, "
                             "ignoring");

              return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
            }

          inviter_handle = gabble_handle_for_contact (priv->conn->handles,
                                                      invite_from, FALSE);

          reason_node = lm_message_node_get_child (node, "reason");
          if (reason_node)
            {
              reason = lm_message_node_get_value (reason_node);
            }
          else
            {
              reason = "";
              HANDLER_DEBUG (message->node, "no MUC invite reason specified");
            }

          /* create the channel */
          handle = gabble_handle_for_room (priv->conn->handles, from);

          if (g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)) == NULL)
            {
              chan = new_muc_channel (fac, handle);
              _gabble_muc_channel_handle_invited (chan, inviter_handle, reason);
            }
          else
            {
              HANDLER_DEBUG (message->node, "ignoring invite to a room we're already in");
            }

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  /* check if a room with the jid exists */
  if (!gabble_handle_for_room_exists (priv->conn->handles, from, TRUE))
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  room_handle = gabble_handle_for_room (priv->conn->handles, from);

  /* find the MUC channel */
  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (room_handle));

  if (!chan)
    {
      g_warning ("%s: ignoring groupchat message from known handle with "
                 "no MUC channel", G_STRFUNC);

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* get the handle of the sender, which is either the room
   * itself or one of its members */
  if (gabble_handle_for_room_exists (priv->conn->handles, from, FALSE))
    {
      handle = room_handle;
    }
  else
    {
      handle = gabble_handle_for_contact (priv->conn->handles, from, TRUE);
    }

  if (_gabble_muc_channel_receive (chan, msgtype, handle, stamp,
                                   body_offset, message))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}


/**
 * connection_presence_muc_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
muc_factory_presence_cb (LmMessageHandler *handler,
                            LmConnection *lmconn,
                            LmMessage *msg,
                            gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  const char *from;
  LmMessageSubType sub_type;
  GabbleMucChannel *muc_chan;
  LmMessageNode *x_node;

  g_assert (lmconn == priv->conn->lmconn);

  from = lm_message_node_get_attribute (msg->node, "from");

  if (from == NULL)
    {
      HANDLER_DEBUG (msg->node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (msg);

  muc_chan = get_muc_from_jid (fac, from);

  /* is it an error and for a MUC? */
  if (sub_type == LM_MESSAGE_SUB_TYPE_ERROR
      && muc_chan != NULL)
    {
      _gabble_muc_channel_presence_error (muc_chan, from, msg->node);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  x_node = _get_muc_node (msg->node);

  /* is it a MUC member presence? */
  if (x_node)
    {
      if (muc_chan != NULL)
        {
          GabbleHandle handle;

          handle = gabble_handle_for_contact (priv->conn->handles, from, TRUE);
          if (handle == 0)
            {
              HANDLER_DEBUG (msg->node, "discarding MUC presence from malformed jid");
              return LM_HANDLER_RESULT_REMOVE_MESSAGE;
            }

          _gabble_muc_channel_member_presence_updated (muc_chan, handle,
                                                       msg, x_node);
        }
      else
        {
          HANDLER_DEBUG (msg->node, "discarding unexpected MUC member presence");

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

}

static void
gabble_muc_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  g_debug ("%s: closing channels", G_STRFUNC);

  if (priv->channels)
    {
      GHashTable *tmp = priv->channels;
      priv->channels = NULL;
      g_hash_table_destroy (tmp);
    }
}

static void
gabble_muc_factory_iface_connected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
gabble_muc_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  g_debug ("%s: removing callbacks", G_STRFUNC);

  if (priv->message_cb)
    {
      lm_connection_unregister_message_handler (priv->conn->lmconn, priv->message_cb,
                                                LM_MESSAGE_TYPE_MESSAGE);
      priv->message_cb = NULL;
    }

  if (priv->presence_cb)
    {
      lm_connection_unregister_message_handler (priv->conn->lmconn, priv->presence_cb,
                                                LM_MESSAGE_TYPE_PRESENCE);
      priv->presence_cb = NULL;
    }
}

struct _ForeachData
{
  TpChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);

  data->foreach (chan, data->user_data);
}

static void
gabble_muc_factory_iface_foreach (TpChannelFactoryIface *iface, TpChannelFunc foreach, gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
gabble_muc_factory_iface_request (TpChannelFactoryIface *iface,
                                  const gchar *chan_type,
                                  TpHandleType handle_type,
                                  guint handle,
                                  TpChannelIface **ret)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleMucChannel *chan;
  GArray *members;
  GError *error;
  gboolean retval;

  if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_ROOM)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!gabble_handle_is_valid (priv->conn->handles, TP_HANDLE_TYPE_ROOM, handle, NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));
  if (!chan)
    {
      chan = new_muc_channel (fac, handle);

      members = g_array_sized_new (FALSE, FALSE, sizeof (GabbleHandle), 1);
      g_array_append_val (members, priv->conn->self_handle);

      retval = gabble_group_mixin_add_members (G_OBJECT (chan), members, "", &error);

      g_array_free (members, TRUE);

      if (!retval)
        {
          GError *close_err;

          if (!gabble_muc_channel_close (GABBLE_MUC_CHANNEL (chan), &close_err))
            {
              g_error_free (close_err);
            }

          g_debug ("%s: error while adding self to group mixin", G_STRFUNC);
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
        }
    }

  *ret = TP_CHANNEL_IFACE (chan);
  return TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE;
}

static void
gabble_muc_factory_iface_init (gpointer g_iface,
                              gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_muc_factory_iface_close_all;
  klass->connected = gabble_muc_factory_iface_connected;
  klass->disconnected = gabble_muc_factory_iface_disconnected;
  klass->foreach = gabble_muc_factory_iface_foreach;
  klass->request = gabble_muc_factory_iface_request;
}


