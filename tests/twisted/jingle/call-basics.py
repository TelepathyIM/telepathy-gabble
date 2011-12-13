"""
Test basic outgoing and incoming call handling
"""

import dbus
from dbus.exceptions import DBusException

from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertDoesNotContain, assertContains, assertLength, assertNotEquals,
    DictionarySupersetOf)
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects
import ns
from config import CHANNEL_TYPE_CALL_ENABLED

if not CHANNEL_TYPE_CALL_ENABLED:
    print "NOTE: built with --disable-channel-type-call"
    raise SystemExit(77)

def check_state (q, chan, state, wait = False):
    if wait:
        q.expect('dbus-signal', signal='CallStateChanged',
            interface = cs.CHANNEL_TYPE_CALL,
            predicate = lambda e: e.args[0] == state)

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (state, properties["CallState"])

def check_and_accept_offer (q, bus, conn,
        content, md, remote_handle, offer_path = None,
        md_changed = True):

    [path, handle, remote_md ] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "MediaDescriptionOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    if offer_path != None:
        assertEquals (offer_path, path)

    assertNotEquals ("/", path)

    offer = bus.get_object (conn.bus_name, path)
    codecmap_property = offer.Get (cs.CALL_CONTENT_MEDIADESCRIPTION,
        "Codecs", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (remote_md[cs.CALL_CONTENT_MEDIADESCRIPTION + '.Codecs'], codecmap_property)

    offer.Accept (md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

    current_md = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "LocalMediaDescriptions", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (md,  current_md[remote_handle])

    if md_changed:
        o = q.expect ('dbus-signal', signal='LocalMediaDescriptionChanged')
        assertEquals ([remote_handle, md], o.args)

def test_content_addition (jt2, jp, q, bus, conn, chan, remote_handle):
    path = chan.AddContent ("Webcam", cs.CALL_MEDIA_TYPE_VIDEO,
        dbus_interface=cs.CHANNEL_TYPE_CALL)
    content = bus.get_object (conn.bus_name, path)
    content_properties = content.GetAll (cs.CALL_CONTENT,
        dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (cs.CALL_DISPOSITION_NONE,
        content_properties["Disposition"])
    #assertEquals (self_handle, content_properties["Creator"])
    assertContains ("Webcam", content_properties["Name"])

    md = jt2.get_call_video_md_dbus()
    check_and_accept_offer (q, bus, conn, content, md, remote_handle)

    cstream = bus.get_object (conn.bus_name, content_properties["Streams"][0])
    candidates = jt2.get_call_remote_transports_dbus ()
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    q.expect('stream-iq', predicate=jp.action_predicate('content-add'))

    content.Remove(dbus_interface=cs.CALL_CONTENT)
    q.expect('stream-iq', predicate=jp.action_predicate('content-remove'))

def run_test(jp, q, bus, conn, stream, incoming):
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

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

    # Ensure a channel that doesn't exist yet.
    if incoming:
        jt2.incoming_call()
    else:
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
    emitted_props = signal.args[0][0][1]

    assertEquals(
        cs.CHANNEL_TYPE_CALL, emitted_props[cs.CHANNEL_TYPE])

    assertEquals(remote_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals('foo@bar.com', emitted_props[cs.TARGET_ID])

    assertEquals(not incoming, emitted_props[cs.REQUESTED])
    if incoming:
        assertEquals(remote_handle, emitted_props[cs.INITIATOR_HANDLE])
        assertEquals('foo@bar.com', emitted_props[cs.INITIATOR_ID])
    else:
        assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
        assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    assertEquals(True, emitted_props[cs.CALL_INITIAL_AUDIO])
    assertEquals(False, emitted_props[cs.CALL_INITIAL_VIDEO])

    chan = bus.get_object (conn.bus_name, signal.args[0][0][0])

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
        dbus_interface=dbus.PROPERTIES_IFACE)

    # Check if all the properties are there
    assertEquals (sorted([ "Contents", "CallMembers",
        "CallState", "CallFlags", "CallStateReason", "CallStateDetails",
        "HardwareStreaming", "InitialAudio", "InitialAudioName",
        "InitialVideo", "InitialVideoName", "MutableContents",
        "InitialTransport", "MemberIdentifiers" ]),
        sorted(properties.keys()))

    # Remote member is the target
    assertEquals ([remote_handle], properties["CallMembers"].keys())
    assertEquals (0, properties["CallMembers"][remote_handle])

    # No Hardware Streaming for you
    assertEquals (False, properties["HardwareStreaming"])

    # Only an audio content
    assertLength (1, properties["Contents"])

    content = bus.get_object (conn.bus_name, properties["Contents"][0])

    content_properties = content.GetAll (cs.CALL_CONTENT,
        dbus_interface=dbus.PROPERTIES_IFACE)

    # Has one stream
    assertLength (1, content_properties["Streams"])
    assertEquals (cs.CALL_DISPOSITION_INITIAL,
        content_properties["Disposition"])

    # Implements Content.Interface.Media
    assertEquals([cs.CALL_CONTENT_IFACE_MEDIA],
        content_properties["Interfaces"])

    #if incoming:
    #    assertEquals (remote_handle, content_properties["Creator"])
    #else:
    #    assertEquals (self_handle, content_properties["Creator"])

    assertContains ("Name", content_properties.keys())

    cstream = bus.get_object (conn.bus_name, content_properties["Streams"][0])

    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)

    assertDoesNotContain (self_handle, stream_props["RemoteMembers"].keys())
    assertContains (remote_handle, stream_props["RemoteMembers"].keys())
    assertEquals([cs.CALL_STREAM_IFACE_MEDIA], stream_props["Interfaces"])

    if incoming:
        assertEquals (cs.CALL_SENDING_STATE_PENDING_SEND,
            stream_props["LocalSendingState"])
        assertEquals (cs.CALL_SENDING_STATE_SENDING,
            stream_props["RemoteMembers"][remote_handle])
    else:
        assertEquals (cs.CALL_SENDING_STATE_PENDING_SEND,
            stream_props["RemoteMembers"][remote_handle])
        assertEquals (cs.CALL_SENDING_STATE_PENDING_SEND,
            stream_props["LocalSendingState"])


    # Media type should audio
    assertEquals (cs.CALL_MEDIA_TYPE_AUDIO, content_properties["Type"])

    # Packetization should be RTP
    content_media_properties = content.GetAll (cs.CALL_CONTENT_IFACE_MEDIA,
        dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_CONTENT_PACKETIZATION_RTP,
        content_media_properties["Packetization"])

    # Check if the channel is in the right pending state
    if not incoming:
        check_state (q, chan, cs.CALL_STATE_PENDING_INITIATOR)
        chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)

    check_state (q, chan, cs.CALL_STATE_INITIALISING,
        wait = not incoming)

    # Setup media description
    md = jt2.get_call_audio_md_dbus()

    # make sure this fails with NotAvailable
    try:
        content.UpdateLocalMediaDescription(remote_handle, md, dbus_interface=cs.CALL_CONTENT_IFACE_MEDIA)
    except DBusException, e:
        if e.get_dbus_name() != cs.NOT_AVAILABLE:
            raise e
    else:
        assert false

    # We should have a md offer
    check_and_accept_offer (q, bus, conn, content, md, remote_handle)

    current_md = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "LocalMediaDescriptions", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (md,  current_md[remote_handle])


    cstream.SetCredentials(jt2.ufrag, jt2.pwd,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    q.expect('dbus-signal', signal='LocalCredentialsChanged',
             args=[jt2.ufrag, jt2.pwd])

    credentials = cstream.GetAll(cs.CALL_STREAM_IFACE_MEDIA,
        dbus_interface=dbus.PROPERTIES_IFACE)["LocalCredentials"]
    assertEquals ((jt2.ufrag, jt2.pwd), credentials)

    # Add candidates
    candidates = jt2.get_call_remote_transports_dbus ()
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    expected = [ EventPattern('dbus-signal', signal='LocalCandidatesAdded') ]

    if not incoming:
        expected.append (EventPattern('stream-iq',
            predicate=jp.action_predicate('session-initiate')))

    ret = q.expect_many (*expected)
    assertEquals (candidates, ret[0].args[0])

    if not incoming:
        jt2.parse_session_initiate(ret[1].query)

    cstream.FinishInitialCandidates (dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    local_candidates = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
        "LocalCandidates", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (candidates,  local_candidates)

    endpoints = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
        "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
    assertLength (1, endpoints)

    # There doesn't seem to be a good way to get the transport type from the
    # JP used, for now assume we prefer gtalk p2p and always pick that..
    transport = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                "Transport", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_TRANSPORT_GOOGLE, transport)

    endpoint = bus.get_object (conn.bus_name, endpoints[0])

    endpoint_props = endpoint.GetAll(cs.CALL_STREAM_ENDPOINT,
                 dbus_interface=dbus.PROPERTIES_IFACE)
    transport = endpoint_props["Transport"]
    assertEquals (cs.CALL_STREAM_TRANSPORT_GOOGLE, transport)

    candidates = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "RemoteCandidates",  dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals ([], candidates)

    selected_candidate = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "SelectedCandidatePairs",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals ([], selected_candidate)

    state = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "EndpointState",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals ({}, state)

    if jp.dialect == 'gtalk-v0.3':
        # Candidates must be sent one at a time.
        for candidate in jt2.get_call_remote_transports_dbus():
            component, addr, port, props = candidate
            jt2.send_remote_candidates_call_xmpp(jt2.audio_names[0],
                    "initiator", [candidate])
            q.expect('dbus-signal',
                    signal='RemoteCandidatesAdded',
                    interface=cs.CALL_STREAM_ENDPOINT,
                    args=[[(component, addr, port,
                               DictionarySupersetOf(props))]])
    elif jp.dialect == 'gtalk-v0.4' and not incoming:
        # Don't test this case at all.
        pass
    else:
        jt2.send_remote_candidates_call_xmpp(jt2.audio_names[0], "initiator")

        candidates = []
        for component, addr, port, props in \
                jt2.get_call_remote_transports_dbus():
            candidates.append((component, addr, port,
                               DictionarySupersetOf(props)))

        q.expect ('dbus-signal',
                signal='RemoteCandidatesAdded',
                interface=cs.CALL_STREAM_ENDPOINT,
                args=[candidates])

    # FIXME: makes sense to have same local and remove candidate?
    candidate1 = jt2.get_call_remote_transports_dbus()[0]

    # Expected to fail since we did not said we are controlling side
    try:
        endpoint.SetSelectedCandidatePair (candidate1, candidate1,
            dbus_interface=cs.CALL_STREAM_ENDPOINT)
    except DBusException, e:
        if e.get_dbus_name() != cs.INVALID_ARGUMENT:
            raise e
    else:
        assert false

    endpoint.SetControlling(True, dbus_interface=cs.CALL_STREAM_ENDPOINT)
    endpoint.SetSelectedCandidatePair (candidate1, candidate1,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)

    pair = q.expect ('dbus-signal',
        signal='CandidatePairSelected', interface=cs.CALL_STREAM_ENDPOINT)
    assertEquals (candidate1, pair.args[0])
    assertEquals (candidate1, pair.args[1])

    candidate2 = jt2.get_call_remote_transports_dbus()[1]
    endpoint.SetSelectedCandidatePair (candidate2, candidate2,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)

    # We have an RTCP candidate as well, so we should set this as selected
    # too.
    pair = q.expect ('dbus-signal',
        signal='CandidatePairSelected', interface=cs.CALL_STREAM_ENDPOINT)
    assertEquals (candidate2, pair.args[0])
    assertEquals (candidate2, pair.args[1])

    pairs = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "SelectedCandidatePairs",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (len(pairs), 2)
    assertEquals (pairs[0][0], pairs[0][1])
    assertEquals (pairs[1][0], pairs[1][1])
    if pairs[0][0] == candidate1:
        assertEquals (pairs[1][0], candidate2)
    else:
        assertEquals (pairs[0][0], candidate2)
        assertEquals (pairs[1][0], candidate1)

    # setting endpoints to CONNECTED should make the call state move from
    # INITIALISING to INITIALISED

    endpoint.SetEndpointState (1, cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)
    q.expect('dbus-signal', signal='EndpointStateChanged',
        interface=cs.CALL_STREAM_ENDPOINT)

    endpoint.SetEndpointState (2, cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)
    q.expect('dbus-signal', signal='EndpointStateChanged',
        interface=cs.CALL_STREAM_ENDPOINT)

    state = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "EndpointState",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED, state[1])
    assertEquals (cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED, state[2])

    check_state (q, chan, cs.CALL_STATE_INITIALISED)

    if incoming:
        # Act as if we're ringing
        chan.SetRinging (dbus_interface=cs.CHANNEL_TYPE_CALL)
        signal = q.expect('dbus-signal', signal='CallStateChanged')
        assertEquals(cs.CALL_FLAG_LOCALLY_RINGING,
            signal.args[1] & cs.CALL_FLAG_LOCALLY_RINGING)

        # And now pickup the call
        chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)
        ret = q.expect_many (
            EventPattern('dbus-signal', signal='CallStateChanged'),
            EventPattern('stream-iq',
                predicate=jp.action_predicate('session-accept')),
            )
        assertEquals(0, ret[0].args[1] & cs.CALL_FLAG_LOCALLY_RINGING)
    else:
        if jp.is_modern_jingle():
            # The other person's client starts ringing, and tells us so!
            node = jp.SetIq(jt2.peer, jt2.jid, [
                jp.Jingle(jt2.sid, jt2.jid, 'session-info', [
                    ('ringing', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
            stream.send(jp.xml(node))

            o = q.expect ('dbus-signal', signal="CallMembersChanged")
            assertEquals(cs.CALL_MEMBER_FLAG_RINGING, o.args[0][remote_handle])

        jt2.accept()

        o = q.expect ('dbus-signal', signal='NewMediaDescriptionOffer')

        [path, _, _ ] = o.args
        md = jt2.get_call_audio_md_dbus()

        check_and_accept_offer (q, bus, conn, content, md, remote_handle, path,
            md_changed = False )

    check_state (q, chan, cs.CALL_STATE_ACCEPTED)

    o = q.expect ('dbus-signal', signal='LocalSendingStateChanged',
        interface = cs.CALL_STREAM)
    assertEquals(cs.CALL_SENDING_STATE_SENDING, o.args[0])

    # All Direction should be both now
    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)
    assertEquals ({remote_handle: cs.CALL_SENDING_STATE_SENDING},
        stream_props["RemoteMembers"])
    assertEquals (cs.CALL_SENDING_STATE_SENDING, stream_props["LocalSendingState"])

    # Turn sending off and on again
    cstream.SetSending (False,
        dbus_interface = cs.CALL_STREAM)

    o = q.expect ('dbus-signal', signal='LocalSendingStateChanged',
        interface = cs.CALL_STREAM)
    assertEquals(cs.CALL_SENDING_STATE_NONE, o.args[0])

    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)
    assertEquals({remote_handle: cs.CALL_SENDING_STATE_SENDING},
                 stream_props["RemoteMembers"])
    assertEquals(cs.CALL_SENDING_STATE_NONE, stream_props["LocalSendingState"])

    cstream.SetSending (True,
        dbus_interface = cs.CALL_STREAM)

    o = q.expect ('dbus-signal', signal='LocalSendingStateChanged',
        interface = cs.CALL_STREAM)
    assertEquals(cs.CALL_SENDING_STATE_SENDING, o.args[0])

    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)
    assertEquals ({remote_handle: cs.CALL_SENDING_STATE_SENDING},
                  stream_props["RemoteMembers"])
    assertEquals (cs.CALL_SENDING_STATE_SENDING, stream_props["LocalSendingState"])

    try:
        test_content_addition (jt2, jp, q, bus, conn, chan, remote_handle)
    except DBusException, e:
        assertEquals (cs.NOT_AVAILABLE, e.get_dbus_name ())
        assert not jp.can_do_video()

    if incoming:
        jt2.terminate()
    else:
        chan.Hangup (0, "", "",
            dbus_interface=cs.CHANNEL_TYPE_CALL)

    check_state (q, chan, cs.CALL_STATE_ENDED, wait = True)

if __name__ == '__main__':
    test_all_dialects(lambda jp, q, bus, conn, stream:
        run_test(jp, q, bus, conn, stream, False))
    test_all_dialects(lambda jp, q, bus, conn, stream:
        run_test(jp, q, bus, conn, stream, True))
