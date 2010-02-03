/*
 * gabble-base-call-channel.c - Source for GabbleBaseBaseCallChannel
 * Copyright (C) 2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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


#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/gtypes.h>

#include <extensions/extensions.h>

#include "util.h"
#include "base-call-channel.h"
#include "call-content.h"

#include "connection.h"
#include "jingle-session.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

static void channel_iface_init (gpointer, gpointer);
static void call_iface_init (gpointer, gpointer);

static GHashTable *members_to_hash (GabbleBaseCallChannel *self);

G_DEFINE_TYPE_WITH_CODE(GabbleBaseCallChannel, gabble_base_call_channel,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
  G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_CALL,
        call_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL)
);

static const gchar *gabble_base_call_channel_interfaces[] = {
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_TARGET_HANDLE,
  PROP_TARGET_ID,

  PROP_REQUESTED,
  PROP_CONNECTION,
  PROP_CREATOR,
  PROP_CREATOR_ID,

  PROP_INTERFACES,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  PROP_INITIAL_AUDIO,
  PROP_INITIAL_VIDEO,
  PROP_MUTABLE_CONTENTS,
  PROP_HARDWARE_STREAMING,
  PROP_CONTENTS,

  PROP_CALL_STATE,
  PROP_CALL_FLAGS,
  PROP_CALL_STATE_DETAILS,
  PROP_CALL_STATE_REASON,

  PROP_CALL_MEMBERS,

  LAST_PROPERTY
};


/* private structure */
struct _GabbleBaseCallChannelPrivate
{
  gchar *object_path;
  TpHandle creator;

  gboolean closed;

  gboolean registered;
  gboolean requested;

  gboolean dispose_has_run;

  GList *contents;

  GabbleCallState state;
  guint flags;
  GHashTable *details;
  GValueArray *reason;

  /* handle -> CallMember object hash */
  GHashTable *members;
};

static void
gabble_base_call_channel_constructed (GObject *obj)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (obj);
  GabbleBaseCallChannelPrivate *priv = self->priv;

  if (priv->requested)
    gabble_base_call_channel_set_state (self,
      GABBLE_CALL_STATE_PENDING_INITIATOR);
  else
    gabble_base_call_channel_set_state (self,
      GABBLE_CALL_STATE_PENDING_RECEIVER);

  if (G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->constructed
      != NULL)
    G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->constructed (obj);
}

static void
gabble_base_call_channel_init (GabbleBaseCallChannel *self)
{
  GabbleBaseCallChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BASE_CALL_CHANNEL, GabbleBaseCallChannelPrivate);

  self->priv = priv;

  priv->reason = gabble_value_array_build (3,
    G_TYPE_UINT, 0,
    G_TYPE_UINT, 0,
    G_TYPE_STRING, "",
    G_TYPE_INVALID);

  priv->details = tp_asv_new (NULL, NULL);

  priv->members = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, g_object_unref);
}

static void gabble_base_call_channel_dispose (GObject *object);
static void gabble_base_call_channel_finalize (GObject *object);

static void
gabble_base_call_channel_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (object);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = (TpBaseConnection *) self->conn;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        g_assert (priv->object_path != NULL);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, GABBLE_IFACE_CHANNEL_TYPE_CALL);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value,
            GABBLE_BASE_CALL_CHANNEL_GET_CLASS (self)->handle_type);
        break;
      case PROP_TARGET_HANDLE:
        g_value_set_uint (value, self->target);
        break;
      case PROP_TARGET_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);
          const gchar *target_id = tp_handle_inspect (repo, self->target);

          g_value_set_string (value, target_id);
        }
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, self->conn);
        break;
      case PROP_CREATOR:
        g_value_set_uint (value, priv->creator);
        break;
      case PROP_CREATOR_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
               base_conn, TP_HANDLE_TYPE_CONTACT);

          g_value_set_string (value, tp_handle_inspect (repo, priv->creator));
        }
        break;
      case PROP_REQUESTED:
        g_value_set_boolean (value, priv->requested);
        break;
      case PROP_INTERFACES:
        g_value_set_boxed (value, gabble_base_call_channel_interfaces);
        break;
      case PROP_CHANNEL_DESTROYED:
        g_value_set_boolean (value, priv->closed);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_take_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                TP_IFACE_CHANNEL, "Requested",
                TP_IFACE_CHANNEL, "Interfaces",
                GABBLE_IFACE_CHANNEL_TYPE_CALL, "InitialAudio",
                GABBLE_IFACE_CHANNEL_TYPE_CALL, "InitialVideo",
                GABBLE_IFACE_CHANNEL_TYPE_CALL, "MutableContents",
                NULL));
        break;
      case PROP_INITIAL_AUDIO:
        g_value_set_boolean (value, self->initial_audio);
        break;
      case PROP_INITIAL_VIDEO:
        g_value_set_boolean (value, self->initial_video);
        break;
      case PROP_MUTABLE_CONTENTS:
      /* FIXME: this should probably move to the implementation class
        if (priv->session != NULL)
          g_value_set_boolean (value,
            gabble_jingle_session_can_modify_contents (priv->session));
        else
          */
          g_value_set_boolean (value, TRUE);
        break;
      case PROP_CONTENTS:
        {
          GPtrArray *arr = g_ptr_array_sized_new (2);
          GList *l;

          for (l = priv->contents; l != NULL; l = g_list_next (l))
            {
              GabbleCallContent *c = GABBLE_CALL_CONTENT (l->data);
              g_ptr_array_add (arr,
                (gpointer) gabble_call_content_get_object_path (c));
            }

          g_value_set_boxed (value, arr);
          g_ptr_array_free (arr, TRUE);
          break;
        }
      case PROP_HARDWARE_STREAMING:
        g_value_set_boolean (value, FALSE);
        break;
      case PROP_CALL_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_CALL_FLAGS:
        g_value_set_uint (value, priv->flags);
        break;
      case PROP_CALL_STATE_DETAILS:
        g_value_set_boxed (value, priv->details);
        break;
      case PROP_CALL_STATE_REASON:
        g_value_set_boxed (value, priv->reason);
        break;
      case PROP_CALL_MEMBERS:
        g_value_take_boxed (value, members_to_hash (self));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_base_call_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (object);
  GabbleBaseCallChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_REQUESTED:
        priv->requested = g_value_get_boolean (value);
        break;
      case PROP_HANDLE_TYPE:
      case PROP_CHANNEL_TYPE:
        /* these properties are writable in the interface, but not actually
        * meaningfully changable on this channel, so we do nothing */
        break;
      case PROP_TARGET_HANDLE:
        self->target = g_value_get_uint (value);
        g_assert (self->target != 0);
        break;
      case PROP_CONNECTION:
        self->conn = g_value_get_object (value);
        g_assert (self->conn != NULL);
        break;
      case PROP_CREATOR:
        priv->creator = g_value_get_uint (value);
        break;
      case PROP_INITIAL_AUDIO:
        self->initial_audio = g_value_get_boolean (value);
        break;
      case PROP_INITIAL_VIDEO:
        self->initial_video = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}


static void
gabble_base_call_channel_class_init (
    GabbleBaseCallChannelClass *gabble_base_call_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_base_call_channel_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
      { "Requested", "requested", NULL },
      { "InitiatorHandle", "creator", NULL },
      { "InitiatorID", "creator-id", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl call_props[] = {
      { "CallMembers", "call-members", NULL },
      { "MutableContents", "mutable-contents", NULL },
      { "InitialAudio", "initial-audio", NULL },
      { "InitialVideo", "initial-video", NULL },
      { "Contents", "contents", NULL },
      { "HardwareStreaming", "hardware-streaming", NULL },
      { "CallState", "call-state", NULL },
      { "CallFlags", "call-flags", NULL },
      { "CallStateReason",  "call-state-reason", NULL },
      { "CallStateDetails", "call-state-details", NULL },
      { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { GABBLE_IFACE_CHANNEL_TYPE_CALL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        call_props,
      },
      { NULL }
  };

  g_type_class_add_private (gabble_base_call_channel_class,
      sizeof (GabbleBaseCallChannelPrivate));

  object_class->constructed = gabble_base_call_channel_constructed;

  object_class->get_property = gabble_base_call_channel_get_property;
  object_class->set_property = gabble_base_call_channel_set_property;

  object_class->dispose = gabble_base_call_channel_dispose;
  object_class->finalize = gabble_base_call_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_TARGET_HANDLE,
      "handle");

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "Target JID of the call" ,
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this media channel object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_uint ("creator", "Channel creator",
      "The TpHandle representing the contact who created the channel.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR, param_spec);

  param_spec = g_param_spec_string ("creator-id", "Creator bare JID",
      "The bare JID obtained by inspecting the creator handle.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_boolean ("initial-audio", "InitialAudio",
      "Whether the channel initially contained an audio stream",
      FALSE,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_AUDIO,
      param_spec);

  param_spec = g_param_spec_boolean ("initial-video", "InitialVideo",
      "Whether the channel initially contained an video stream",
      FALSE,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_VIDEO,
      param_spec);

  param_spec = g_param_spec_boolean ("mutable-contents", "MutableContents",
      "Whether the set of streams on this channel are mutable once requested",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MUTABLE_CONTENTS,
      param_spec);

  param_spec = g_param_spec_boxed ("contents", "Contents",
      "The contents of the channel",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTENTS,
      param_spec);

  param_spec = g_param_spec_boolean ("hardware-streaming", "HardwareStreaming",
      "True if all the streaming is done by hardware",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HARDWARE_STREAMING,
      param_spec);

  param_spec = g_param_spec_uint ("call-state", "CallState",
      "The status of the call",
      GABBLE_CALL_STATE_UNKNOWN,
      NUM_GABBLE_CALL_STATES,
      GABBLE_CALL_STATE_UNKNOWN,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CALL_STATE, param_spec);

  param_spec = g_param_spec_uint ("call-flags", "CallFlags",
      "Flags representing the status of the call",
      0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CALL_FLAGS,
      param_spec);

  param_spec = g_param_spec_boxed ("call-state-reason", "CallStateReason",
      "The reason why the call is in the current state",
      GABBLE_STRUCT_TYPE_CALL_STATE_REASON,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CALL_STATE_REASON,
      param_spec);

  param_spec = g_param_spec_boxed ("call-state-details", "CallStateDetails",
      "The reason why the call is in the current state",
      TP_HASH_TYPE_QUALIFIED_PROPERTY_VALUE_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CALL_STATE_DETAILS,
      param_spec);

  param_spec = g_param_spec_boxed ("call-members", "CallMembers",
      "The members",
      GABBLE_HASH_TYPE_CALL_MEMBER_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CALL_MEMBERS,
      param_spec);

  gabble_base_call_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleBaseCallChannelClass, dbus_props_class));
}

void
gabble_base_call_channel_dispose (GObject *object)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (object);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  GList *l;

  if (priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  for (l = priv->contents; l != NULL; l = g_list_next (l))
    {
      gabble_call_content_deinit (l->data);
    }

  g_hash_table_unref (self->priv->members);

  g_list_free (priv->contents);
  priv->contents = NULL;

  if (G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->dispose (object);
}

void
gabble_base_call_channel_finalize (GObject *object)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (object);
  GabbleBaseCallChannelPrivate *priv = self->priv;

  g_hash_table_unref (priv->details);
  g_value_array_free (priv->reason);
  g_free (self->priv->object_path);

  G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->finalize (object);
}

void
gabble_base_call_channel_set_state (GabbleBaseCallChannel *self,
  GabbleCallState state)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;

  priv->state = state;

  if (priv->registered)
    gabble_svc_channel_type_call_emit_call_state_changed (self, priv->state,
      priv->flags, priv->reason, priv->details);
}

GabbleCallState
gabble_base_call_channel_get_state (GabbleBaseCallChannel *self)
{
  return self->priv->state;
}

GabbleCallContent *
gabble_base_call_channel_add_content (GabbleBaseCallChannel *self,
    const gchar *name,
    JingleMediaType mtype,
    GabbleCallContentDisposition disposition)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  gchar *object_path;
  GabbleCallContent *content;

  /* FIXME could clash when other party in a one-to-one call creates a stream
   * with the same media type and name */
  object_path =
    g_strdup_printf ("%s/Content_%s_%d", priv->object_path, name, mtype);

  content = g_object_new (GABBLE_TYPE_CALL_CONTENT,
    "connection", self->conn,
    "object-path", object_path,
    "disposition", disposition,
    "jingle-media-type", mtype,
    "name", name,
    NULL);

  g_free (object_path);

  priv->contents = g_list_prepend (priv->contents, content);

  return GABBLE_CALL_CONTENT (content);
}

void
gabble_base_call_channel_register (GabbleBaseCallChannel *self)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  DBusGConnection *bus;

  /* register object on the bus */
  bus = tp_get_bus ();
  DEBUG ("Registering %s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path,
    G_OBJECT (self));

  priv->registered = TRUE;
}

void
gabble_base_call_channel_close (GabbleBaseCallChannel *self)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  DEBUG ("Closing media channel %s", self->priv->object_path);

  if (!priv->closed)
    {
      GHashTableIter iter;
      gpointer value;

      priv->closed = TRUE;

      g_hash_table_iter_init (&iter, priv->members);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          GabbleCallMember *member = GABBLE_CALL_MEMBER (value);
          GabbleJingleSession *session =
              gabble_call_member_get_session (member);

          gabble_jingle_session_terminate (session,
           TP_CHANNEL_GROUP_CHANGE_REASON_NONE,
            NULL, NULL);
       }

      tp_svc_channel_emit_closed (self);
    }
}

/**
 * gabble_base_call_channel_close_async:
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_base_call_channel_close_async (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);

  DEBUG ("called");
  gabble_base_call_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

/**
 * gabble_base_call_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_base_call_channel_get_channel_type (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      GABBLE_IFACE_CHANNEL_TYPE_CALL);
}

/**
 * gabble_base_call_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_base_call_channel_get_handle (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
    GABBLE_BASE_CALL_CHANNEL (iface)->target);
}

/**
 * gabble_base_call_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_base_call_channel_get_interfaces (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      gabble_base_call_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_base_call_channel_##x##suffix)
    IMPLEMENT(close,_async);
    IMPLEMENT(get_channel_type,);
    IMPLEMENT(get_handle,);
    IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
gabble_base_call_channel_ringing (GabbleSvcChannelTypeCall *iface,
    DBusGMethodInvocation *context)
{
  DEBUG ("Client is ringing");
  gabble_svc_channel_type_call_return_from_ringing (context);
}

static void
gabble_base_call_channel_accept (GabbleSvcChannelTypeCall *iface,
        DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  GabbleBaseCallChannelClass *base_class =
      GABBLE_BASE_CALL_CHANNEL_GET_CLASS (self);

  DEBUG ("Client accepted the call");

  if (priv->requested)
    {
      if (priv->state == GABBLE_CALL_STATE_PENDING_INITIATOR)
        {
          gabble_base_call_channel_set_state (self,
              GABBLE_CALL_STATE_PENDING_RECEIVER);
        }
    }
  else if (priv->state < GABBLE_CALL_STATE_ACCEPTED)
    {
      gabble_base_call_channel_set_state (self,
        GABBLE_CALL_STATE_ACCEPTED);
    }

  if (base_class->accept != NULL)
    base_class->accept (self);

  gabble_svc_channel_type_call_return_from_accept (context);
}

static void
gabble_base_call_channel_hangup (GabbleSvcChannelTypeCall *iface,
  guint reason,
  const gchar *detailed_reason,
  const gchar *message,
  DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->members);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      GabbleCallMember *member = GABBLE_CALL_MEMBER (value);
      GabbleJingleSession *session = gabble_call_member_get_session (member);

      gabble_jingle_session_terminate (session,
        TP_CHANNEL_GROUP_CHANGE_REASON_NONE,
        message, NULL);
    }

  gabble_svc_channel_type_call_return_from_hangup (context);
}

static void
gabble_base_call_channel_add_content_dbus (GabbleSvcChannelTypeCall *iface,
  const gchar *name,
  TpMediaStreamType mtype,
  DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  JingleMediaType type = JINGLE_MEDIA_TYPE_NONE;
  const char *path = NULL;
  GHashTableIter iter;
  gpointer value;
  GError *error = NULL;

  if (mtype == TP_MEDIA_STREAM_TYPE_AUDIO)
    type = JINGLE_MEDIA_TYPE_AUDIO;
  else if (mtype == TP_MEDIA_STREAM_TYPE_VIDEO)
    type = JINGLE_MEDIA_TYPE_VIDEO;
  else
    goto unicorns;

  /* FIXME needs to be a virtual method that calls up to the implementation so
   * that it can actually do the right thing (tm) */

  g_hash_table_iter_init (&iter, priv->members);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      GabbleCallMember *member = GABBLE_CALL_MEMBER (value);
      GabbleCallMemberContent *content;

      content = gabble_call_member_create_content (member, name, type, &error);

      if (content != NULL)
        {
          GabbleCallContent *c;
          c = gabble_base_call_channel_add_content (self,
            name, type, GABBLE_CALL_CONTENT_DISPOSITION_NONE);
          gabble_call_content_add_member_content (c, content);
          path = gabble_call_content_get_object_path (c);
        }
      break;
    }

  if (path == NULL)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  gabble_svc_channel_type_call_emit_content_added (self, path, type);

  gabble_svc_channel_type_call_return_from_add_content (context, path);
  return;

unicorns:
  {
    GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "Unknown content type" };
    dbus_g_method_return_error (context, &e);
  }
}


static void
call_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcChannelTypeCallClass *klass =
    (GabbleSvcChannelTypeCallClass *) g_iface;

#define IMPLEMENT(x, suffix) gabble_svc_channel_type_call_implement_##x (\
    klass, gabble_base_call_channel_##x##suffix)
  IMPLEMENT(ringing,);
  IMPLEMENT(accept,);
  IMPLEMENT(hangup,);
  IMPLEMENT(add_content, _dbus);
#undef IMPLEMENT
}

gboolean
gabble_base_call_channel_registered (GabbleBaseCallChannel *self)
{
  return self->priv->registered;
}

static void
call_member_flags_changed_cb (GabbleCallMember *member,
  GabbleCallMemberFlags flags,
  gpointer user_data)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (user_data);
  GArray *empty = g_array_new (TRUE, TRUE, sizeof (TpHandle));
  GHashTable *members;

  members = members_to_hash (self);

  gabble_svc_channel_type_call_emit_call_members_changed (self,
      members, empty);
  g_array_unref (empty);

  g_hash_table_unref (members);
}

GabbleCallMember *
gabble_base_call_channel_get_member_from_handle (
    GabbleBaseCallChannel *self,
    TpHandle handle)
{
  return g_hash_table_lookup (self->priv->members, GUINT_TO_POINTER (handle));
}

GabbleCallMember *
gabble_base_call_channel_ensure_member_from_handle (
    GabbleBaseCallChannel *self,
    TpHandle handle)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  GabbleCallMember *m;

  m = g_hash_table_lookup (priv->members, GUINT_TO_POINTER (handle));
  if (m == NULL)
    {
      m = GABBLE_CALL_MEMBER (g_object_new (GABBLE_TYPE_CALL_MEMBER,
        "target", handle,
        "call", self,
        NULL));
      g_hash_table_insert (priv->members, GUINT_TO_POINTER (handle), m);
      gabble_signal_connect_weak (m, "flags-changed",
        G_CALLBACK (call_member_flags_changed_cb), G_OBJECT (self));
    }

  return m;
}

static GHashTable *
members_to_hash (GabbleBaseCallChannel *self)
{
  GHashTable *h = g_hash_table_new (g_direct_hash, g_direct_equal);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, self->priv->members);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      GabbleCallMember *m = GABBLE_CALL_MEMBER (value);

      g_hash_table_insert (h,
        GUINT_TO_POINTER (gabble_call_member_get_handle (m)),
        GUINT_TO_POINTER ((guint) gabble_call_member_get_flags (m)));
    }

  return h;
}

GList *
gabble_base_call_channel_get_contents (GabbleBaseCallChannel *self)
{
  return self->priv->contents;
}
