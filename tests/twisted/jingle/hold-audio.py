"""
Test the Hold API.
"""

from gabbletest import make_result_iq, sync_stream
from servicetest import make_channel_proxy, call_async, EventPattern
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

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')
    hold_iface = make_channel_proxy(conn, path, 'Channel.Interface.Hold')

    media_iface.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=lambda e:
        jp.match_jingle_action(e.query, 'session-initiate'))
    stream.send(make_result_iq(stream, e.stanza))

    jt.parse_session_initiate(e.query)
    jt.accept()

    q.expect('stream-iq', iq_type='result')

    # These are 0- (for old dialects) or 1- (for new dialects) element lists
    # that can be splatted into expect_many with *
    hold_event = jp.hold_notification_event_list(True)
    active_event = jp.hold_notification_event_list(False)
    # ---- Test 1: GetHoldState returns unheld and unhold is a no-op ----

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state
    hold_iface.RequestHold(False)

    # ---- Test 2: successful hold ----

    call_async(q, hold_iface, 'RequestHold', True)
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

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state
    hold_iface.RequestHold(True)

    # ---- Test 4: successful unhold ----

    q.forbid_events(hold_event)

    call_async(q, hold_iface, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <active/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(hold_event)

    call_async(q, stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *active_event
        )

    # ---- Test 5: GetHoldState returns False and unhold is a no-op ----

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state
    hold_iface.RequestHold(False)

    # ---- Test 6: 3 parallel calls to hold ----

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state

    call_async(q, hold_iface, 'RequestHold', True)
    call_async(q, hold_iface, 'RequestHold', True)
    call_async(q, hold_iface, 'RequestHold', True)
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

    q.forbid_events(active_event)

    call_async(q, hold_iface, 'RequestHold', False)
    call_async(q, hold_iface, 'RequestHold', False)
    call_async(q, hold_iface, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <active/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(active_event)

    call_async(q, stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *active_event
        )

    # ---- Test 8: hold, then change our minds before s-e has responded ----

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state

    call_async(q, hold_iface, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[True]),
        *hold_event
        )

    q.forbid_events(active_event)

    call_async(q, hold_iface, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        # Gabble shouldn't send <active/> here because s-e might have already
        # relinquished the audio hardware.
        )

    sync_stream(q, stream)
    q.unforbid_events(active_event)

    call_async(q, stream_handler, 'HoldState', True)
    q.expect('dbus-return', method='HoldState', value=())

    call_async(q, stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *active_event
        )

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state

    # ---- Test 9: unhold, then change our minds before s-e has responded ----

    # Go to state "held" first
    call_async(q, hold_iface, 'RequestHold', True)
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

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state

    # Check that Gabble doesn't send another <hold/>, or send <active/> before
    # we change our minds.
    q.forbid_events(active_event + hold_event)

    call_async(q, hold_iface, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        )

    call_async(q, hold_iface, 'RequestHold', True)
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

    hold_state = hold_iface.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state

    sync_stream(q, stream)
    q.unforbid_events(active_event + hold_event)

    # ---- Test 10: attempting to unhold fails ----

    # Check that Gabble doesn't send another <hold/>, or send <active/> even
    # though unholding fails.
    q.forbid_events(active_event + hold_event)

    call_async(q, hold_iface, 'RequestHold', False)
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
    q.unforbid_events(active_event + hold_event)

    # ---- Test 11: when we successfully unhold, the peer gets <active/> ---

    q.forbid_events(active_event)

    call_async(q, hold_iface, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SetStreamHeld', args=[False]),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <active/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(active_event)

    call_async(q, stream_handler, 'HoldState', False)
    q.expect_many(
        EventPattern('dbus-return', method='HoldState', value=()),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        *active_event
        )

    # ---- The end ----

    group_iface.RemoveMembers([self_handle], 'closed')

    # Test completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    test_all_dialects(test)

