/*
 * media-channel-hold.c - Hold and CallState interface implementations
 * Copyright © 2006–2009 Collabora Ltd.
 * Copyright © 2006–2009 Nokia Corporation
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

#include "media-channel.h"
#include "media-channel-internal.h"

#include <telepathy-glib/channel-iface.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"
#include "util.h"

/*
 * Implementation of Channel.Interface.Hold, which deals with placing the peer
 * on and off hold.
 */

static void
stream_hold_state_changed (GabbleMediaStream *stream G_GNUC_UNUSED,
    GParamSpec *unused G_GNUC_UNUSED,
    gpointer data)
{
  GabbleMediaChannel *self = data;
  GabbleMediaChannelPrivate *priv = self->priv;
  gboolean all_held = TRUE, any_held = FALSE;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      gboolean its_hold;

      g_object_get (g_ptr_array_index (priv->streams, i),
          "local-hold", &its_hold,
          NULL);

      DEBUG ("Stream at index %u has local-hold=%u", i, (guint) its_hold);

      all_held = all_held && its_hold;
      any_held = any_held || its_hold;
    }

  DEBUG ("all_held=%u, any_held=%u", (guint) all_held, (guint) any_held);

  if (all_held && !any_held)
    {
      /* There are no streams, move to the desired state immediately */
      switch (priv->hold_state)
        {
        case TP_LOCAL_HOLD_STATE_PENDING_HOLD:
          DEBUG ("no streams, moving from pending hold to held");
          priv->hold_state = TP_LOCAL_HOLD_STATE_HELD;

          /* No need to touch the session: send_held (TRUE) is called as soon
           * as Hold is requested.
           */
          break;

        case TP_LOCAL_HOLD_STATE_PENDING_UNHOLD:
          DEBUG ("no streams, moving from pending unhold to unheld");
          priv->hold_state = TP_LOCAL_HOLD_STATE_UNHELD;

          if (priv->session != NULL)
            gabble_jingle_session_set_local_hold (priv->session, FALSE);

          break;

        default:
          /* nothing to change */
          return;
        }
    }
  else if (all_held)
    {
      /* Move to state HELD */
      switch (priv->hold_state)
        {
        case TP_LOCAL_HOLD_STATE_HELD:
          /* nothing changed */
          return;

        case TP_LOCAL_HOLD_STATE_PENDING_UNHOLD:
          /* This can happen if the user asks us to hold, then changes their
           * mind. We make no particular guarantees about stream states when
           * in PENDING_UNHOLD state, so keep claiming to be in that state */
          return;

        case TP_LOCAL_HOLD_STATE_PENDING_HOLD:
          /* We wanted to hold, and indeed we have. Yay! Keep whatever
           * reason code we used for going to PENDING_HOLD */
          priv->hold_state = TP_LOCAL_HOLD_STATE_HELD;
          break;

        case TP_LOCAL_HOLD_STATE_UNHELD:
          /* We were previously UNHELD. So why have we gone on hold now? */
          DEBUG ("Unexpectedly entered HELD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_HELD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
          break;
        }
    }
  else if (any_held)
    {
      switch (priv->hold_state)
        {
        case TP_LOCAL_HOLD_STATE_UNHELD:
          /* The streaming client has spontaneously changed its stream
           * state. Why? We just don't know */
          DEBUG ("Unexpectedly entered PENDING_UNHOLD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_UNHOLD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
          break;

        case TP_LOCAL_HOLD_STATE_HELD:
          /* Likewise */
          DEBUG ("Unexpectedly entered PENDING_HOLD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_HOLD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
          break;

        default:
          /* nothing particularly interesting - we're trying to change hold
           * state already, so nothing to signal */
          return;
        }
    }
  else
    {
      /* Move to state UNHELD */
      switch (priv->hold_state)
        {
        case TP_LOCAL_HOLD_STATE_UNHELD:
          /* nothing changed */
          return;

        case TP_LOCAL_HOLD_STATE_PENDING_HOLD:
          /* This can happen if the user asks us to unhold, then changes their
           * mind. We make no particular guarantees about stream states when
           * in PENDING_HOLD state, so keep claiming to be in that state */
          return;

        case TP_LOCAL_HOLD_STATE_PENDING_UNHOLD:
          /* We wanted to hold, and indeed we have. Yay! Keep whatever
           * reason code we used for going to PENDING_UNHOLD */
          priv->hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
          break;

        case TP_LOCAL_HOLD_STATE_HELD:
          /* We were previously HELD. So why have we gone off hold now? */
          DEBUG ("Unexpectedly entered UNHELD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
          break;
        }

      /* Tell the peer what's happened. */
      if (priv->session != NULL)
        gabble_jingle_session_set_local_hold (priv->session, FALSE);
    }

  tp_svc_channel_interface_hold_emit_hold_state_changed (self,
      priv->hold_state, priv->hold_state_reason);
}


static void
stream_unhold_failed (GabbleMediaStream *stream,
    gpointer data)
{
  GabbleMediaChannel *self = data;
  GabbleMediaChannelPrivate *priv = self->priv;
  guint i;

  DEBUG ("%p: %p", self, stream);

  /* Unholding failed - let's roll back to Hold state */
  priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_HOLD;
  priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_RESOURCE_NOT_AVAILABLE;
  tp_svc_channel_interface_hold_emit_hold_state_changed (self,
      priv->hold_state, priv->hold_state_reason);

  /* The stream's state may have changed from unheld to held, so re-poll.
   * It's possible that all streams are now held, in which case we can stop. */
  stream_hold_state_changed (stream, NULL, self);

  if (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD)
    return;

  /* There should be no need to notify the peer, who already thinks they're
   * on hold, so just tell the streaming client what to do. */

  for (i = 0; i < priv->streams->len; i++)
    {
      gabble_media_stream_hold (g_ptr_array_index (priv->streams, i),
          TRUE);
    }
}


void
gabble_media_channel_hold_stream_closed (GabbleMediaChannel *chan,
    GabbleMediaStream *stream)
{
  /* A stream closing might cause the "total" hold state to change:
   * if there's one held and one unheld, and the unheld one closes,
   * then our state changes from indeterminate to held. */
  stream_hold_state_changed (stream, NULL, chan);
}


/* Implements RequestHold on Telepathy.Channel.Interface.Hold */
static void
gabble_media_channel_request_hold (TpSvcChannelInterfaceHold *iface,
                                   gboolean hold,
                                   DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv = self->priv;
  GabbleJingleSession *session = priv->session;
  guint i;
  TpLocalHoldState old_state = priv->hold_state;

  DEBUG ("%p: RequestHold(%u)", self, !!hold);

  if (hold)
    {
      if (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD)
        {
          DEBUG ("No-op");
          tp_svc_channel_interface_hold_return_from_request_hold (context);
          return;
        }

      if (priv->hold_state == TP_LOCAL_HOLD_STATE_UNHELD && session != NULL)
        gabble_jingle_session_set_local_hold (session, TRUE);

      priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_HOLD;
    }
  else
    {
      if (priv->hold_state == TP_LOCAL_HOLD_STATE_UNHELD)
        {
          DEBUG ("No-op");
          tp_svc_channel_interface_hold_return_from_request_hold (context);
          return;
        }

      priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_UNHOLD;
    }

  if (old_state != priv->hold_state ||
      priv->hold_state_reason != TP_LOCAL_HOLD_STATE_REASON_REQUESTED)
    {
      tp_svc_channel_interface_hold_emit_hold_state_changed (self,
          priv->hold_state, TP_LOCAL_HOLD_STATE_REASON_REQUESTED);
      priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_REQUESTED;
    }

  if (priv->streams->len == 0)
    {
      /* No streams yet! We can go straight to the desired state. */
      stream_hold_state_changed (NULL, NULL, self);
    }
  else
    {
      /* Tell streaming client to release or reacquire resources */

      for (i = 0; i < priv->streams->len; i++)
        {
          gabble_media_stream_hold (g_ptr_array_index (priv->streams, i), hold);
        }
    }

  tp_svc_channel_interface_hold_return_from_request_hold (context);
}


/* Implements GetHoldState on Telepathy.Channel.Interface.Hold */
static void
gabble_media_channel_get_hold_state (TpSvcChannelInterfaceHold *iface,
                                     DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = (GabbleMediaChannel *) iface;
  GabbleMediaChannelPrivate *priv = self->priv;

  tp_svc_channel_interface_hold_return_from_get_hold_state (context,
      priv->hold_state, priv->hold_state_reason);
}


void
gabble_media_channel_hold_iface_init (gpointer g_iface,
    gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcChannelInterfaceHoldClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_hold_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(get_hold_state);
  IMPLEMENT(request_hold);
#undef IMPLEMENT
}


/*
 * Implementation of Channel.Interface.CallState, which indicates call states
 * from the peer (such as being put on or off hold, or that the peer's client
 * is ringing.
 */

static void
remote_state_changed_cb (GabbleJingleSession *session,
    GabbleMediaChannel *self)
{
  GabbleMediaChannelPrivate *priv = self->priv;
  TpChannelCallStateFlags call_state = 0;

  if (gabble_jingle_session_get_remote_hold (session))
    call_state |= TP_CHANNEL_CALL_STATE_HELD;

  if (gabble_jingle_session_get_remote_ringing (session))
    call_state |= TP_CHANNEL_CALL_STATE_RINGING;

  DEBUG ("Call state changed to %u (current state %u)", call_state,
      priv->call_state);

  if (call_state == priv->call_state)
    return;

  priv->call_state = call_state;

  tp_svc_channel_interface_call_state_emit_call_state_changed (self,
      priv->session->peer, call_state);
}


/* Implements GetCallStates on Channel.Interface.CallState */
static void
gabble_media_channel_get_call_states (TpSvcChannelInterfaceCallState *iface,
    DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = (GabbleMediaChannel *) iface;
  GHashTable *states = g_hash_table_new (g_direct_hash, g_direct_equal);

  if (self->priv->session != NULL)
    g_hash_table_insert (states, GUINT_TO_POINTER (self->priv->session->peer),
        GUINT_TO_POINTER (self->priv->call_state));

  tp_svc_channel_interface_call_state_return_from_get_call_states (context,
      states);

  g_hash_table_destroy (states);
}


void
gabble_media_channel_call_state_iface_init (gpointer g_iface,
    gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcChannelInterfaceCallStateClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_call_state_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(get_call_states);
#undef IMPLEMENT
}


/* Called by construct_stream to allow the Hold code to hook itself up to a new
 * stream.
 */
void
gabble_media_channel_hold_new_stream (GabbleMediaChannel *chan,
    GabbleMediaStream *stream,
    GabbleJingleMediaRtp *content)
{
  GObject *chan_o = (GObject *) chan;

  gabble_signal_connect_weak (stream, "unhold-failed",
      (GCallback) stream_unhold_failed, chan_o);
  gabble_signal_connect_weak (stream, "notify::local-hold",
      (GCallback) stream_hold_state_changed, chan_o);

  /* A stream being added might cause the "total" hold state to change */
  stream_hold_state_changed (stream, NULL, chan);
}

/* Called by _latch_to_session to allow the CallState code to hook itself up to
 * a new session.
 */
void
gabble_media_channel_hold_latch_to_session (GabbleMediaChannel *chan)
{
  g_assert (chan->priv->session != NULL);

  /* Watch the active/ringing/held state of the session so we can keep the call
   * state up to date.
   */
  gabble_signal_connect_weak (chan->priv->session, "remote-state-changed",
      (GCallback) remote_state_changed_cb, (GObject *) chan);
}
