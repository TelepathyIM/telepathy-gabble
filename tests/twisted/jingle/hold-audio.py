"""
Test the Hold API.
"""

from gabbletest import make_result_iq, sync_stream
from servicetest import (
    assertEquals, sync_dbus,
    make_channel_proxy, call_async, EventPattern, wrap_channel,
    )
import constants as cs

from jingletest2 import JingleTest2, test_all_dialects

def test(jp, q, bus, conn, stream):
    remote_jid = 'foo@bar.com/Foo'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]
    path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT,
        handle, True)

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['Hold'])

    # These are 0- (for old dialects) or 1- (for new dialects) element lists
    # that can be splatted into expect_many with *
    hold_event = jp.rtp_info_event_list("hold")
    unhold_event = jp.rtp_info_event_list("unhold")

    # Before we have any streams, GetHoldState returns Unheld and unhold is a
    # no-op.
    assertEquals((cs.HS_UNHELD, cs.HSR_NONE), chan.Hold.GetHoldState())
    chan.Hold.RequestHold(False)

    # Before we have any streams, RequestHold(True) should work; because there
    # are no streams, it should take effect at once. It certainly shouldn't
    # send anything to the peer.
    q.forbid_events(hold_event)
    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect('dbus-signal', signal='HoldStateChanged',
        args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED])
    q.expect('dbus-signal', signal='HoldStateChanged',
        args=[cs.HS_HELD, cs.HSR_REQUESTED])
    assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())

    # If we unhold, it should succeed immediately again, because there are no
    # resources to reclaim.
    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect('dbus-signal', signal='HoldStateChanged',
        args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED])
    q.expect('dbus-signal', signal='HoldStateChanged',
        args=[cs.HS_UNHELD, cs.HSR_REQUESTED])
    assertEquals((cs.HS_UNHELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())

    # Put the call back on hold ...
    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect('dbus-signal', signal='HoldStateChanged',
        args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED])
    q.expect('dbus-signal', signal='HoldStateChanged',
        args=[cs.HS_HELD, cs.HSR_REQUESTED])
    assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())

    # ... and request a stream.
    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # Syncing here to make sure SetStreamHeld happens after Ready...
    sync_dbus(bus, q, conn)
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    # Now Gabble tells the streaming implementation to go on hold (because it
    # said it was Ready), and the session is initiated.
    e = q.expect_many(
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        EventPattern('stream-iq',
            predicate=jp.action_predicate('session-initiate')),
        )[1]

    # Ensure that if Gabble sent the <hold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(hold_event)

    stream.send(make_result_iq(stream, e.stanza))

    # We've acked the s-i, so we do speak Jingle; Gabble should send the
    # <hold/> notification.
    q.expect_many(*hold_event)

    # The call's still on hold, both before and after the streaming
    # implementation says it's okay with that (re-entering PENDING_HOLD seems
    # silly).
    assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
    stream_handler.HoldState(True)
    assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())

    # The peer answers the call; they're still on hold.
    jt.parse_session_initiate(e.query)
    jt.accept()

    q.expect('stream-iq', iq_type='result')

    assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())

    # Now we decide we do actually want to speak to them, and unhold.
    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *unhold_event
        )

    # Hooray! Now let's check that Hold works properly once the call's fully
    # established.

    # ---- Test 1: GetHoldState returns unheld and unhold is a no-op ----

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state
    chan.Hold.RequestHold(False)

    # ---- Test 2: successful hold ----

    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        *hold_event
        )

    call_async(q, stream_handler, 'HoldState', True)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )

    # ---- Test 3: GetHoldState returns held and hold is a no-op ----

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state
    chan.Hold.RequestHold(True)

    # ---- Test 4: successful unhold ----

    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, stream_handler, 'HoldState', False)
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

    call_async(q, stream_handler, 'HoldState', True)
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

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, stream_handler, 'HoldState', False)
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

    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        # Gabble shouldn't send <unhold/> here because s-e might have already
        # relinquished the audio hardware.
        )

    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, stream_handler, 'HoldState', True)
    q.expect('dbus-return', method='HoldState', value=())

    call_async(q, stream_handler, 'HoldState', False)
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
    call_async(q, stream_handler, 'HoldState', True)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )

    # Actually do test 9

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state

    # Check that Gabble doesn't send another <hold/>, or send <unhold/> before
    # we change our minds.
    q.forbid_events(unhold_event + hold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        )

    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        )


    call_async(q, stream_handler, 'HoldState', False)
    call_async(q, stream_handler, 'HoldState', True)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state

    sync_stream(q, stream)
    q.unforbid_events(unhold_event + hold_event)

    # ---- Test 10: attempting to unhold fails ----

    # Check that Gabble doesn't send another <hold/>, or send <unhold/> even
    # though unholding fails.
    q.forbid_events(unhold_event + hold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    call_async(q, stream_handler, 'UnholdFailure')

    q.expect_many(
        EventPattern('dbus-return', method='UnholdFailure', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
        )

    sync_stream(q, stream)
    q.unforbid_events(unhold_event + hold_event)

    # ---- Test 11: when we successfully unhold, the peer gets <unhold/> ---

    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    call_async(q, stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *unhold_event
        )

    # ---- The end ----

    chan.Group.RemoveMembers([self_handle], 'closed')

    # Test completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

if __name__ == '__main__':
    test_all_dialects(test)
