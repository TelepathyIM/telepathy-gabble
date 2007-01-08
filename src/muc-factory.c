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

#define DEBUG_FLAG GABBLE_DEBUG_MUC

#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "gabble-presence-cache.h"
#include "gabble-muc-channel.h"
#include "gabble-roomlist-channel.h"
#include "handles.h"
#include "muc-factory.h"
#include "namespaces.h"
#include <telepathy-glib/interfaces.h>
#include "text-mixin.h"
#include <telepathy-glib/channel-factory-iface.h>
#include "util.h"

static void gabble_muc_factory_iface_init (gpointer g_iface, gpointer iface_data);

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
  GabbleRoomlistChannel *roomlist_channel;

  GHashTable *disco_requests;

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

  priv->disco_requests = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                NULL, NULL);

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
  /* GabbleMucFactoryPrivate *priv; */

  obj = G_OBJECT_CLASS (gabble_muc_factory_parent_class)->
           constructor (type, n_props, props);
  /* priv = GABBLE_MUC_FACTORY_GET_PRIVATE (obj); */

  return obj;
}

static void
cancel_disco_request (gpointer key, gpointer value, gpointer user_data)
{
  GabbleDisco *disco = GABBLE_DISCO (user_data);
  GabbleDiscoRequest *request = (GabbleDiscoRequest *) key;

  gabble_disco_cancel_request (disco, request);
}

static void
gabble_muc_factory_dispose (GObject *object)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));

  g_hash_table_foreach (priv->disco_requests, cancel_disco_request, priv->conn->disco);
  g_hash_table_destroy (priv->disco_requests);

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


static GabbleMucChannel *
get_muc_from_jid (GabbleMucFactory *fac, const gchar *jid)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandle handle;
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
  TpHandle room_handle;

  if (priv->channels != NULL)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      DEBUG ("removing MUC channel with handle %d", room_handle);

      g_hash_table_remove (priv->channels, GINT_TO_POINTER (room_handle));
    }
}

static void
muc_ready_cb (GabbleMucChannel *chan,
              gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);

  DEBUG ("chan=%p", chan);

  g_signal_emit_by_name (fac, "new-channel", chan);
}

static void
muc_join_error_cb (GabbleMucChannel *chan,
                   GError *error,
                   gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);

  DEBUG ("error->code=%u, error->message=\"%s\"", error->code, error->message);

  g_signal_emit_by_name (fac, "channel-error", chan, error);
}

/**
 * new_muc_channel
 */
static GabbleMucChannel *
new_muc_channel (GabbleMucFactory *fac, TpHandle handle, gboolean invite_self)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleMucChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)) == NULL);

  object_path = g_strdup_printf ("%s/MucChannel%u", priv->conn->object_path, handle);

  DEBUG ("creating new chan, object path %s", object_path);

  chan = g_object_new (GABBLE_TYPE_MUC_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "invite-self", invite_self,
                       NULL);

  g_signal_connect (chan, "closed", (GCallback) muc_channel_closed_cb, fac);

  g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

  g_free (object_path);

  g_signal_connect (chan, "ready", G_CALLBACK (muc_ready_cb), fac);
  g_signal_connect (chan, "join-error", G_CALLBACK (muc_join_error_cb),
                    fac);

  return chan;
}


struct DiscoInviteData {
    GabbleMucFactory *factory;
    gchar *reason;
    TpHandle inviter;
};

/**
 * obsolete_invite_disco_cb:
 *
 * Callback for disco request we fired upon encountering obsolete disco.
 * If the object is in fact MUC room, create a channel for it.
 */
static void
obsolete_invite_disco_cb (GabbleDisco *self,
                          GabbleDiscoRequest *request,
                          const gchar *jid,
                          const gchar *node,
                          LmMessageNode *query_result,
                          GError* error,
                          gpointer user_data)
{
  struct DiscoInviteData *data = (struct DiscoInviteData *) user_data;

  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data->factory);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  LmMessageNode *identity;
  const char *category, *type;
  TpHandle handle;

  g_hash_table_remove (priv->disco_requests, request);

  identity = lm_message_node_get_child (query_result, "identity");
  if (NULL == identity)
    return;

  category = lm_message_node_get_attribute (identity, "category");
  if (NULL == category)
    return;

  type = lm_message_node_get_attribute (identity, "type");
  if (NULL == type)
    return;

  if (0 != strcmp (category, "conference") ||
      0 != strcmp (type, "text"))
    {
      DEBUG ("obsolete invite request specified invalid jid '%s', ignoring", jid);
    }

  /* OK, it's MUC after all, create a new channel */
  handle = gabble_handle_for_room (priv->conn->handles, jid);

  if (g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)) == NULL)
    {
      GabbleMucChannel *chan;
      chan = new_muc_channel (fac, handle, FALSE);
      _gabble_muc_channel_handle_invited (chan, data->inviter, data->reason);
    }
  else
    {
      DEBUG ("ignoring invite to a room '%s' we're already in", jid);
    }

  g_free (data->reason);
  g_free (data);
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
  TpHandleType handle_type;
  TpHandle room_handle, handle;
  GabbleMucChannel *chan;
  GabbleTextMixinSendError send_error;

  if (!gabble_text_mixin_parse_incoming_message (message, &from, &stamp,
        &msgtype, &body, &body_offset, &send_error))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* does it have a muc subnode? */
  node = lm_message_node_get_child_with_namespace (message->node, "x",
      NS_MUC_USER);
  if (node != NULL)
    {
      /* and an invitation? */
      node = lm_message_node_get_child (node, "invite");
      if (node != NULL)
        {
          LmMessageNode *reason_node;
          const gchar *invite_from, *reason;
          TpHandle inviter_handle;

          if (send_error != CHANNEL_TEXT_SEND_NO_ERROR)
            {
              NODE_DEBUG (message->node, "got a MUC invitation message "
                             "with a send error; ignoring");

              return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
            }

          invite_from = lm_message_node_get_attribute (node, "from");
          if (invite_from == NULL)
            {
              NODE_DEBUG (message->node, "got a MUC invitation message "
                             "without a from field on the invite node, "
                             "ignoring");

              return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
            }

          inviter_handle = gabble_handle_for_contact (priv->conn->handles,
                                                      invite_from, FALSE);

          reason_node = lm_message_node_get_child (node, "reason");
          if (reason_node != NULL)
            {
              reason = lm_message_node_get_value (reason_node);
            }
          else
            {
              reason = "";
              NODE_DEBUG (message->node, "no MUC invite reason specified");
            }

          /* create the channel */
          handle = gabble_handle_for_room (priv->conn->handles, from);

          if (g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)) == NULL)
            {
              chan = new_muc_channel (fac, handle, FALSE);
              _gabble_muc_channel_handle_invited (chan, inviter_handle, reason);
            }
          else
            {
              NODE_DEBUG (message->node, "ignoring invite to a room we're already in");
            }

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }
  else
    {
      TpHandle inviter_handle;
      GabbleDiscoRequest *request;
      const gchar *reason;
      struct DiscoInviteData *disco_udata;

      /* check for obsolete invite method */
      for (node = message->node->children; node != NULL; node = node->next)
        if (strcmp (node->name, "x") == 0)
          if (lm_message_node_has_namespace (node, NS_X_CONFERENCE, NULL))
            break;

      if (node == NULL)
        goto HANDLE_MESSAGE;

      /* the room JID is in x */
      from = lm_message_node_get_attribute (node, "jid");
      if (from == NULL)
        return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

      /* the inviter JID is in "from" */
      inviter_handle = gabble_handle_for_contact (priv->conn->handles,
                                                  from, FALSE);

      /* reason is the body */
      reason = body;

      disco_udata = g_new0 (struct DiscoInviteData, 1);
      disco_udata->factory = fac;
      disco_udata->reason = g_strdup (reason);
      disco_udata->inviter = inviter_handle;

      NODE_DEBUG (message->node, "received obsolete invite method");

      request = gabble_disco_request (priv->conn->disco, GABBLE_DISCO_TYPE_INFO,
          from, NULL, obsolete_invite_disco_cb, disco_udata, G_OBJECT (fac), NULL);

      if (request != NULL)
        g_hash_table_insert (priv->disco_requests, request, NULL);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

HANDLE_MESSAGE:

  /* check if a room with the jid exists */
  if (!gabble_handle_for_room_exists (priv->conn->handles, from, TRUE))
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  room_handle = gabble_handle_for_room (priv->conn->handles, from);

  /* find the MUC channel */
  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (room_handle));

  if (chan == NULL)
    {
      g_warning ("%s: ignoring groupchat message from known handle with "
                 "no MUC channel", G_STRFUNC);

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* get the handle of the sender, which is either the room
   * itself or one of its members */
  if (gabble_handle_for_room_exists (priv->conn->handles, from, FALSE))
    {
      handle_type = TP_HANDLE_TYPE_ROOM;
      handle = room_handle;
    }
  else
    {
      handle_type = TP_HANDLE_TYPE_CONTACT;
      handle = gabble_handle_for_contact (priv->conn->handles, from, TRUE);
    }

  if (send_error != CHANNEL_TEXT_SEND_NO_ERROR)
    {
      _gabble_text_mixin_send_error_signal (G_OBJECT (chan), send_error, stamp,
          msgtype, body_offset);
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  if (_gabble_muc_channel_receive (chan, msgtype, handle_type, handle, stamp,
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
      NODE_DEBUG (msg->node, "presence stanza without from attribute, ignoring");
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

  x_node = lm_message_node_get_child_with_namespace (msg->node, "x", NS_MUC_USER);

  /* is it a MUC member presence? */
  if (x_node != NULL)
    {
      if (muc_chan != NULL)
        {
          TpHandle handle;

          handle = gabble_handle_for_contact (priv->conn->handles, from, TRUE);
          if (handle == 0)
            {
              NODE_DEBUG (msg->node, "discarding MUC presence from malformed jid");
              return LM_HANDLER_RESULT_REMOVE_MESSAGE;
            }

          _gabble_muc_channel_member_presence_updated (muc_chan, handle,
                                                       msg, x_node);
        }
      else
        {
          NODE_DEBUG (msg->node, "discarding unexpected MUC member presence");

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

}

static void
roomlist_channel_closed_cb (GabbleRoomlistChannel *chan, gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  if (priv->roomlist_channel != NULL)
    {
      g_object_unref (priv->roomlist_channel);
      priv->roomlist_channel = NULL;
    }
}

static gboolean
make_roomlist_channel (GabbleMucFactory *fac)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  if (priv->roomlist_channel == NULL)
    {
      const gchar *server;
      gchar *object_path;

      server = _gabble_connection_find_conference_server (priv->conn);

      if (server == NULL)
        return FALSE;

      object_path = g_strdup_printf ("%s/RoomlistChannel", priv->conn->object_path);

      priv->roomlist_channel = _gabble_roomlist_channel_new (priv->conn,
          object_path, server);

      g_signal_connect (priv->roomlist_channel, "closed",
                        (GCallback) roomlist_channel_closed_cb, fac);

      g_signal_emit_by_name (fac, "new-channel", priv->roomlist_channel);

      g_free (object_path);
    }

  return TRUE;
}



static void
gabble_muc_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  DEBUG ("closing channels");

  if (priv->channels != NULL)
    {
      GHashTable *tmp = priv->channels;
      priv->channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (priv->roomlist_channel != NULL)
    {
      GObject *tmp = G_OBJECT (priv->roomlist_channel);
      priv->roomlist_channel = NULL;
      g_object_unref (tmp);
    }
}

static void
gabble_muc_factory_iface_connecting (TpChannelFactoryIface *iface)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  DEBUG ("adding callbacks");

  g_assert (priv->message_cb == NULL);
  g_assert (priv->presence_cb == NULL);

  priv->message_cb = lm_message_handler_new (muc_factory_message_cb, fac, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->message_cb,
                                          LM_MESSAGE_TYPE_MESSAGE,
                                          LM_HANDLER_PRIORITY_NORMAL);

  priv->presence_cb = lm_message_handler_new (muc_factory_presence_cb, fac, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->presence_cb,
                                          LM_MESSAGE_TYPE_PRESENCE,
                                          LM_HANDLER_PRIORITY_NORMAL);
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

  DEBUG ("removing callbacks");

  g_assert (priv->message_cb != NULL);
  g_assert (priv->presence_cb != NULL);

  lm_connection_unregister_message_handler (priv->conn->lmconn, priv->message_cb,
                                            LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->message_cb);
  priv->message_cb = NULL;

  lm_connection_unregister_message_handler (priv->conn->lmconn, priv->presence_cb,
                                            LM_MESSAGE_TYPE_PRESENCE);
  lm_message_handler_unref (priv->presence_cb);
  priv->presence_cb = NULL;
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

  if (priv->roomlist_channel != NULL)
    foreach (TP_CHANNEL_IFACE (priv->roomlist_channel), user_data);
}

static TpChannelFactoryRequestStatus
gabble_muc_factory_iface_request (TpChannelFactoryIface *iface,
                                  const gchar *chan_type,
                                  TpHandleType handle_type,
                                  guint handle,
                                  TpChannelIface **ret,
                                  GError **error)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleMucChannel *chan;

  if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    {
      /* FIXME - delay if services aren't discovered yet? */
      if (!make_roomlist_channel (fac))
        {
          DEBUG ("no conference server available for roomlist request");
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
        }
      *ret = TP_CHANNEL_IFACE (priv->roomlist_channel);
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE;
    }

  if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_ROOM)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!tp_handle_is_valid (priv->conn->handle_repos[TP_HANDLE_TYPE_ROOM],
        handle, NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));
  if (!chan)
    {
      chan = new_muc_channel (fac, handle, TRUE);
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
    }

  if (_gabble_muc_channel_is_ready (chan))
    {
      *ret = TP_CHANNEL_IFACE (chan);
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE;
    }
  else
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
    }
}

static void
gabble_muc_factory_iface_init (gpointer g_iface,
                              gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_muc_factory_iface_close_all;
  klass->connecting = gabble_muc_factory_iface_connecting;
  klass->connected = gabble_muc_factory_iface_connected;
  klass->disconnected = gabble_muc_factory_iface_disconnected;
  klass->foreach = gabble_muc_factory_iface_foreach;
  klass->request = gabble_muc_factory_iface_request;
}


