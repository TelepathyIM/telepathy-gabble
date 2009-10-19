/*
 * media-channel-internal.h - implementation details shared between
 *                            MediaChannel source files
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

#ifndef __GABBLE_MEDIA_CHANNEL_INTERNAL_H__
#define __GABBLE_MEDIA_CHANNEL_INTERNAL_H__

#include "media-channel.h"

#include <glib.h>

#include "media-stream.h"
#include "jingle-session.h"
#include "jingle-media-rtp.h"

G_BEGIN_DECLS

struct _GabbleMediaChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  TpHandle creator;
  TpHandle initial_peer;
  gboolean peer_in_rp;

  GabbleJingleSession *session;

  /* array of referenced GabbleMediaStream*.  Always non-NULL. */
  GPtrArray *streams;
  /* list of PendingStreamRequest* in no particular order */
  GList *pending_stream_requests;

  /* list of StreamCreationData* in no particular order */
  GList *stream_creation_datas;

  guint next_stream_id;

  TpLocalHoldState hold_state;
  TpLocalHoldStateReason hold_state_reason;

  TpChannelCallStateFlags call_state;

  GPtrArray *delayed_request_streams;

  gboolean initial_audio;
  gboolean initial_video;
  gboolean immutable_streams;

  /* These are really booleans, but gboolean is signed. Thanks, GLib */
  unsigned ready:1;
  unsigned closed:1;
  unsigned dispose_has_run:1;
};

void gabble_media_channel_hold_latch_to_session (GabbleMediaChannel *chan);

void gabble_media_channel_hold_new_stream (GabbleMediaChannel *chan,
    GabbleMediaStream *stream,
    GabbleJingleMediaRtp *content);
void gabble_media_channel_hold_stream_closed (GabbleMediaChannel *chan,
    GabbleMediaStream *stream);

void gabble_media_channel_hold_iface_init (gpointer g_iface,
    gpointer iface_data G_GNUC_UNUSED);

void gabble_media_channel_call_state_iface_init (gpointer g_iface,
    gpointer iface_data G_GNUC_UNUSED);

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_CHANNEL_INTERNAL_H__ */
