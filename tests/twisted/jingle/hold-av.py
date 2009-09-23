"""
Test the Hold API.
"""

from gabbletest import make_result_iq, sync_stream
from servicetest import (
    assertEquals, wrap_channel,
    make_channel_proxy, call_async, EventPattern, sync_dbus)

import constants as cs

from jingletest2 import JingleTest2, test_all_dialects

def mutable_stream_tests(jp, jt, q, bus, conn, stream, chan, handle):
    # ---- Test 13: while the call's on hold, we add a new stream ---
    # We shouldn't go off hold locally as a result, and the new StreamHandler
    # should tell s-e to hold the stream.

    pending_hold = [
        EventPattern('dbus-signal', signal='HoldStateChanged',
            predicate=lambda e: e.args[0] == cs.HS_PENDING_HOLD),
        ]
    q.forbid_events(pending_hold)

    call_async(q, chan.StreamedMedia, 'RequestStreams', handle,
        [cs.MEDIA_STREAM_TYPE_AUDIO])

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    audio_stream_path = e.args[0]
    audio_stream_handler = make_channel_proxy(conn, e.args[0],
            'Media.StreamHandler')

    # Syncing here to make sure SetStreamHeld happens after Ready...
    sync_dbus(bus, q, conn)

    audio_stream_handler.Ready(jt.get_audio_codecs_dbus())
    audio_stream_handler.NewNativeCandidate("fake",
            jt.get_remote_transports_dbus())
    audio_stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    q.expect_many(
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True],
            path=audio_stream_path),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[False],
            path=audio_stream_path),
        )

    assertEquals(cs.HS_HELD, chan.Hold.GetHoldState()[0])

    sync_dbus(bus, q, conn)

    # ---- Test 14: while the call's on hold, the peer adds a new stream ----
    # Again, we shouldn't go off hold locally as a result, and the new
    # StreamHandler should tell s-e to hold the stream.

    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.peer, 'content-add', [
            jp.Content('videostream', 'initiator', 'both', [
                jp.Description('video', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt.video_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    video_stream_path = e.args[0]
    video_stream_handler = make_channel_proxy(conn, e.args[0],
            'Media.StreamHandler')

    # Syncing here to make sure SetStreamHeld happens after Ready...
    sync_dbus(bus, q, conn)

    video_stream_handler.Ready(jt.get_video_codecs_dbus())
    video_stream_handler.NewNativeCandidate("fake",
            jt.get_remote_transports_dbus())
    video_stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    q.expect_many(
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True],
            path=video_stream_path),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[False],
            path=video_stream_path),
        )

    assertEquals(cs.HS_HELD, chan.Hold.GetHoldState()[0])

    sync_dbus(bus, q, conn)
    q.unforbid_events(pending_hold)



def test(jp, q, bus, conn, stream):
    # These are 0- (for old dialects) or 1- (for new dialects) element lists
    # that can be splatted into expect_many with *
    hold_event = jp.rtp_info_event_list("hold")
    unhold_event = jp.rtp_info_event_list("unhold")

    # Let's forbid them until we're ready to start holding, to check that
    # Gabble doesn't send spurious notifications.
    q.forbid_events(hold_event)
    q.forbid_events(unhold_event)

    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.peer])[0]
    path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.HT_CONTACT, handle, True)

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['Hold'])

    call_async(q, chan.StreamedMedia, 'RequestStreams', handle,
        [cs.MEDIA_STREAM_TYPE_AUDIO, cs.MEDIA_STREAM_TYPE_VIDEO])

    if not jp.can_do_video():
        # Video on GTalk? Not so much.
        e = q.expect('dbus-error', method='RequestStreams')
        # The spec and implemention say this should be NotAvailable, but wjt
        # thinks it should be NotCapable. The spec bug is #20920.
        name = e.error.get_dbus_name()
        #assert name == cs.NOT_CAPABLE, name
        return

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()


    e = q.expect('dbus-signal', signal='NewStreamHandler')

    # FIXME: we assume this one's the audio stream, just because we requested
    # that first
    audio_stream_path = e.args[0]
    audio_stream_handler = make_channel_proxy(conn, e.args[0],
            'Media.StreamHandler')

    audio_stream_handler.NewNativeCandidate("fake",
            jt.get_remote_transports_dbus())
    audio_stream_handler.Ready(jt.get_audio_codecs_dbus())
    audio_stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    video_stream_path = e.args[0]
    video_stream_handler = make_channel_proxy(conn, e.args[0],
            'Media.StreamHandler')

    video_stream_handler.NewNativeCandidate("fake",
            jt.get_remote_transports_dbus())
    video_stream_handler.Ready(jt.get_video_codecs_dbus())
    video_stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    stream.send(make_result_iq(stream, e.stanza))

    jt.parse_session_initiate(e.query)
    jt.accept()

    q.expect('stream-iq', iq_type='result')

    # ---- Test 1: GetHoldState returns unheld and unhold is a no-op ----

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state
    chan.Hold.RequestHold(False)

    # We're about to start holding, so remove the ban on <hold/>.
    sync_stream(q, stream)
    q.unforbid_events(hold_event)

    # ---- Test 2: successful hold ----

    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        *hold_event
        )

    call_async(q, audio_stream_handler, 'HoldState', True)
    call_async(q, video_stream_handler, 'HoldState', True)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )

    # ---- Test 3: GetHoldState returns held and hold is a no-op ----

    q.forbid_events(hold_event)

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state
    chan.Hold.RequestHold(True)

    sync_stream(q, stream)
    q.unforbid_events(hold_event)

    # ---- Test 4: successful unhold ----

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, audio_stream_handler, 'HoldState', False)
    call_async(q, video_stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *unhold_event
        )

    # ---- Test 5: GetHoldState returns False and unhold is a no-op ----

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state
    chan.Hold.RequestHold(False)

    # ---- Test 6: 3 parallel calls to hold ----

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state

    call_async(q, chan.Hold, 'RequestHold', True)
    call_async(q, chan.Hold, 'RequestHold', True)
    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        *hold_event
        )

    call_async(q, audio_stream_handler, 'HoldState', True)
    call_async(q, video_stream_handler, 'HoldState', True)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )

    # ---- Test 7: 3 parallel calls to unhold ----

    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    call_async(q, chan.Hold, 'RequestHold', False)
    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, audio_stream_handler, 'HoldState', False)
    call_async(q, video_stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *unhold_event
        )

    # ---- Test 8: hold, then change our minds before s-e has responded ----

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state

    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        *hold_event
        )

    # Gabble can't send <unhold/> until s-e confirms it has the resources
    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        )

    call_async(q, audio_stream_handler, 'HoldState', True)
    call_async(q, video_stream_handler, 'HoldState', True)
    call_async(q, audio_stream_handler, 'HoldState', False)

    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, video_stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *unhold_event
        )

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state

    # ---- Test 9: unhold, then change our minds before s-e has responded ----

    # Go to state "held" first
    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        *hold_event
        )
    call_async(q, audio_stream_handler, 'HoldState', True)
    call_async(q, video_stream_handler, 'HoldState', True)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )

    # Actually do test 9

    q.forbid_events(hold_event + unhold_event)

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state

    call_async(q, chan.Hold, 'RequestHold', False)
    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        )
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        )

    call_async(q, audio_stream_handler, 'HoldState', False)
    call_async(q, video_stream_handler, 'HoldState', False)
    call_async(q, audio_stream_handler, 'HoldState', True)
    call_async(q, video_stream_handler, 'HoldState', True)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )

    sync_stream(q, stream)
    q.unforbid_events(hold_event + unhold_event)

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state

    # ---- Test 10: attempting to unhold fails (both streams) ----

    q.forbid_events(hold_event + unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    call_async(q, audio_stream_handler, 'UnholdFailure')
    call_async(q, video_stream_handler, 'UnholdFailure')

    q.expect_many(
        EventPattern('dbus-return', method='UnholdFailure', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
        )

    sync_stream(q, stream)
    q.unforbid_events(hold_event + unhold_event)

    # ---- Test 11: attempting to unhold fails (first stream) ----

    q.forbid_events(hold_event + unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    call_async(q, audio_stream_handler, 'UnholdFailure')

    q.expect_many(
        EventPattern('dbus-return', method='UnholdFailure', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
        )

    sync_stream(q, stream)
    q.unforbid_events(hold_event + unhold_event)

    # ---- Test 12: attempting to unhold partially fails, so roll back ----

    q.forbid_events(hold_event + unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    call_async(q, audio_stream_handler, 'HoldState', False)
    q.expect('dbus-return', method='HoldState', value=())

    call_async(q, video_stream_handler, 'UnholdFailure')

    q.expect_many(
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        EventPattern('dbus-return', method='UnholdFailure', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
        )

    call_async(q, audio_stream_handler, 'HoldState', True)

    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
        )

    sync_stream(q, stream)
    q.unforbid_events(hold_event + unhold_event)

    if jp.has_mutable_streams():
        mutable_stream_tests(jp, jt, q, bus, conn, stream, chan, handle)

    # ---- The end ----

    chan.Group.RemoveMembers([self_handle], 'closed')

    # Test completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

if __name__ == '__main__':
    test_all_dialects(test)
