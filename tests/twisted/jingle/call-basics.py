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
    assertEquals, assertContains, assertLength, assertNotEquals
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects
import ns

def check_state (q, chan, state, wait = False):
    if wait:
        q.expect('dbus-signal', signal='CallStateChanged',
            interface = cs.CHANNEL_TYPE_CALL,
            predicate = lambda e: e.args[0] == state)

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (state,
        properties["CallState"])

def check_and_accept_offer (q, bus, conn, self_handle, remote_handle,
        content, codecs, offer_path = None):

    [path, codecmap] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    if offer_path != None:
        assertEquals (offer_path, path)

    assertNotEquals ("/", path)

    offer = bus.get_object (conn.bus_name, path)
    codecmap_property = offer.Get (cs.CALL_CONTENT_CODECOFFER,
        "RemoteContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecmap, codecmap_property)

    offer.Accept (codecs, dbus_interface=cs.CALL_CONTENT_CODECOFFER)

    current_codecs = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "ContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecs,  current_codecs[self_handle])

    o = q.expect ('dbus-signal', signal='CodecsChanged')

    assertEquals ([{ self_handle: codecs, remote_handle: codecs}, []],
        o.args)

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
        "HardwareStreaming", "InitialAudio", "InitialVideo",
        "MutableContents" ]), sorted(properties.keys()))

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

    if incoming:
        assertEquals (remote_handle, content_properties["Creator"])
    else:
        assertEquals (self_handle, content_properties["Creator"])

    assertContains ("Name", content_properties.keys())

    cstream = bus.get_object (conn.bus_name, content_properties["Streams"][0])

    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)

    assertContains (self_handle, stream_props["Senders"].keys())
    assertContains (remote_handle, stream_props["Senders"].keys())

    if incoming:
        assertEquals (cs.CALL_SENDING_STATE_PENDING_SEND,
            stream_props["Senders"][self_handle])
        assertEquals (cs.CALL_SENDING_STATE_SENDING,
            stream_props["Senders"][remote_handle])
    else:
        assertEquals (cs.CALL_SENDING_STATE_PENDING_SEND,
            stream_props["Senders"][remote_handle])
        assertEquals (cs.CALL_SENDING_STATE_SENDING,
            stream_props["Senders"][self_handle])


    # Media type should audio
    assertEquals (cs.CALL_MEDIA_TYPE_AUDIO, content_properties["Type"])

    # Check if the channel is in the right pending state
    if not incoming:
        check_state (q, chan, cs.CALL_STATE_PENDING_INITIATOR)
        chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)

    check_state (q, chan, cs.CALL_STATE_PENDING_RECEIVER,
        wait = not incoming)

    # Setup codecs
    codecs = jt2.get_call_audio_codecs_dbus()
    if incoming:
        # Act as if we're ringing
        chan.Ringing (dbus_interface=cs.CHANNEL_TYPE_CALL)
        # We should have a codec offer
        check_and_accept_offer (q, bus, conn, self_handle, remote_handle,
            content, codecs)
    else:
        content.SetCodecs(codecs, dbus_interface=cs.CALL_CONTENT_IFACE_MEDIA)

    current_codecs = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "ContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (codecs,  current_codecs[self_handle])

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

    cstream.CandidatesPrepared (dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

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

    transport = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                "Transport", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_TRANSPORT_GOOGLE, transport)

    candidates = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "RemoteCandidates",  dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals ([], candidates)

    selected_candidate = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "SelectedCandidate",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals ((0, '', 0, {}), selected_candidate)

    state = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "StreamState",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.MEDIA_STREAM_STATE_DISCONNECTED, state)

    if incoming or jp.dialect != 'gtalk-v0.4':
        jt2.remote_candidates(jt2.audio_names[0], "initiator")

        remote_candidates = q.expect ('dbus-signal',
            signal='RemoteCandidatesAdded', interface=cs.CALL_STREAM_ENDPOINT)
        assertEquals (jt2.get_call_remote_transports_dbus(),
            remote_candidates.args[0])

    endpoint.SetSelectedCandidate (jt2.get_call_remote_transports_dbus()[0],
        dbus_interface=cs.CALL_STREAM_ENDPOINT)

    selected_candidate = q.expect ('dbus-signal',
        signal='CandidateSelected', interface=cs.CALL_STREAM_ENDPOINT)
    assertEquals (jt2.get_call_remote_transports_dbus(),
        selected_candidate.args)

    selected_candidate = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "SelectedCandidate",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (jt2.get_call_remote_transports_dbus()[0], selected_candidate)

    endpoint.SetStreamState (cs.MEDIA_STREAM_STATE_CONNECTED,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)

    q.expect('dbus-signal', signal='StreamStateChanged',
        interface=cs.CALL_STREAM_ENDPOINT)

    state = endpoint.Get (cs.CALL_STREAM_ENDPOINT,
        "StreamState",  dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.MEDIA_STREAM_STATE_CONNECTED, state)

    if incoming:
        chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)
        q.expect('stream-iq', predicate=jp.action_predicate('session-accept'))
    else:
        if jp.is_modern_jingle():
            # The other person's client starts ringing, and tells us so!
            node = jp.SetIq(jt2.peer, jt2.jid, [
                jp.Jingle(jt2.sid, jt2.jid, 'session-info', [
                    ('ringing', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
            stream.send(jp.xml(node))

            q.expect ('dbus-signal', signal="CallMembersChanged",
                args = [{ remote_handle: cs.CALL_MEMBER_FLAG_RINGING }, []])

        jt2.accept()

        o = q.expect ('dbus-signal', signal='NewCodecOffer')

        [path, _ ] = o.args
        codecs = jt2.get_call_audio_codecs_dbus()

        check_and_accept_offer (q, bus, conn, self_handle, remote_handle,
            content, codecs, path)

    check_state (q, chan, cs.CALL_STATE_ACCEPTED)

    # All Direction should be both now
    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)
    assertEquals (
        {
            self_handle : cs.CALL_SENDING_STATE_SENDING,
            remote_handle: cs.CALL_SENDING_STATE_SENDING
        },
        stream_props["Senders"])

    # Turn sending off and on again
    cstream.SetSending (False,
        dbus_interface = cs.CALL_STREAM)

    q.expect ('dbus-signal', signal='SendersChanged',
        interface = cs.CALL_STREAM,
        args = [{ self_handle: cs.CALL_SENDING_STATE_NONE }, []])

    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)
    assertEquals ({ self_handle : cs.CALL_SENDING_STATE_NONE,
                    remote_handle: cs.CALL_SENDING_STATE_SENDING },
                  stream_props["Senders"])

    cstream.SetSending (True,
        dbus_interface = cs.CALL_STREAM)

    q.expect ('dbus-signal', signal='SendersChanged',
        interface = cs.CALL_STREAM,
        args = [{ self_handle: cs.CALL_SENDING_STATE_SENDING }, []])

    stream_props = cstream.GetAll (cs.CALL_STREAM,
        dbus_interface = dbus.PROPERTIES_IFACE)
    assertEquals ({ self_handle : cs.CALL_SENDING_STATE_SENDING,
                    remote_handle: cs.CALL_SENDING_STATE_SENDING },
                  stream_props["Senders"])

    try:
        path = chan.AddContent ("Webcam", cs.CALL_MEDIA_TYPE_VIDEO,
            dbus_interface=cs.CHANNEL_TYPE_CALL)
        content = bus.get_object (conn.bus_name, path)
        content_properties = content.GetAll (cs.CALL_CONTENT,
            dbus_interface=dbus.PROPERTIES_IFACE)

        assertEquals (cs.CALL_DISPOSITION_NONE,
            content_properties["Disposition"])
        assertEquals (self_handle, content_properties["Creator"])
        assertContains ("Webcam", content_properties["Name"])
    except DBusException, e:
        assert not jp.can_do_video()
        assertEquals (cs.NOT_AVAILABLE, e.get_dbus_name ())

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
