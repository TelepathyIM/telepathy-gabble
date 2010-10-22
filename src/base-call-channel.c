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

#include <dbus/dbus-glib-lowlevel.h>

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
#include "dtmf.h"
#include "jingle-session.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

static void channel_iface_init (gpointer, gpointer);
static void call_iface_init (gpointer, gpointer);
static void dtmf_iface_init (gpointer, gpointer);

static GHashTable *members_to_hash (GabbleBaseCallChannel *self);

G_DEFINE_TYPE_WITH_CODE(GabbleBaseCallChannel, gabble_base_call_channel,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
  G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_CALL,
        call_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DTMF,
      dtmf_iface_init);

);

static const gchar *gabble_base_call_channel_interfaces[] = {
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_OBJECT_PATH_PREFIX,
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

  PROP_CURRENTLY_SENDING_TONES,
  PROP_INITIAL_TONES,
  PROP_DEFERRED_TONES,

  LAST_PROPERTY
};

enum
{
  ENDED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };


/* private structure */
struct _GabbleBaseCallChannelPrivate
{
  gchar *object_path;
  gchar *object_path_prefix;
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

  GabbleDTMFPlayer *dtmf_player;
  gchar *deferred_tones;
  gboolean have_some_audio;

  /* handle -> CallMember object hash */
  GHashTable *members;
};

static void
gabble_base_call_channel_tones_deferred_cb (GabbleBaseCallChannel *self,
    const gchar *tones,
    GabbleDTMFPlayer *dtmf_player)
{
  DEBUG ("waiting for user to continue sending '%s'", tones);

  g_free (self->priv->deferred_tones);
  self->priv->deferred_tones = g_strdup (tones);
  /* FIXME: when available in telepathy-spec:
  tp_svc_channel_interface_dtmf_emit_tones_deferred (self, tones);
  */
}

static void
gabble_base_call_channel_constructed (GObject *obj)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (obj);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = (TpBaseConnection *) self->conn;
  TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->object_path == NULL)
    {
      g_assert (priv->object_path_prefix != NULL);
      priv->object_path = g_strdup_printf ("%s/CallChannel%p",
        priv->object_path_prefix, obj);
    }

  if (priv->requested)
    gabble_base_call_channel_set_state (self,
      GABBLE_CALL_STATE_PENDING_INITIATOR);
  else
    gabble_base_call_channel_set_state (self,
      GABBLE_CALL_STATE_PENDING_RECEIVER);

  /* ref target and creator handles if we got them */
  if (priv->creator != 0)
    tp_handle_ref (repo, priv->creator);

  if (self->target != 0)
    tp_handle_ref (repo, self->target);

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

  priv->reason = tp_value_array_build (3,
    G_TYPE_UINT, 0,
    G_TYPE_UINT, 0,
    G_TYPE_STRING, "",
    G_TYPE_INVALID);

  priv->details = tp_asv_new (NULL, NULL);

  priv->members = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, g_object_unref);

  priv->dtmf_player = gabble_dtmf_player_new ();
  priv->have_some_audio = FALSE;

  tp_g_signal_connect_object (priv->dtmf_player, "finished",
      G_CALLBACK (tp_svc_channel_interface_dtmf_emit_stopped_tones), self,
      G_CONNECT_SWAPPED);

  tp_g_signal_connect_object (priv->dtmf_player, "tones-deferred",
      G_CALLBACK (gabble_base_call_channel_tones_deferred_cb), self,
      G_CONNECT_SWAPPED);
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
        break;
      case PROP_OBJECT_PATH_PREFIX:
        g_value_set_string (value, priv->object_path_prefix);
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
      case PROP_CURRENTLY_SENDING_TONES:
        g_value_set_boolean (value,
            gabble_dtmf_player_is_active (priv->dtmf_player));
        break;
      case PROP_INITIAL_TONES:
        /* FIXME: stub */
        g_value_set_static_string (value, "");
        break;
      case PROP_DEFERRED_TONES:
        if (priv->deferred_tones != NULL)
          g_value_set_string (value, priv->deferred_tones);
        else
          g_value_set_static_string (value, "");
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
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_OBJECT_PATH_PREFIX:
        priv->object_path_prefix = g_value_dup_string (value);
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
 static TpDBusPropertiesMixinPropImpl dtmf_props[] = {
      { "CurrentlySendingTones", "currently-sending-tones", NULL },
      { "InitialTones", "initial-tones", NULL },
      /* FIXME:
       * { "DeferredTones", "deferred-tones", NULL }, */
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
      { TP_IFACE_CHANNEL_INTERFACE_DTMF,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        dtmf_props,
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

  signals[ENDED] = g_signal_new ("ended",
      G_OBJECT_CLASS_TYPE (object_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  param_spec = g_param_spec_string ("object-path-prefix", "Object path prefix",
      "prefix of the object path",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH_PREFIX,
      param_spec);

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

  param_spec = g_param_spec_boolean ("currently-sending-tones",
      "CurrentlySendingTones",
      "True if a DTMF tone is being sent",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CURRENTLY_SENDING_TONES,
      param_spec);

  param_spec = g_param_spec_string ("initial-tones", "InitialTones",
      "Initial DTMF tones to be sent in the first audio stream",
      "", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_TONES,
      param_spec);

  param_spec = g_param_spec_string ("deferred-tones", "DeferredTones",
      "DTMF tones that followed a 'w' or 'W', to be resumed on user request",
      "", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DEFERRED_TONES,
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
  TpBaseConnection *base_conn = (TpBaseConnection *) self->conn;
  TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  for (l = priv->contents; l != NULL; l = g_list_next (l))
    {
      gabble_call_content_deinit (l->data);
    }

  tp_clear_pointer (&priv->members, g_hash_table_unref);
  tp_clear_pointer (&priv->contents, g_list_free);

  if (priv->creator != 0)
    tp_handle_unref (repo, priv->creator);

  if (self->target != 0)
    tp_handle_unref (repo, self->target);

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
  g_free (self->priv->object_path_prefix);
  tp_clear_pointer (&self->priv->deferred_tones, g_free);

  G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->finalize (object);
}

void
gabble_base_call_channel_set_state (GabbleBaseCallChannel *self,
  GabbleCallState state)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;

  /* signal when going to the ended state */
  if (state != priv->state && state == GABBLE_CALL_STATE_ENDED)
    g_signal_emit (self, signals[ENDED], 0);

  priv->state = state;

  if (priv->state != GABBLE_CALL_STATE_PENDING_RECEIVER)
    priv->flags &= ~GABBLE_CALL_FLAG_LOCALLY_RINGING;

  if (priv->registered)
    gabble_svc_channel_type_call_emit_call_state_changed (self, priv->state,
      priv->flags, priv->reason, priv->details);
}

GabbleCallState
gabble_base_call_channel_get_state (GabbleBaseCallChannel *self)
{
  return self->priv->state;
}

void
base_call_channel_remove_content (GabbleBaseCallChannel *self,
    GabbleCallContent *content)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  const gchar *path;
  GList *l;
  gboolean still_have_audio = FALSE;

  priv->contents = g_list_remove (priv->contents, content);

  path = gabble_call_content_get_object_path (content);
  gabble_svc_channel_type_call_emit_content_removed (self, path);

  gabble_call_content_deinit (content);

  /* let's see if we still have any audio contents */
  for (l = priv->contents; l != NULL; l = l->next)
    {
      if (gabble_call_content_get_media_type (
              GABBLE_CALL_CONTENT (l->data)) ==
          JINGLE_MEDIA_TYPE_AUDIO)
        {
          still_have_audio = TRUE;
          break;
        }
    }

  if (priv->have_some_audio && !still_have_audio)
    {
      /* the last audio stream just closed */
      gabble_dtmf_player_cancel (priv->dtmf_player);
    }

  priv->have_some_audio = still_have_audio;
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
  gchar *escaped;

  /* FIXME could clash when other party in a one-to-one call creates a stream
   * with the same media type and name */
  escaped = tp_escape_as_identifier (name);
  object_path =
    g_strdup_printf ("%s/Content_%s_%d", priv->object_path, escaped, mtype);
  g_free (escaped);

  content = g_object_new (GABBLE_TYPE_CALL_CONTENT,
    "connection", self->conn,
    "object-path", object_path,
    "disposition", disposition,
    "jingle-media-type", mtype,
    "name", name,
    NULL);

  g_free (object_path);

  priv->contents = g_list_prepend (priv->contents, content);

  gabble_svc_channel_type_call_emit_content_added (self,
     gabble_call_content_get_object_path (content),
     mtype == JINGLE_MEDIA_TYPE_AUDIO ?
      TP_MEDIA_STREAM_TYPE_AUDIO : TP_MEDIA_STREAM_TYPE_VIDEO);

  if (mtype == JINGLE_MEDIA_TYPE_AUDIO)
    priv->have_some_audio = TRUE;

  return GABBLE_CALL_CONTENT (content);
}

void
gabble_base_call_channel_register (GabbleBaseCallChannel *self)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  TpDBusDaemon *bus;

  /* register object on the bus */
  DEBUG ("Registering %s", priv->object_path);
  bus = tp_base_connection_get_dbus_daemon ((TpBaseConnection *) self->conn);
  tp_dbus_daemon_register_object (bus, priv->object_path, G_OBJECT (self));

  priv->registered = TRUE;
}

void
gabble_base_call_channel_close (GabbleBaseCallChannel *self)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  DEBUG ("Closing media channel %s", self->priv->object_path);

  if (!priv->closed)
    {
      GabbleBaseCallChannelClass *base_class =
        GABBLE_BASE_CALL_CHANNEL_GET_CLASS (self);
      GList *l;
      GHashTableIter iter;
      gpointer value;

      priv->closed = TRUE;

      if (base_class->close != NULL)
        base_class->close (self);

      g_hash_table_iter_init (&iter, priv->members);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        gabble_call_member_shutdown (value);

      /* shutdown all our contents */
      for (l = priv->contents ; l != NULL; l = g_list_next (l))
        {
          gabble_call_content_deinit (GABBLE_CALL_CONTENT (l->data));
        }
      g_list_free (priv->contents);
      priv->contents = NULL;

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

  if (DEBUGGING)
    {
      gchar *caller = dbus_g_method_get_sender (context);

      DEBUG ("called by %s", caller);
      g_free (caller);
    }

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
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);
  GabbleBaseCallChannelPrivate *priv = self->priv;

  if (priv->requested)
    {
      GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Call was requested. Ringing doesn't make sense." };
      dbus_g_method_return_error (context, &e);
    }
  else if (priv->state != GABBLE_CALL_STATE_PENDING_RECEIVER)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Call is not in the right state for Ringing." };
      dbus_g_method_return_error (context, &e);
    }
  else
    {
      if ((priv->flags & GABBLE_CALL_FLAG_LOCALLY_RINGING) == 0)
        {
          DEBUG ("Client is ringing");
          priv->flags |= GABBLE_CALL_FLAG_LOCALLY_RINGING;
          gabble_base_call_channel_set_state (self, priv->state);
        }

      gabble_svc_channel_type_call_return_from_ringing (context);
    }
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
      else
        {
          DEBUG ("Invalid state for Accept: Channel requested and "
              "state == %d", priv->state);
          goto err;
        }
    }
  else if (priv->state < GABBLE_CALL_STATE_ACCEPTED)
    {
      gabble_base_call_channel_set_state (self,
        GABBLE_CALL_STATE_ACCEPTED);
    }
  else
    {
      DEBUG ("Invalid state for Accept: state == %d", priv->state);
      goto err;
    }

  if (base_class->accept != NULL)
    base_class->accept (self);

  g_list_foreach (self->priv->contents,
      (GFunc)gabble_call_content_accept, NULL);

  gabble_svc_channel_type_call_return_from_accept (context);
  return;

err:
  {
    GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "Invalid state for Accept" };
    dbus_g_method_return_error (context, &e);
  }
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
  GabbleBaseCallChannelClass *base_class =
      GABBLE_BASE_CALL_CHANNEL_GET_CLASS (self);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->members);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    gabble_call_member_shutdown (GABBLE_CALL_MEMBER (value));

  if (base_class->hangup)
    base_class->hangup (self, reason, detailed_reason, message);

  gabble_base_call_channel_set_state ( GABBLE_BASE_CALL_CHANNEL (self),
          GABBLE_CALL_STATE_ENDED);

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
  GabbleBaseCallChannelClass *base_class =
      GABBLE_BASE_CALL_CHANNEL_GET_CLASS (self);
  JingleMediaType type = JINGLE_MEDIA_TYPE_NONE;
  GError *error = NULL;
  GabbleCallContent *content;

  if (priv->state == GABBLE_CALL_STATE_ENDED)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "No contents can be added. The call has already ended.");
      goto error;
    }

  if (mtype == TP_MEDIA_STREAM_TYPE_AUDIO)
    type = JINGLE_MEDIA_TYPE_AUDIO;
  else if (mtype == TP_MEDIA_STREAM_TYPE_VIDEO)
    type = JINGLE_MEDIA_TYPE_VIDEO;
  else
    goto unicorns;

  content = base_class->add_content (self, name, type, &error);

  if (content == NULL)
    goto error;

  gabble_svc_channel_type_call_return_from_add_content (context,
    gabble_call_content_get_object_path (content));
  return;

unicorns:
  {
    GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "Unknown content type" };
    dbus_g_method_return_error (context, &e);
    return;
  }

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
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

#define TONE_MS 200
#define GAP_MS 100
#define PAUSE_MS 3000
/* arbitrary limit on the length of a tone started with StartTone */
#define MAX_TONE_SECONDS 10

static void
gabble_base_call_channel_start_tone (TpSvcChannelInterfaceDTMF *iface,
    guint stream_id G_GNUC_UNUSED,
    guchar event,
    DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);
  gchar tones[2] = { '\0', '\0' };
  GError *error = NULL;

  if (!self->priv->have_some_audio)
    {
      GError e = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "There are no audio streams" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  tones[0] = gabble_dtmf_event_to_char (event);

  if (gabble_dtmf_player_play (self->priv->dtmf_player,
      tones, MAX_TONE_SECONDS * 1000, GAP_MS, PAUSE_MS, &error))
    {
      tp_clear_pointer (&self->priv->deferred_tones, g_free);
      tp_svc_channel_interface_dtmf_emit_sending_tones (self, tones);
      tp_svc_channel_interface_dtmf_return_from_start_tone (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_clear_error (&error);
    }
}

static void
gabble_base_call_channel_stop_tone (TpSvcChannelInterfaceDTMF *iface,
    guint stream_id,
    DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);

  gabble_dtmf_player_cancel (self->priv->dtmf_player);
  tp_svc_channel_interface_dtmf_return_from_stop_tone (context);
}

static void
gabble_base_call_channel_multiple_tones (
    TpSvcChannelInterfaceDTMF *iface,
    const gchar *dialstring,
    DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);
  GError *error = NULL;

  if (!self->priv->have_some_audio)
    {
      GError e = { TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "There are no audio streams" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  if (gabble_dtmf_player_play (self->priv->dtmf_player,
      dialstring, TONE_MS, GAP_MS, PAUSE_MS, &error))
    {
      tp_clear_pointer (&self->priv->deferred_tones, g_free);
      tp_svc_channel_interface_dtmf_emit_sending_tones (self, dialstring);
      tp_svc_channel_interface_dtmf_return_from_start_tone (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_clear_error (&error);
    }
}

static void
dtmf_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceDTMFClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_dtmf_implement_##x (\
    klass, gabble_base_call_channel_##x)
  IMPLEMENT(start_tone);
  IMPLEMENT(stop_tone);
  IMPLEMENT(multiple_tones);
#undef IMPLEMENT
}

gboolean
gabble_base_call_channel_registered (GabbleBaseCallChannel *self)
{
  return self->priv->registered;
}

static void
base_call_channel_signal_call_members (GabbleBaseCallChannel *self,
  TpHandle removed_handle)
{
  GArray *removals = g_array_new (TRUE, TRUE, sizeof (TpHandle));
  GHashTable *members;

  members = members_to_hash (self);

  if (removed_handle != 0)
    g_array_append_val (removals, removed_handle);

  gabble_svc_channel_type_call_emit_call_members_changed (self,
      members, removals);

  g_array_unref (removals);
  g_hash_table_unref (members);
}

static void
call_member_flags_changed_cb (GabbleCallMember *member,
  GabbleCallMemberFlags flags,
  gpointer user_data)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (user_data);

  base_call_channel_signal_call_members (self, 0);
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

      base_call_channel_signal_call_members (self, 0);
    }

  return m;
}

void
gabble_base_call_channel_remove_member (GabbleBaseCallChannel *self,
    GabbleCallMember *member)
{
  TpHandle h = gabble_call_member_get_handle (member);

  g_assert (g_hash_table_lookup (self->priv->members,
    GUINT_TO_POINTER (h))== member);

  gabble_call_member_shutdown (member);
  g_hash_table_remove (self->priv->members, GUINT_TO_POINTER (h));
  base_call_channel_signal_call_members (self, h);
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

GHashTable *
gabble_base_call_channel_get_members (GabbleBaseCallChannel *self)
{
  return self->priv->members;
}
