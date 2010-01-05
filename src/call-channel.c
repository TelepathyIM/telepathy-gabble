/*
 * gabble-call-channel.c - Source for GabbleCallChannel
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
#include "call-channel.h"
#include "call-content.h"

#include "connection.h"
#include "jingle-session.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void channel_iface_init (gpointer, gpointer);
static void call_iface_init (gpointer, gpointer);
static void async_initable_iface_init (GAsyncInitableIface *iface);

static void call_channel_setup (GabbleCallChannel *self);
static const char *call_channel_add_content (GabbleCallChannel *self,
  GabbleJingleContent *c, GabbleCallContentDisposition disposition);

static void call_session_state_changed_cb (GabbleJingleSession *session,
  GParamSpec *param, GabbleCallChannel *self);

G_DEFINE_TYPE_WITH_CODE(GabbleCallChannel, gabble_call_channel,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
  G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_CALL,
        call_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL)
);

static const gchar *gabble_call_channel_interfaces[] = {
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

  PROP_SESSION,
  LAST_PROPERTY
};


/* private structure */
struct _GabbleCallChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  GabbleJingleSession *session;

  TpHandle creator;
  TpHandle target;

  gboolean closed;

  gboolean initial_audio;
  gboolean initial_video;
  gboolean registered;
  gboolean requested;

  gboolean dispose_has_run;

  GList *contents;

  gchar *transport_ns;

  GabbleCallState state;
  guint flags;
  GHashTable *details;
  GValueArray *reason;

  GHashTable *members;
};

static void
gabble_call_channel_constructed (GObject *obj)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (obj);
  GabbleCallChannelPrivate *priv = self->priv;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_handles;

  conn = (TpBaseConnection *) priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  priv->requested = (priv->session == NULL);

  if (priv->session != NULL)
      priv->creator = priv->session->peer;
  else
      priv->creator = conn->self_handle;

  g_hash_table_insert (priv->members,
    GUINT_TO_POINTER (priv->target),
    GUINT_TO_POINTER (0));

  /* automatically add creator to channel, but also ref them again (because
   * priv->creator is the InitiatorHandle) */
  g_assert (priv->creator != 0);
  tp_handle_ref (contact_handles, priv->creator);

  if (priv->session != NULL)
    {
      GList *contents, *l;
      contents = gabble_jingle_session_get_contents (priv->session);

      gabble_signal_connect_weak (priv->session, "notify::state",
        G_CALLBACK (call_session_state_changed_cb), obj);

      for (l = contents; l != NULL; l = g_list_next (l))
        {
          GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (l->data);
          JingleMediaType mtype;

          if (priv->transport_ns == NULL)
            g_object_get (content, "transport-ns",
              &priv->transport_ns,
              NULL);

          call_channel_add_content (self, content,
            GABBLE_CALL_CONTENT_DISPOSITION_INITIAL);

          g_object_get (content, "media-type", &mtype, NULL);
          switch (mtype)
            {
              case JINGLE_MEDIA_TYPE_AUDIO:
                priv->initial_audio = TRUE;
                break;
              case JINGLE_MEDIA_TYPE_VIDEO:
                priv->initial_video = TRUE;
                break;
              default:
                break;
            }
        }
    }

  if (priv->requested)
    priv->state = GABBLE_CALL_STATE_PENDING_INITIATOR;
  else
    priv->state = GABBLE_CALL_STATE_PENDING_RECEIVER;

  if (G_OBJECT_CLASS (gabble_call_channel_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_channel_parent_class)->constructed (obj);
}

static void
gabble_call_channel_init (GabbleCallChannel *self)
{
  GabbleCallChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_CHANNEL, GabbleCallChannelPrivate);

  self->priv = priv;

  priv->reason = gabble_value_array_build (3,
    G_TYPE_UINT, 0,
    G_TYPE_UINT, 0,
    G_TYPE_STRING, "",
    G_TYPE_INVALID);

  priv->details = tp_asv_new (NULL, NULL);

  priv->members = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void gabble_call_channel_dispose (GObject *object);
static void gabble_call_channel_finalize (GObject *object);

static void
gabble_call_channel_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallChannel *chan = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = chan->priv;
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, GABBLE_IFACE_CHANNEL_TYPE_CALL);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_TARGET_HANDLE:
        g_value_set_uint (value, priv->target);
        break;
      case PROP_TARGET_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);
          const gchar *target_id = tp_handle_inspect (repo, priv->target);

          g_value_set_string (value, target_id);
        }
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
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
        g_value_set_boxed (value, gabble_call_channel_interfaces);
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
      case PROP_SESSION:
        g_value_set_object (value, priv->session);
        break;
      case PROP_INITIAL_AUDIO:
        g_value_set_boolean (value, priv->initial_audio);
        break;
      case PROP_INITIAL_VIDEO:
        g_value_set_boolean (value, priv->initial_video);
        break;
      case PROP_MUTABLE_CONTENTS:
        g_value_set_boolean (value,
           gabble_jingle_session_can_modify_contents (priv->session));
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
        g_value_set_boxed (value, priv->members);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallChannel *chan = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = chan->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_HANDLE_TYPE:
      case PROP_CHANNEL_TYPE:
        /* these properties are writable in the interface, but not actually
        * meaningfully changable on this channel, so we do nothing */
        break;
      case PROP_TARGET_HANDLE:
        priv->target = g_value_get_uint (value);
        break;
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_CREATOR:
        priv->creator = g_value_get_uint (value);
        break;
      case PROP_SESSION:
        g_assert (priv->session == NULL);
        priv->session = g_value_dup_object (value);
        break;
      case PROP_INITIAL_AUDIO:
        priv->initial_audio = g_value_get_boolean (value);
        break;
      case PROP_INITIAL_VIDEO:
        priv->initial_video = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}


static void
gabble_call_channel_class_init (
    GabbleCallChannelClass *gabble_call_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_channel_class);
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

  g_type_class_add_private (gabble_call_channel_class,
      sizeof (GabbleCallChannelPrivate));

  object_class->constructed = gabble_call_channel_constructed;

  object_class->get_property = gabble_call_channel_get_property;
  object_class->set_property = gabble_call_channel_set_property;

  object_class->dispose = gabble_call_channel_dispose;
  object_class->finalize = gabble_call_channel_finalize;

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
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_object ("session", "GabbleJingleSession object",
      "Jingle session associated with this media channel object.",
      GABBLE_TYPE_JINGLE_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);

  param_spec = g_param_spec_boolean ("initial-audio", "InitialAudio",
      "Whether the channel initially contained an audio stream",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_AUDIO,
      param_spec);

  param_spec = g_param_spec_boolean ("initial-video", "InitialVideo",
      "Whether the channel initially contained an video stream",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
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

  gabble_call_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleCallChannelClass, dbus_props_class));
}

void
gabble_call_channel_dispose (GObject *object)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = self->priv;
  GList *l;

  if (priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  for (l = priv->contents; l != NULL; l = g_list_next (l))
    {
      g_object_unref (l->data);
    }

  g_list_free (priv->contents);
  priv->contents = NULL;

  if (G_OBJECT_CLASS (gabble_call_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_channel_parent_class)->dispose (object);
}

void
gabble_call_channel_finalize (GObject *object)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = self->priv;

  g_hash_table_unref (priv->details);
  g_value_array_free (priv->reason);
  g_free (self->priv->object_path);
  g_free (self->priv->transport_ns);

  g_hash_table_unref (self->priv->members);

  G_OBJECT_CLASS (gabble_call_channel_parent_class)->finalize (object);
}

static void
emit_call_state_changed (GabbleCallChannel *self)
{
  GabbleCallChannelPrivate *priv = self->priv;

  gabble_svc_channel_type_call_emit_call_state_changed (self, priv->state,
    priv->flags, priv->reason, priv->details);
}

static void
call_session_state_changed_cb (GabbleJingleSession *session,
  GParamSpec *param,
  GabbleCallChannel *self)
{
  GabbleCallChannelPrivate *priv = self->priv;
  JingleSessionState state;

  g_object_get (session, "state", &state, NULL);

  if (state == JS_STATE_ACTIVE && priv->state != GABBLE_CALL_STATE_ACCEPTED)
    {
      priv->state = GABBLE_CALL_STATE_ACCEPTED;
      emit_call_state_changed (self);

      return;
    }

  if (state == JS_STATE_ENDED && priv->state < GABBLE_CALL_STATE_ENDED)
    {
      priv->state = GABBLE_CALL_STATE_ENDED;
      emit_call_state_changed (self);
      return;
    }
}

static const gchar *
call_channel_add_content (GabbleCallChannel *self,
  GabbleJingleContent *c,
  GabbleCallContentDisposition disposition)
{
  GabbleCallChannelPrivate *priv = self->priv;
  gchar *object_path;
  GabbleCallContent *content;
  TpHandle creator;

  object_path = g_strdup_printf ("%s/Content%p", priv->object_path, c);

  if (gabble_jingle_content_is_created_by_us (c))
    creator = ((TpBaseConnection *) priv->conn)->self_handle;
  else
    creator = priv->session->peer;

  content = g_object_new (GABBLE_TYPE_CALL_CONTENT,
    "connection", priv->conn,
    "object-path", object_path,
    "jingle-content", c,
    "target-handle", priv->target,
    "disposition", disposition,
    "creator", creator,
    NULL);

  g_free (object_path);

  priv->contents = g_list_prepend (priv->contents, content);

  return gabble_call_content_get_object_path (content);
}

static const gchar *
call_channel_create_content (GabbleCallChannel *self,
    const gchar *name,
    JingleMediaType type,
    GabbleCallContentDisposition disposition,
    GError **error)
{
  GabbleCallChannelPrivate *priv = self->priv;
  const gchar *content_ns;
  GabbleJingleContent *c;
  const gchar *peer_resource;

  peer_resource = gabble_jingle_session_get_peer_resource (priv->session);

  if (peer_resource[0] != '\0')
    DEBUG ("existing call, using peer resource %s", peer_resource);
  else
    DEBUG ("existing call, using bare JID");

  content_ns = jingle_pick_best_content_type (priv->conn, priv->target,
    peer_resource, type);

  if (content_ns == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "Content type %d not available for this resource", type);
      return NULL;
    }

  DEBUG ("Creating new jingle content with ns %s : %s",
    content_ns, priv->transport_ns);

  c = gabble_jingle_session_add_content (priv->session,
      type, name, content_ns, priv->transport_ns);

  g_assert (c != NULL);

  return call_channel_add_content (self, c, disposition);
}


static void
call_channel_setup (GabbleCallChannel *self)
{
  GabbleCallChannelPrivate *priv = self->priv;
  DBusGConnection *bus;

  /* register object on the bus */
  bus = tp_get_bus ();
  DEBUG ("Registering %s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path,
    G_OBJECT (self));

  priv->registered = TRUE;
}

void
gabble_call_channel_close (GabbleCallChannel *self)
{
  GabbleCallChannelPrivate *priv = self->priv;
  DEBUG ("Closing media channel %s", self->priv->object_path);

  if (!priv->closed)
    {
      priv->closed = TRUE;
      if (priv->session != NULL)
        gabble_jingle_session_terminate (priv->session,
            TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL, NULL);

      tp_svc_channel_emit_closed (self);
    }
}

/**
 * gabble_call_channel_close_async:
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_call_channel_close_async (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (iface);

  DEBUG ("called");
  gabble_call_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

/**
 * gabble_call_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_call_channel_get_channel_type (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      GABBLE_IFACE_CHANNEL_TYPE_CALL);
}

/**
 * gabble_call_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_call_channel_get_handle (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
    GABBLE_CALL_CHANNEL (iface)->priv->target);
}

/**
 * gabble_call_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_call_channel_get_interfaces (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      gabble_call_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_call_channel_##x##suffix)
    IMPLEMENT(close,_async);
    IMPLEMENT(get_channel_type,);
    IMPLEMENT(get_handle,);
    IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
gabble_call_channel_ringing (GabbleSvcChannelTypeCall *iface,
    DBusGMethodInvocation *context)
{
  DEBUG ("Client is ringing");
  gabble_svc_channel_type_call_return_from_ringing (context);
}

static void
gabble_call_channel_accept (GabbleSvcChannelTypeCall *iface,
        DBusGMethodInvocation *context)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (iface);
  GabbleCallChannelPrivate *priv = self->priv;

  DEBUG ("Client accepted the call");

  if (priv->requested)
    {
      if (priv->state == GABBLE_CALL_STATE_PENDING_INITIATOR)
        {
          priv->state = GABBLE_CALL_STATE_PENDING_RECEIVER;
          emit_call_state_changed (self);
        }
    }
  else if (priv->state < GABBLE_CALL_STATE_ACCEPTED)
    {
      priv->state = GABBLE_CALL_STATE_ACCEPTED;
      emit_call_state_changed (self);
    }

  gabble_jingle_session_accept (self->priv->session);

  gabble_svc_channel_type_call_return_from_accept (context);
}

static void
gabble_call_channel_hangup (GabbleSvcChannelTypeCall *iface,
  guint reason,
  const gchar *detailed_reason,
  const gchar *message,
  DBusGMethodInvocation *context)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (iface);
  GabbleCallChannelPrivate *priv = self->priv;
  GError *error = NULL;

  if (!gabble_jingle_session_terminate (priv->session,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE,
      message, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  gabble_svc_channel_type_call_return_from_hangup (context);
}

static void
gabble_call_channel_add_content (GabbleSvcChannelTypeCall *iface,
  const gchar *name,
  TpMediaStreamType mtype,
  DBusGMethodInvocation *context)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (iface);
  JingleMediaType type = JINGLE_MEDIA_TYPE_NONE;
  const char *path;
  GError *error = NULL;

  if (mtype == TP_MEDIA_STREAM_TYPE_AUDIO)
    type = JINGLE_MEDIA_TYPE_AUDIO;
  else if (mtype == TP_MEDIA_STREAM_TYPE_VIDEO)
    type = JINGLE_MEDIA_TYPE_VIDEO;
  else
    goto unicorns;

  path = call_channel_create_content (self, name, type,
    GABBLE_CALL_CONTENT_DISPOSITION_NONE, &error);

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

#define IMPLEMENT(x) gabble_svc_channel_type_call_implement_##x (\
    klass, gabble_call_channel_##x)
  IMPLEMENT(ringing);
  IMPLEMENT(accept);
  IMPLEMENT(hangup);
  IMPLEMENT(add_content);
#undef IMPLEMENT
}


static void
remote_state_changed_cb (GabbleJingleSession *session, gpointer user_data)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (user_data);
  GabbleCallChannelPrivate *priv = self->priv;
  GabbleCallMemberFlags oldflags, newflags = 0;
  GArray *empty;

  if (gabble_jingle_session_get_remote_ringing (priv->session))
    newflags |= GABBLE_CALL_MEMBER_FLAG_RINGING;

  if (gabble_jingle_session_get_remote_hold (priv->session))
    newflags |= GABBLE_CALL_MEMBER_FLAG_HELD;

  oldflags = GPOINTER_TO_UINT (g_hash_table_lookup (priv->members,
    GUINT_TO_POINTER (priv->target)));

  if (oldflags == newflags)
    return;

  g_hash_table_insert (priv->members, GUINT_TO_POINTER (priv->target),
    GUINT_TO_POINTER (newflags));

  empty = g_array_new (TRUE, TRUE, sizeof (TpHandle));
  gabble_svc_channel_type_call_emit_call_members_changed (self,
    priv->members, empty);
  g_array_unref (empty);
}


static void
call_channel_init_async (GAsyncInitable *initable,
  int priority,
  GCancellable *cancellable,
  GAsyncReadyCallback callback,
  gpointer user_data)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (initable);
  GabbleCallChannelPrivate *priv = self->priv;
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (self),
    callback, user_data, NULL);

  if (priv->registered)
    goto out;

  if (priv->session == NULL)
    {
      const gchar *resource;
      JingleDialect dialect;
      const gchar *transport;

      /* FIXME might need to wait on capabilities, also don't need transport
       * and dialect already */
      resource = jingle_pick_best_resource (priv->conn,
        priv->target, priv->initial_audio, priv->initial_video,
        &transport, &dialect);

      if (resource == NULL)
        {
          g_simple_async_result_set_error (result, TP_ERRORS,
            TP_ERROR_NOT_CAPABLE,
            "member does not have the desired audio/video capabilities");
          goto out;
        }

      priv->transport_ns = g_strdup (transport);
      priv->session = gabble_jingle_factory_create_session (
        priv->conn->jingle_factory, priv->target, resource, FALSE);

      gabble_signal_connect_weak (priv->session, "notify::state",
        G_CALLBACK (call_session_state_changed_cb), G_OBJECT (self));

      g_object_set (priv->session, "dialect", dialect, NULL);

      /* Setup the session and the initial contents */
      if (priv->initial_audio)
        call_channel_create_content (self, "Audio", JINGLE_MEDIA_TYPE_AUDIO,
          GABBLE_CALL_CONTENT_DISPOSITION_INITIAL, NULL);

      if (priv->initial_video)
        call_channel_create_content (self, "Video", JINGLE_MEDIA_TYPE_VIDEO,
          GABBLE_CALL_CONTENT_DISPOSITION_INITIAL, NULL);
    }

  call_channel_setup (self);

  gabble_signal_connect_weak (priv->session, "remote-state-changed",
    G_CALLBACK (remote_state_changed_cb), G_OBJECT (self));

out:
  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = call_channel_init_async;
}
