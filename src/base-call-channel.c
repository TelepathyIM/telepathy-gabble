/*
 * base-call-channel.c - Source for GabbleBaseCallChannel
 * Copyright © 2009–2010 Collabora Ltd.
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

#include "util.h"
#include "base-call-channel.h"
#include "call-content.h"

#include "connection.h"
#include "jingle-session.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

static void call_iface_init (gpointer, gpointer);
static void gabble_base_call_channel_close (TpBaseChannel *base);

static GHashTable *members_to_hash (GabbleBaseCallChannel *self);

G_DEFINE_TYPE_WITH_CODE(GabbleBaseCallChannel, gabble_base_call_channel,
  TP_TYPE_BASE_CHANNEL,
  G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_CALL,
        call_iface_init)
);

static const gchar *gabble_base_call_channel_interfaces[] = {
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH_PREFIX = 1,

  PROP_INITIAL_AUDIO,
  PROP_INITIAL_VIDEO,
  PROP_INITIAL_AUDIO_NAME,
  PROP_INITIAL_VIDEO_NAME,
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

enum
{
  ENDED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };


/* private structure */
struct _GabbleBaseCallChannelPrivate
{
  gchar *object_path_prefix;

  gboolean dispose_has_run;

  GList *contents;

  gchar *initial_audio_name;
  gchar *initial_video_name;

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
  TpBaseChannel *base = TP_BASE_CHANNEL (self);

  if (G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->constructed
      != NULL)
    G_OBJECT_CLASS (gabble_base_call_channel_parent_class)->constructed (obj);

  if (tp_base_channel_is_requested (base))
    gabble_base_call_channel_set_state (self,
      GABBLE_CALL_STATE_PENDING_INITIATOR);
  else
    gabble_base_call_channel_set_state (self,
      GABBLE_CALL_STATE_PENDING_RECEIVER);
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

  switch (property_id)
    {
      case PROP_OBJECT_PATH_PREFIX:
        g_value_set_string (value, priv->object_path_prefix);
        break;
      case PROP_INITIAL_AUDIO:
        g_value_set_boolean (value, self->initial_audio);
        break;
      case PROP_INITIAL_VIDEO:
        g_value_set_boolean (value, self->initial_video);
        break;
      case PROP_INITIAL_AUDIO_NAME:
        g_value_set_string (value, priv->initial_audio_name);
        break;
      case PROP_INITIAL_VIDEO_NAME:
        g_value_set_string (value, priv->initial_video_name);
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
              GabbleBaseCallContent *c = GABBLE_BASE_CALL_CONTENT (l->data);
              g_ptr_array_add (arr,
                (gpointer) gabble_base_call_content_get_object_path (c));
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
      case PROP_OBJECT_PATH_PREFIX:
        priv->object_path_prefix = g_value_dup_string (value);
        break;
      case PROP_INITIAL_AUDIO:
        self->initial_audio = g_value_get_boolean (value);
        break;
      case PROP_INITIAL_VIDEO:
        self->initial_video = g_value_get_boolean (value);
        break;
      case PROP_INITIAL_AUDIO_NAME:
        priv->initial_audio_name = g_value_dup_string (value);
        break;
      case PROP_INITIAL_VIDEO_NAME:
        priv->initial_video_name = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static gchar *
gabble_base_call_channel_get_object_path_suffix (TpBaseChannel *base)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (base);
  GabbleBaseCallChannelPrivate *priv = self->priv;

  g_assert (priv->object_path_prefix != NULL);

  return g_strdup_printf ("%s/CallChannel%p", priv->object_path_prefix, self);
}

static void
gabble_base_call_channel_fill_immutable_properties (
    TpBaseChannel *chan,
    GHashTable *properties)
{
  TP_BASE_CHANNEL_CLASS (gabble_base_call_channel_parent_class)
      ->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      GABBLE_IFACE_CHANNEL_TYPE_CALL, "InitialAudio",
      GABBLE_IFACE_CHANNEL_TYPE_CALL, "InitialVideo",
      GABBLE_IFACE_CHANNEL_TYPE_CALL, "MutableContents",
      NULL);
}

static void
gabble_base_call_channel_class_init (
    GabbleBaseCallChannelClass *gabble_base_call_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_base_call_channel_class);
  TpBaseChannelClass *base_channel_class =
      TP_BASE_CHANNEL_CLASS (gabble_base_call_channel_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl call_props[] = {
      { "CallMembers", "call-members", NULL },
      { "MutableContents", "mutable-contents", NULL },
      { "InitialAudio", "initial-audio", NULL },
      { "InitialVideo", "initial-video", NULL },
      { "InitialAudioName", "initial-audio-name", NULL },
      { "InitialVideoName", "initial-video-name", NULL },
      { "Contents", "contents", NULL },
      { "HardwareStreaming", "hardware-streaming", NULL },
      { "CallState", "call-state", NULL },
      { "CallFlags", "call-flags", NULL },
      { "CallStateReason",  "call-state-reason", NULL },
      { "CallStateDetails", "call-state-details", NULL },
      { NULL }
  };

  g_type_class_add_private (gabble_base_call_channel_class,
      sizeof (GabbleBaseCallChannelPrivate));

  object_class->constructed = gabble_base_call_channel_constructed;

  object_class->get_property = gabble_base_call_channel_get_property;
  object_class->set_property = gabble_base_call_channel_set_property;

  object_class->dispose = gabble_base_call_channel_dispose;
  object_class->finalize = gabble_base_call_channel_finalize;

  base_channel_class->channel_type = GABBLE_IFACE_CHANNEL_TYPE_CALL;
  base_channel_class->interfaces = gabble_base_call_channel_interfaces;
  base_channel_class->get_object_path_suffix =
      gabble_base_call_channel_get_object_path_suffix;
  base_channel_class->fill_immutable_properties =
      gabble_base_call_channel_fill_immutable_properties;
  base_channel_class->close = gabble_base_call_channel_close;

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

  param_spec = g_param_spec_string ("initial-audio-name", "InitialAudioName",
      "Name for the initial audio content",
      "audio",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_AUDIO_NAME,
      param_spec);

  param_spec = g_param_spec_string ("initial-video-name", "InitialVideoName",
      "Name for the initial video content",
      "video",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_VIDEO_NAME,
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

  tp_dbus_properties_mixin_implement_interface (object_class,
      GABBLE_IFACE_QUARK_CHANNEL_TYPE_CALL,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      call_props);
}

void
gabble_base_call_channel_dispose (GObject *object)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (object);
  GabbleBaseCallChannelPrivate *priv = self->priv;

  DEBUG ("hello thar");

  if (priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  g_list_foreach (priv->contents, (GFunc) gabble_base_call_content_deinit, NULL);
  tp_clear_pointer (&priv->members, g_hash_table_unref);
  tp_clear_pointer (&priv->contents, g_list_free);

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
  g_free (self->priv->object_path_prefix);
  g_free (self->priv->initial_audio_name);
  g_free (self->priv->initial_video_name);

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

  if (tp_base_channel_is_registered (TP_BASE_CHANNEL (self)))
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

  priv->contents = g_list_remove (priv->contents, content);

  path = gabble_base_call_content_get_object_path (
      GABBLE_BASE_CALL_CONTENT (content));
  gabble_svc_channel_type_call_emit_content_removed (self, path);

  gabble_base_call_content_deinit (GABBLE_BASE_CALL_CONTENT (content));
}

GabbleCallContent *
gabble_base_call_channel_add_content (GabbleBaseCallChannel *self,
    const gchar *name,
    JingleMediaType mtype,
    GabbleCallContentDisposition disposition)
{
  GabbleBaseCallChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  gchar *object_path;
  GabbleBaseCallContent *content;
  gchar *escaped;

  /* FIXME could clash when other party in a one-to-one call creates a stream
   * with the same media type and name */
  escaped = tp_escape_as_identifier (name);
  object_path = g_strdup_printf ("%s/Content_%s_%d",
      tp_base_channel_get_object_path (base),
      escaped, mtype);
  g_free (escaped);

  content = g_object_new (GABBLE_TYPE_CALL_CONTENT,
    "connection", tp_base_channel_get_connection (base),
    "object-path", object_path,
    "disposition", disposition,
    "media-type", jingle_media_type_to_tp (mtype),
    "name", name,
    NULL);

  g_free (object_path);

  g_signal_connect_swapped (content, "removed",
      G_CALLBACK (base_call_channel_remove_content), self);

  priv->contents = g_list_prepend (priv->contents, content);

  gabble_svc_channel_type_call_emit_content_added (self,
     gabble_base_call_content_get_object_path (content));

  gabble_call_content_new_offer (GABBLE_CALL_CONTENT (content));

  return GABBLE_CALL_CONTENT (content);
}

static void
gabble_base_call_channel_close (TpBaseChannel *base)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (base);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  GHashTableIter iter;
  gpointer value;

  DEBUG ("Closing media channel %s", tp_base_channel_get_object_path (base));

  g_hash_table_iter_init (&iter, priv->members);

  while (g_hash_table_iter_next (&iter, NULL, &value))
    gabble_call_member_shutdown (value);

  /* shutdown all our contents */
  g_list_foreach (priv->contents, (GFunc) gabble_base_call_content_deinit,
      NULL);
  g_list_free (priv->contents);
  priv->contents = NULL;

  tp_base_channel_destroyed (base);
}

static void
gabble_base_call_channel_set_ringing (GabbleSvcChannelTypeCall *iface,
    DBusGMethodInvocation *context)
{
  GabbleBaseCallChannel *self = GABBLE_BASE_CALL_CHANNEL (iface);
  GabbleBaseCallChannelPrivate *priv = self->priv;
  TpBaseChannel *tp_base = TP_BASE_CHANNEL (self);

  if (tp_base_channel_is_requested (tp_base))
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

      gabble_svc_channel_type_call_return_from_set_ringing (context);
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
  TpBaseChannel *tp_base = TP_BASE_CHANNEL (self);

  DEBUG ("Client accepted the call");

  if (tp_base_channel_is_requested (tp_base))
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
      gabble_base_call_content_get_object_path (
          GABBLE_BASE_CALL_CONTENT (content)));
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
  IMPLEMENT(set_ringing,);
  IMPLEMENT(accept,);
  IMPLEMENT(hangup,);
  IMPLEMENT(add_content, _dbus);
#undef IMPLEMENT
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
