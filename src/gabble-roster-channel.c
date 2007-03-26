/*
 * gabble-roster-channel.c - Source for GabbleRosterChannel
 * Copyright (C) 2005, 2006 Collabora Ltd.
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

#define DEBUG_FLAG GABBLE_DEBUG_ROSTER

#include "debug.h"
#include "gabble-connection.h"
#include <telepathy-glib/group-mixin.h>
#include "handles.h"
#include "roster.h"
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include "util.h"

#include "gabble-roster-channel.h"

static void channel_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleRosterChannel, gabble_roster_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_CONTACT_LIST, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleRosterChannelPrivate GabbleRosterChannelPrivate;

struct _GabbleRosterChannelPrivate
{
  GabbleConnection *conn;
  char *object_path;
  TpHandle handle;
  guint handle_type;

  gboolean dispose_has_run;
  gboolean closed;
};

#define GABBLE_ROSTER_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleRosterChannelPrivate *)obj->priv)

static void
gabble_roster_channel_init (GabbleRosterChannel *self)
{
  GabbleRosterChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannelPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static GObject *
gabble_roster_channel_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleRosterChannelPrivate *priv;
  DBusGConnection *bus;
  TpBaseConnection *conn;
  gboolean valid;
  TpHandle self_handle;
  guint handle_type;
  TpHandleRepoIface *handle_repo, *contact_repo;

  obj = G_OBJECT_CLASS (gabble_roster_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));
  conn = (TpBaseConnection *)priv->conn;
  handle_type = priv->handle_type;
  handle_repo = tp_base_connection_get_handles (conn, handle_type);
  contact_repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
  self_handle = conn->self_handle;

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  g_assert (handle_type == TP_HANDLE_TYPE_GROUP
      || handle_type == TP_HANDLE_TYPE_LIST);

  /* ref our list handle */
  tp_handle_ref (handle_repo, priv->handle);

  /* initialize group mixin */
  tp_group_mixin_init ((TpSvcChannelInterfaceGroup *)obj,
      G_STRUCT_OFFSET (GabbleRosterChannel, group),
      contact_repo,
      self_handle);

  if (handle_type == TP_HANDLE_TYPE_GROUP)
    {
      tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)obj,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD |
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }
  else if (handle_type != TP_HANDLE_TYPE_LIST)
    {
      g_assert_not_reached ();
    }
  /* magic contact lists from here down... */
  else if (GABBLE_LIST_HANDLE_PUBLISH == priv->handle)
    {
      tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)obj,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE,
          0);
    }
  else if (GABBLE_LIST_HANDLE_SUBSCRIBE == priv->handle)
    {
      tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)obj,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD |
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
          TP_CHANNEL_GROUP_FLAG_CAN_RESCIND |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND,
          0);
    }
  else if (GABBLE_LIST_HANDLE_KNOWN == priv->handle)
    {
      tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)obj,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }
  else if (GABBLE_LIST_HANDLE_DENY == priv->handle)
    {
      tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)obj,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD |
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }
  else
    {
      g_assert_not_reached ();
    }

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
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, priv->handle_type);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
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
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE_TYPE:
      priv->handle_type = g_value_get_uint (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_roster_channel_dispose (GObject *object);
static void gabble_roster_channel_finalize (GObject *object);

static gboolean _gabble_roster_channel_add_member_cb (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error);
static gboolean _gabble_roster_channel_remove_member_cb (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error);

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

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  tp_group_mixin_class_init ((TpSvcChannelInterfaceGroupClass *)object_class,
                             G_STRUCT_OFFSET (GabbleRosterChannelClass, group_class),
                             _gabble_roster_channel_add_member_cb,
                             _gabble_roster_channel_remove_member_cb);
}

void
gabble_roster_channel_dispose (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    tp_svc_channel_emit_closed ((TpSvcChannel *)object);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose (object);
}

void
gabble_roster_channel_finalize (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection *)priv->conn;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (conn,
      priv->handle_type);

  /* free any data held directly by the object here */

  g_free (priv->object_path);

  tp_handle_unref (handle_repo, priv->handle);

  tp_group_mixin_finalize ((TpSvcChannelInterfaceGroup *)object);

  G_OBJECT_CLASS (gabble_roster_channel_parent_class)->finalize (object);
}


static gboolean
_gabble_roster_channel_send_presence (GabbleRosterChannel *chan,
                                      LmMessageSubType sub_type,
                                      TpHandle handle,
                                      const gchar *status,
                                      GError **error)
{
  GabbleRosterChannelPrivate *priv;
  TpBaseConnection *conn;
  TpHandleRepoIface *repo;
  const char *contact;
  LmMessage *message;
  gboolean result;

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);
  conn = (TpBaseConnection *)priv->conn;
  repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
  contact = tp_handle_inspect (repo, handle);

  message = lm_message_new_with_sub_type (contact,
      LM_MESSAGE_TYPE_PRESENCE,
      sub_type);

  if (LM_MESSAGE_SUB_TYPE_SUBSCRIBE == sub_type)
    lm_message_node_add_own_nick (message->node, priv->conn);

  if (status != NULL && status[0] != '\0')
    lm_message_node_add_child (message->node, "status", status);

  result = _gabble_connection_send (priv->conn, message, error);

  lm_message_unref (message);

  return result;
}


/**
 * _gabble_roster_channel_add_member_cb
 *
 * Called by the group mixin to add one member.
 */
static gboolean
_gabble_roster_channel_add_member_cb (TpSvcChannelInterfaceGroup *obj,
                                      TpHandle handle,
                                      const gchar *message,
                                      GError **error)
{
  GabbleRosterChannelPrivate *priv;
  gboolean ret = FALSE;
#ifdef ENABLE_DEBUG
  TpHandleRepoIface *handle_repo, *contact_repo;
#endif

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));
#ifdef ENABLE_DEBUG
  handle_repo = tp_base_connection_get_handles ((TpBaseConnection *)priv->conn,
      priv->handle_type);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->conn, TP_HANDLE_TYPE_CONTACT);
#endif

  DEBUG ("called on %s with handle %u (%s) \"%s\"",
      tp_handle_inspect (handle_repo, priv->handle), handle,
      tp_handle_inspect (contact_repo, handle), message);

  if (TP_HANDLE_TYPE_GROUP == priv->handle_type)
    {
      ret = gabble_roster_handle_add_to_group (priv->conn->roster,
          handle, priv->handle, error);
    }
  else if (TP_HANDLE_TYPE_LIST != priv->handle_type)
    {
      g_assert_not_reached ();
      return FALSE;
    }
  /* "magic" contact lists, from here down... */
  /* publish list */
  else if (GABBLE_LIST_HANDLE_PUBLISH == priv->handle)
    {
      /* send <presence type="subscribed"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_SUBSCRIBED, handle, message, error);
    }
  /* subscribe list */
  else if (GABBLE_LIST_HANDLE_SUBSCRIBE == priv->handle)
    {
      /* add item to the roster (GTalk depends on this, clearing the H flag) */
      gabble_roster_handle_add (priv->conn->roster, handle, NULL);

      /* send <presence type="subscribe"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_SUBSCRIBE, handle, message, error);
    }
  /* deny list */
  else if (GABBLE_LIST_HANDLE_DENY == priv->handle)
    {
      /* block contact */
      ret = gabble_roster_handle_set_blocked (priv->conn->roster, handle, TRUE,
          error);
    }
  else
    {
      g_assert_not_reached ();
    }

  return ret;
}


/**
 * _gabble_roster_channel_remove_member_cb
 *
 * Called by the group mixin to remove one member.
 */
static gboolean
_gabble_roster_channel_remove_member_cb (TpSvcChannelInterfaceGroup *obj,
                                         TpHandle handle,
                                         const gchar *message,
                                         GError **error)
{
  GabbleRosterChannelPrivate *priv;
#ifdef ENABLE_DEBUG
  TpBaseConnection *conn;
  TpHandleRepoIface *handle_repo, *contact_repo;
#endif
  gboolean ret = FALSE;

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));
#ifdef ENABLE_DEBUG
  conn = (TpBaseConnection *)priv->conn;
  handle_repo = tp_base_connection_get_handles (conn, priv->handle_type);
  contact_repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
#endif

  DEBUG ("called on %s with handle %u (%s) \"%s\"",
      tp_handle_inspect (handle_repo, priv->handle), handle,
      tp_handle_inspect (contact_repo, handle), message);

  if (TP_HANDLE_TYPE_GROUP == priv->handle_type)
    {
      ret = gabble_roster_handle_remove_from_group (priv->conn->roster,
          handle, priv->handle, error);
    }
  else if (TP_HANDLE_TYPE_LIST != priv->handle_type)
    {
      g_assert_not_reached ();
      return FALSE;
    }
  /* "magic" contact lists, from here down... */
  /* publish list */
  else if (GABBLE_LIST_HANDLE_PUBLISH == priv->handle)
    {
      /* send <presence type="unsubscribed"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED, handle, message, error);

      /* remove it from local_pending here, because roster callback doesn't
         know if it can (subscription='none' is used both during request and
         when it's rejected) */
      if (tp_handle_set_is_member (GABBLE_ROSTER_CHANNEL (obj)->group.local_pending, handle))
        {
          TpIntSet *rem = tp_intset_new ();

          tp_intset_add (rem, handle);
          tp_group_mixin_change_members (obj, "", NULL, rem, NULL, NULL,
              0, 0);

          tp_intset_destroy (rem);
        }
    }
  /* subscribe list */
  else if (GABBLE_LIST_HANDLE_SUBSCRIBE == priv->handle)
    {
      /* send <presence type="unsubscribe"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE, handle, message, error);
    }
  /* known list */
  else if (GABBLE_LIST_HANDLE_KNOWN == priv->handle)
    {
      /* send roster subscription=remove IQ */
      ret = gabble_roster_handle_remove (priv->conn->roster, handle, error);
    }
  /* deny list */
  else if (GABBLE_LIST_HANDLE_DENY == priv->handle)
    {
      /* unblock contact */
      ret = gabble_roster_handle_set_blocked (priv->conn->roster, handle, FALSE,
          error);
    }
  else
    {
      g_assert_not_reached ();
    }

  return ret;
}


/**
 * gabble_roster_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roster_channel_close (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (iface);
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (self));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  if (priv->handle_type == TP_HANDLE_TYPE_LIST)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "you may not close contact list channels" };

      dbus_g_method_return_error (context, &e);
    }
  else /* TP_HANDLE_TYPE_GROUP */
    {
      if (tp_handle_set_size (self->group.members) == 0)
        {
          /* deleting groups isn't a concept that exists on XMPP,
           * so just close the channel */

          priv->closed = TRUE;
          tp_svc_channel_emit_closed ((TpSvcChannel *)self);
          tp_svc_channel_return_from_close (context);
          return;
        }

      else
        {
          GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "you may not close this group, because it's not empty" };

          dbus_g_method_return_error (context, &e);
        }
    }
}


/**
 * gabble_roster_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roster_channel_get_channel_type (TpSvcChannel *iface,
                                        DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
}


/**
 * gabble_roster_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roster_channel_get_handle (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (iface);
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (self));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, priv->handle_type,
      priv->handle);
}


/**
 * gabble_roster_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roster_channel_get_interfaces (TpSvcChannel *self,
                                      DBusGMethodInvocation *context)
{
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, NULL };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_roster_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}
