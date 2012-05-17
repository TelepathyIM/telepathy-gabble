/*
 * gabble-call-channel.c - Source for GabbleCallChannel
 * Copyright (C) 2006, 2009 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/gtypes.h>

#include "util.h"
#include "call-channel.h"
#include "call-content.h"

#include "connection.h"
#include "jingle-session.h"
#include "jingle-tp-util.h"
#include "presence-cache.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

static void call_session_state_changed_cb (GabbleJingleSession *session,
  GParamSpec *param, GabbleCallChannel *self);
static void call_member_content_added_cb (GabbleCallMember *member,
    GabbleCallMemberContent *content, GabbleCallChannel *self);
static void call_member_content_removed_cb (GabbleCallMember *member,
    GabbleCallMemberContent *mcontent,
    GabbleBaseCallChannel *self);
static void call_session_terminated_cb (GabbleJingleSession *session,
    gboolean locally_terminated, JingleReason termination_reason,
    gchar *reason_text, GabbleCallChannel *self);

static void call_channel_accept (TpBaseMediaCallChannel *channel);
static TpBaseCallContent * call_channel_add_content (
    TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    TpMediaStreamDirection initial_direction,
    GError **error);
static void call_channel_hold_state_changed (TpBaseMediaCallChannel *self,
    TpLocalHoldState hold_state, TpLocalHoldStateReason hold_state_reason);


G_DEFINE_TYPE_WITH_CODE(GabbleCallChannel, gabble_call_channel,
  GABBLE_TYPE_BASE_CALL_CHANNEL,
  G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init);
);

/* properties */
enum
{
  PROP_SESSION = 1,
  LAST_PROPERTY
};


/* private structure */
struct _GabbleCallChannelPrivate
{
  gboolean dispose_has_run;

  /* Our only call member, owned by the base channel */
  GabbleCallMember *member;
  GabbleJingleSession *session;
};

static void
gabble_call_channel_constructed (GObject *obj)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (obj);
  GabbleCallChannelPrivate *priv = self->priv;
  GabbleBaseCallChannel *g_base = GABBLE_BASE_CALL_CHANNEL (obj);
  GabbleCallMember *member;

  member = gabble_base_call_channel_ensure_member_from_handle (g_base,
    tp_base_channel_get_target_handle (TP_BASE_CHANNEL (obj)));
  priv->member = member;

  if (priv->session != NULL)
    {
      GList *contents, *l;

      gabble_call_member_set_session (member, priv->session);

      gabble_signal_connect_weak (priv->session, "notify::state",
        G_CALLBACK (call_session_state_changed_cb), obj);
      gabble_signal_connect_weak (priv->session, "terminated",
        G_CALLBACK (call_session_terminated_cb), G_OBJECT (self));
      gabble_signal_connect_weak (member, "content-added",
        G_CALLBACK (call_member_content_added_cb), G_OBJECT (self));
      gabble_signal_connect_weak (member, "content-removed",
        G_CALLBACK (call_member_content_removed_cb), G_OBJECT (self));

      contents = gabble_call_member_get_contents (member);

      for (l = contents; l != NULL; l = g_list_next (l))
        {
          GabbleCallMemberContent *content =
            GABBLE_CALL_MEMBER_CONTENT (l->data);
          GabbleCallContent *c;

          c = gabble_base_call_channel_add_content (g_base,
                gabble_call_member_content_get_name (content),
                gabble_call_member_content_get_media_type (content),
                TP_CALL_CONTENT_DISPOSITION_INITIAL);

          gabble_call_content_add_member_content (c, content);
        }
    }

  if (G_OBJECT_CLASS (gabble_call_channel_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_channel_parent_class)->constructed (obj);
}

static void
gabble_call_channel_init (GabbleCallChannel *self)
{
  GabbleCallChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_CHANNEL, GabbleCallChannelPrivate);

  self->priv = priv;
}

static void gabble_call_channel_dispose (GObject *object);
static void gabble_call_channel_finalize (GObject *object);

static void
gabble_call_channel_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_SESSION:
        g_value_set_object (value, priv->session);
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
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_SESSION:
        g_assert (priv->session == NULL);
        priv->session = g_value_dup_object (value);
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
  TpBaseMediaCallChannelClass *tp_base_media_call_class =
      TP_BASE_MEDIA_CALL_CHANNEL_CLASS (gabble_call_channel_class);
  TpBaseCallChannelClass *tp_base_call_class =
      TP_BASE_CALL_CHANNEL_CLASS (gabble_call_channel_class);
  TpBaseChannelClass *base_channel_class =
      TP_BASE_CHANNEL_CLASS (gabble_call_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_call_channel_class,
      sizeof (GabbleCallChannelPrivate));

  object_class->constructed = gabble_call_channel_constructed;

  object_class->get_property = gabble_call_channel_get_property;
  object_class->set_property = gabble_call_channel_set_property;

  object_class->dispose = gabble_call_channel_dispose;
  object_class->finalize = gabble_call_channel_finalize;

  base_channel_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;

  tp_base_call_class->add_content = call_channel_add_content;

  tp_base_media_call_class->accept = call_channel_accept;
  tp_base_media_call_class->hold_state_changed =
      call_channel_hold_state_changed;

  param_spec = g_param_spec_object ("session", "GabbleJingleSession object",
      "Jingle session associated with this media channel object.",
      GABBLE_TYPE_JINGLE_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);
}

void
gabble_call_channel_dispose (GObject *object)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->session);

  if (G_OBJECT_CLASS (gabble_call_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_channel_parent_class)->dispose (object);
}

void
gabble_call_channel_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_call_channel_parent_class)->finalize (object);
}

static void
call_session_terminated_cb (GabbleJingleSession *session,
    gboolean locally_terminated, JingleReason termination_reason,
    gchar *reason_text, GabbleCallChannel *self)
{
  TpHandle actor;
  TpCallStateChangeReason call_reason = TP_CALL_STATE_CHANGE_REASON_UNKNOWN;
  const gchar *dbus_detail = "";

  if (tp_base_call_channel_get_state (TP_BASE_CALL_CHANNEL (self)) ==
      TP_CALL_STATE_ENDED)
    {
      DEBUG ("ignoring jingle session terminate, already in ENDED state");
      return;
    }

  if (reason_text == NULL)
    reason_text = "";

  if (locally_terminated)
    actor = tp_base_channel_get_self_handle (TP_BASE_CHANNEL (self));
  else
    actor = tp_base_channel_get_target_handle (TP_BASE_CHANNEL (self));

  switch (termination_reason)
    {
    case JINGLE_REASON_UNKNOWN:
      call_reason = TP_CALL_STATE_CHANGE_REASON_UNKNOWN;
      break;
    case JINGLE_REASON_BUSY:
      call_reason = TP_CALL_STATE_CHANGE_REASON_BUSY;
      break;
    case JINGLE_REASON_CANCEL:
      if (locally_terminated)
        call_reason = TP_CALL_STATE_CHANGE_REASON_USER_REQUESTED;
      else
        call_reason = TP_CALL_STATE_CHANGE_REASON_REJECTED;
      break;
    case JINGLE_REASON_CONNECTIVITY_ERROR:
      call_reason = TP_CALL_STATE_CHANGE_REASON_CONNECTIVITY_ERROR;
      break;
    case JINGLE_REASON_FAILED_APPLICATION:
      call_reason = TP_CALL_STATE_CHANGE_REASON_MEDIA_ERROR;
      dbus_detail = TP_ERROR_STR_MEDIA_CODECS_INCOMPATIBLE;
      break;
    case JINGLE_REASON_GENERAL_ERROR:
      call_reason = TP_CALL_STATE_CHANGE_REASON_SERVICE_ERROR;
      break;
    case JINGLE_REASON_GONE:
      /* This one is only in the media channel, we don't have a
       * Call reason to match */
      call_reason = TP_CALL_STATE_CHANGE_REASON_UNKNOWN;
      break;
    case JINGLE_REASON_MEDIA_ERROR:
      call_reason = TP_CALL_STATE_CHANGE_REASON_MEDIA_ERROR;
      break;
    case JINGLE_REASON_SUCCESS:
      call_reason = TP_CALL_STATE_CHANGE_REASON_USER_REQUESTED;
      break;
    case JINGLE_REASON_TIMEOUT:
      call_reason = TP_CALL_STATE_CHANGE_REASON_NO_ANSWER;
      break;
    case JINGLE_REASON_DECLINE:
      call_reason = TP_CALL_STATE_CHANGE_REASON_REJECTED;
      break;
    case JINGLE_REASON_ALTERNATIVE_SESSION:
      break;
    case JINGLE_REASON_UNSUPPORTED_TRANSPORTS:
      call_reason = TP_CALL_STATE_CHANGE_REASON_NETWORK_ERROR;
      break;
    case JINGLE_REASON_FAILED_TRANSPORT:
      call_reason = TP_CALL_STATE_CHANGE_REASON_CONNECTIVITY_ERROR;
      dbus_detail = TP_ERROR_STR_CONNECTION_FAILED;
      break;
    case JINGLE_REASON_INCOMPATIBLE_PARAMETERS:
      call_reason = TP_CALL_STATE_CHANGE_REASON_MEDIA_ERROR;
      dbus_detail = TP_ERROR_STR_MEDIA_CODECS_INCOMPATIBLE;
      break;
    case JINGLE_REASON_SECURITY_ERROR:
      call_reason = TP_CALL_STATE_CHANGE_REASON_CONNECTIVITY_ERROR;
      break;
    case JINGLE_REASON_UNSUPPORTED_APPLICATIONS:
      call_reason = TP_CALL_STATE_CHANGE_REASON_MEDIA_ERROR;
      dbus_detail = TP_ERROR_STR_MEDIA_UNSUPPORTED_TYPE;
      break;
    case JINGLE_REASON_EXPIRED:
      /* No matching error in our spec */
      call_reason = TP_CALL_STATE_CHANGE_REASON_UNKNOWN;
      break;
    default:
      call_reason = TP_CALL_STATE_CHANGE_REASON_UNKNOWN;
      break;
    }

  DEBUG ("Moving to ENDED state");
  tp_base_call_channel_set_state (TP_BASE_CALL_CHANNEL (self),
      TP_CALL_STATE_ENDED, actor, call_reason, dbus_detail, reason_text);
}

static void
call_session_state_changed_cb (GabbleJingleSession *session,
  GParamSpec *param,
  GabbleCallChannel *self)
{
  TpBaseCallChannel *cbase = TP_BASE_CALL_CHANNEL (self);
  JingleState state;

  g_object_get (session, "state", &state, NULL);

  if (state == JINGLE_STATE_ACTIVE && !tp_base_call_channel_is_accepted (cbase))
    {
      tp_base_call_channel_remote_accept (cbase);
    }
}

/* This function handles additional contents added by the remote side */
static void
call_member_content_added_cb (GabbleCallMember *member,
    GabbleCallMemberContent *content,
    GabbleCallChannel *self)
{
  GabbleBaseCallChannel *cbase = GABBLE_BASE_CALL_CHANNEL (self);
  GabbleJingleContent *jingle_content;
  GabbleCallContent *c;

  jingle_content = gabble_call_member_content_get_jingle_content (content);

  if (jingle_content == NULL ||
      gabble_jingle_content_is_created_by_us (jingle_content))
    return;

  c = gabble_base_call_channel_add_content (cbase,
      gabble_call_member_content_get_name (content),
      gabble_call_member_content_get_media_type (content),
      TP_CALL_CONTENT_DISPOSITION_NONE);

  gabble_call_content_add_member_content (c, content);
}

static gboolean
contact_is_media_capable (GabbleCallChannel *self,
    TpHandle handle,
    gboolean *wait_ret,
    GError **error)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);

  GabblePresence *presence;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  gboolean wait = FALSE;

  presence = gabble_presence_cache_get (conn->presence_cache, handle);

  if (presence != NULL)
    {
      const GabbleCapabilitySet *caps = gabble_presence_peek_caps (presence);

      if (gabble_capability_set_has_one (caps,
            gabble_capabilities_get_any_audio_video ()))
        return TRUE;
    }

  /* Okay, they're not capable (yet). Let's figure out whether we should wait,
   * and return an appropriate error.
   */
  if (gabble_presence_cache_is_unsure (conn->presence_cache, handle))
    {
      DEBUG ("presence cache is still unsure about handle %u", handle);
      wait = TRUE;
    }

  if (wait_ret != NULL)
    *wait_ret = wait;

  if (presence == NULL)
    g_set_error (error, TP_ERROR, TP_ERROR_OFFLINE,
        "contact %d (%s) has no presence available", handle,
        tp_handle_inspect (contact_handles, handle));
  else
    g_set_error (error, TP_ERROR, TP_ERROR_NOT_CAPABLE,
        "contact %d (%s) doesn't have sufficient media caps", handle,
        tp_handle_inspect (contact_handles, handle));

  return FALSE;
}

static void
call_member_content_removed_cb (GabbleCallMember *member,
    GabbleCallMemberContent *mcontent,
    GabbleBaseCallChannel *self)
{
  TpBaseCallChannel *cbase = TP_BASE_CALL_CHANNEL (self);
  GList *l;

  for (l = tp_base_call_channel_get_contents (cbase);
      l != NULL; l = g_list_next (l))
    {
      GabbleCallContent *content = GABBLE_CALL_CONTENT (l->data);
      GList *contents = gabble_call_content_get_member_contents (content);

      if (contents != NULL && contents->data == mcontent)
        {
          tp_base_call_channel_remove_content (cbase,
              TP_BASE_CALL_CONTENT (content),
              0, TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");
          break;
        }
    }
}

static void
call_channel_continue_init (GabbleCallChannel *self,
    GSimpleAsyncResult *result)
{
  GabbleCallChannelPrivate *priv = self->priv;
  TpBaseCallChannel *base = TP_BASE_CALL_CHANNEL (self);
  TpBaseChannel *tp_base = TP_BASE_CHANNEL (self);
  GError *error = NULL;
  gboolean initial_audio, initial_video;
  const gchar *initial_audio_name = NULL, *initial_video_name = NULL;

  if (priv->session == NULL)
    {
      GabbleCallMember *member;
      GList *contents, *l;

      member = gabble_base_call_channel_ensure_member_from_handle (
          GABBLE_BASE_CALL_CHANNEL (self),
          tp_base_channel_get_target_handle (tp_base));

      initial_audio = tp_base_call_channel_has_initial_audio (base,
          &initial_audio_name);
      initial_video = tp_base_call_channel_has_initial_video (base,
          &initial_video_name);

      if (!gabble_call_member_start_session (member,
          initial_audio ? initial_audio_name : NULL,
          initial_video ? initial_video_name : NULL,
          &error))
       {
         g_simple_async_result_set_from_error (result, error);
         g_error_free (error);
         goto out;
       }

      priv->session = g_object_ref (gabble_call_member_get_session (member));
      gabble_signal_connect_weak (priv->session, "notify::state",
        G_CALLBACK (call_session_state_changed_cb), G_OBJECT (self));
      gabble_signal_connect_weak (priv->session, "terminated",
        G_CALLBACK (call_session_terminated_cb), G_OBJECT (self));

      contents = gabble_call_member_get_contents (member);

      for (l = contents; l != NULL; l = g_list_next (l))
        {
          GabbleCallMemberContent *content =
            GABBLE_CALL_MEMBER_CONTENT (l->data);
          GabbleCallContent *c;

          c = gabble_base_call_channel_add_content (
                GABBLE_BASE_CALL_CHANNEL (self),
                gabble_call_member_content_get_name (content),
                gabble_call_member_content_get_media_type (content),
                TP_CALL_CONTENT_DISPOSITION_INITIAL);

          gabble_call_content_add_member_content (c, content);
        }

      gabble_signal_connect_weak (member, "content-added",
        G_CALLBACK (call_member_content_added_cb), G_OBJECT (self));
      gabble_signal_connect_weak (member, "content-removed",
        G_CALLBACK (call_member_content_removed_cb), G_OBJECT (self));
    }

  tp_base_channel_register (tp_base);

out:
  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static void
call_channel_capabilities_discovered_cb (GabblePresenceCache *cache,
    TpHandle handle,
    gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (
      g_async_result_get_source_object (G_ASYNC_RESULT (result)));
  TpBaseChannel *tp_base = TP_BASE_CHANNEL (self);
  TpHandle target = tp_base_channel_get_target_handle (tp_base);
  GError *error_ = NULL;
  gboolean wait;

  if (target != handle || tp_base_channel_is_registered (tp_base))
    goto out;

  if (!contact_is_media_capable (self, target, &wait, &error_))
    {
      if (wait)
        {
          DEBUG ("contact %u caps still pending", target);
          g_error_free (error_);
        }
      else
        {
          DEBUG ("%u: %s", error_->code, error_->message);
          g_simple_async_result_set_from_error (result, error_);
          g_error_free (error_);
          g_simple_async_result_complete_in_idle (result);
          g_object_unref (result);
        }
    }
  else
    {
      call_channel_continue_init (self, result);
    }

out:
  g_object_unref (self);
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
  GabbleBaseCallChannel *base = GABBLE_BASE_CALL_CHANNEL (self);
  TpBaseChannel *tp_base = TP_BASE_CHANNEL (base);
  TpHandle target = tp_base_channel_get_target_handle (tp_base);
  GabbleConnection *conn = GABBLE_CONNECTION (
      tp_base_channel_get_connection (tp_base));
  GSimpleAsyncResult *result;
  GError *error_ = NULL;
  gboolean wait;

  result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, NULL);

  if (tp_base_channel_is_registered (tp_base))
    {
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
      return;
    }

  if (priv->session == NULL &&
      !contact_is_media_capable (self, target, &wait, &error_))
    {
      if (wait)
        {
          DEBUG ("contact %u caps still pending, adding anyways", target);
          g_error_free (error_);

          /* Fuckers */
          gabble_signal_connect_weak (conn->presence_cache,
              "capabilities-discovered",
              G_CALLBACK (call_channel_capabilities_discovered_cb),
              G_OBJECT (result));
        }
      else
        {
          DEBUG ("%u: %s", error_->code, error_->message);
          g_simple_async_result_set_from_error (result, error_);
          g_error_free (error_);
          g_simple_async_result_complete_in_idle (result);
          g_object_unref (result);
        }
    }
  else
    {
      call_channel_continue_init (self, result);
    }
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = call_channel_init_async;
}

static void
call_channel_accept (TpBaseMediaCallChannel *channel)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (channel);
  gabble_jingle_session_accept (self->priv->session);
}

static TpBaseCallContent *
call_channel_add_content (TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    TpMediaStreamDirection initial_direction,
    GError **error)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (base);
  GabbleCallContent *content = NULL;
  GabbleCallMemberContent *mcontent;
  JingleContentSenders senders;
  gboolean initiated_by_us;

  if (initial_direction == TP_MEDIA_STREAM_DIRECTION_NONE)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Jingle can not do contents with direction = NONE");
      return NULL;
    }

  g_object_get (self->priv->session, "local-initiator", &initiated_by_us,
      NULL);

  switch (initial_direction)
    {
    case TP_MEDIA_STREAM_DIRECTION_SEND:
      senders = initiated_by_us ?
          JINGLE_CONTENT_SENDERS_INITIATOR : JINGLE_CONTENT_SENDERS_RESPONDER;
      break;
    case TP_MEDIA_STREAM_DIRECTION_RECEIVE:
      senders = initiated_by_us ?
          JINGLE_CONTENT_SENDERS_RESPONDER : JINGLE_CONTENT_SENDERS_INITIATOR;
      break;
    default:
    case TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL:
      senders = JINGLE_CONTENT_SENDERS_BOTH;
    }

  mcontent = gabble_call_member_create_content (self->priv->member, name,
      jingle_media_type_from_tp (type), senders, error);

  if (mcontent != NULL)
    {
      content = gabble_base_call_channel_add_content (
        GABBLE_BASE_CALL_CHANNEL (base),
        name, jingle_media_type_from_tp (type),
        TP_CALL_CONTENT_DISPOSITION_NONE);
      gabble_call_content_add_member_content (content, mcontent);
    }

  return TP_BASE_CALL_CONTENT (content);
}

static void
call_channel_hold_state_changed (TpBaseMediaCallChannel *bmcc,
    TpLocalHoldState hold_state, TpLocalHoldStateReason hold_state_reason)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (bmcc);

  switch (hold_state)
    {
    case TP_LOCAL_HOLD_STATE_HELD:
    case TP_LOCAL_HOLD_STATE_PENDING_HOLD:
      gabble_jingle_session_set_local_hold (self->priv->session, TRUE);
      break;
    case TP_LOCAL_HOLD_STATE_UNHELD:
      gabble_jingle_session_set_local_hold (self->priv->session, FALSE);
      break;
    case TP_LOCAL_HOLD_STATE_PENDING_UNHOLD:
      break;
    default:
      g_assert_not_reached ();
    }
}
