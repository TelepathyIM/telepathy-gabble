"""
Test the Hold API.
"""

import dbus
from gabbletest import make_result_iq, sync_stream
from servicetest import (
    assertEquals, sync_dbus,
    make_channel_proxy, call_async, EventPattern, wrap_channel, assertLength
    )
import constants as cs

from jingletest2 import JingleTest2, test_all_dialects

def test(jp, q, bus, conn, stream):
    remote_jid = 'foo@bar.com/Foo'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    # Advertise that we can do new style calls
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + ".CallHandler", [
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
                cs.CALL_INITIAL_AUDIO: True},
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
                cs.CALL_INITIAL_VIDEO: True},
            ], [
                cs.CHANNEL_TYPE_CALL + '/gtalk-p2p',
                cs.CHANNEL_TYPE_CALL + '/ice-udp',
                cs.CHANNEL_TYPE_CALL + '/video/h264',
            ]),
        ])

    ret = conn.Requests.CreateChannel(
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
          cs.TARGET_HANDLE: remote_handle,
          cs.CALL_INITIAL_AUDIO: True,
          })
    signal = q.expect('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                cs.CHANNEL_TYPE_CONTACT_LIST not in e.args[0][0][1].values())
    assertLength(1, signal.args)
    assertLength(1, signal.args[0])       # one channel
    assertLength(2, signal.args[0][0])    # two struct members
    path = signal.args[0][0][0]

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Call',
        ['Hold'])

    content_paths = chan.Get(cs.CHANNEL_TYPE_CALL, "Contents",
        dbus_interface=dbus.PROPERTIES_IFACE)
    assertLength (1, content_paths)

    content = bus.get_object (conn.bus_name, content_paths[0])

    
    stream_paths = content.Get(cs.CALL_CONTENT, "Streams",
        dbus_interface=dbus.PROPERTIES_IFACE)
    assertLength (1, stream_paths)

    cstream = bus.get_object (conn.bus_name, stream_paths[0])


    recv_state = cstream.GetAll(cs.CALL_STREAM_IFACE_MEDIA,
                                dbus_interface=dbus.PROPERTIES_IFACE)["ReceivingState"]
    send_state = cstream.GetAll(cs.CALL_STREAM_IFACE_MEDIA,
                                dbus_interface=dbus.PROPERTIES_IFACE)["SendingState"]
    assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, recv_state)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, send_state)


    # These are 0- (for old dialects) or 1- (for new dialects) element lists
    # that can be splatted into expect_many with *
    hold_event = jp.rtp_info_event_list("hold")
    unhold_event = jp.rtp_info_event_list("unhold")

    # Before we have accepted any streams, GetHoldState returns Unheld and
    # unhold is a no-op.
    assertEquals((cs.HS_UNHELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
    chan.Hold.RequestHold(False)

    q.forbid_events(hold_event)
    q.forbid_events(unhold_event)

    assertEquals((cs.HS_UNHELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
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

    # Accept call
    chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)

    ret = q.expect ('dbus-signal', signal='CallStateChanged')
    assertEquals(cs.CALL_STATE_INITIALISING, ret.args[0])

    recv_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "ReceivingState",
                             dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, recv_state)
    send_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "SendingState",
                             dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, send_state)


    # Setup codecs
    md = jt.get_call_audio_md_dbus()


    [path, _] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "MediaDescriptionOffer", dbus_interface=dbus.PROPERTIES_IFACE)
    offer = bus.get_object (conn.bus_name, path)
    offer.Accept (md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

    # Add candidates
    candidates = jt.get_call_remote_transports_dbus ()
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)    

    # The session is initiated.
    e = q.expect('stream-iq',
            predicate=jp.action_predicate('session-initiate'))

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
    assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())

    # The peer answers the call; they're still on hold.
    jt.parse_session_initiate(e.query)
    jt.accept()

    q.expect('stream-iq', iq_type='result')

    assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())

    recv_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "ReceivingState",
                             dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, recv_state)
    send_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "SendingState",
                             dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, send_state)


    # Now we decide we do actually want to speak to them, and unhold.
    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)

    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
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
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-return', method='RequestHold', value=()),
        *hold_event
        )

    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
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
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
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
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-return', method='RequestHold', value=()),
        *hold_event
        )

    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
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
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        *unhold_event
        )

    # ---- Test 8: hold, then change our minds before s-e has responded ----

    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_UNHELD, hold_state

    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        *hold_event
        )

    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
         # Gabble shouldn't send <unhold/> here because s-e might have already
        # relinquished the audio hardware.
        )

    sync_stream(q, stream)
    q.unforbid_events(unhold_event)

    try:
        cstream.CompleteReceivingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STOPPED,
            dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    except dbus.DBusException, e:
        assertEquals (cs.INVALID_ARGUMENT, e.get_dbus_name ())

    try:
        cstream.CompleteSendingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STOPPED,
            dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    except dbus.DBusException, e:
        assertEquals (cs.INVALID_ARGUMENT, e.get_dbus_name ())


    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
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
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-return', method='RequestHold', value=()),
        *hold_event
        )

    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
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
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        )

    call_async(q, chan.Hold, 'RequestHold', True)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        )


    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STOPPED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_REQUESTED]),
        )


    hold_state = chan.Hold.GetHoldState()
    assert hold_state[0] == cs.HS_HELD, hold_state

    sync_stream(q, stream)
    q.unforbid_events(unhold_event + hold_event)

    # ---- Test 10: attempting to unhold fails in the sending bit ----

    # Check that Gabble doesn't send another <hold/>, or send <unhold/> even
    # though unholding fails.
    q.forbid_events(unhold_event + hold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA), 
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    cstream.ReportSendingFailure(0, "", "", 
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)

    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                     interface = cs.CALL_STREAM_IFACE_MEDIA), 
        )

    # ---- Test 11: attempting to unhold fails in the sending bit ----

    
    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA), 
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    cstream.ReportReceivingFailure(0, "", "",
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)

    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
        )


    sync_stream(q, stream)
    q.unforbid_events(unhold_event + hold_event)

    # ---- Test 12: when we successfully unhold, the peer gets <unhold/> ---

    q.forbid_events(unhold_event)

    call_async(q, chan.Hold, 'RequestHold', False)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-return', method='RequestHold', value=()),
        )

    # Ensure that if Gabble sent the <unhold/> stanza too early it's already
    # arrived.
    sync_stream(q, stream)
    q.unforbid_events(unhold_event)


    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    q.expect_many(
        EventPattern('dbus-signal', signal='HoldStateChanged',
            args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        *unhold_event
        )


    # ---- The end ----

    chan.Hangup (0, "", "",
            dbus_interface=cs.CHANNEL_TYPE_CALL)

if __name__ == '__main__':
    test_all_dialects(test)
