"""
Test basic outgoing and incoming call handling
"""

import dbus
from dbus.exceptions import DBusException

from twisted.words.xish import xpath

from gabbletest import exec_test, make_presence, sync_stream
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength, assertNotEquals
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects, JingleProtocol031
import ns

from gabbletest import make_muc_presence
from mucutil import *

from callutils import *

muc = "muji@test"

def run_incoming_test(q, bus, conn, stream, bob_leaves_room = False):
    jp = JingleProtocol031 ()
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', muc + "/bob")
    jt.prepare()
    forbidden = [ no_muji_presences (muc) ]

    self_handle = conn.GetSelfHandle()

    _, _, test_handle, bob_handle = \
        join_muc_and_check(q, bus, conn, stream, muc)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {},
        [('content', ns.MUJI, { "name": "Voice" },
            [( 'description', ns.JINGLE_RTP, {"media": "audio"},
            jt.generate_payloads(jt.audio_codecs))])])
    presence.addChild(jp._simple_xml(muji))

    stream.send(presence)

    e = q.expect ('dbus-signal',
        signal='NewChannels',
        predicate=lambda e: \
            e.args[0][0][1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_CALL )

    (path, props) = e.args[0][0]

    assertContains((cs.CHANNEL_TYPE_CALL + '.InitialAudio', True),
        props.items())
    assertContains((cs.CHANNEL_TYPE_CALL + '.InitialVideo', False),
        props.items())

    general_tests (jp, q, bus, conn, stream, path, props)

    channel = bus.get_object (conn.bus_name, path)
    props = channel.GetAll (cs.CHANNEL_TYPE_CALL,
        dbus_interface = dbus.PROPERTIES_IFACE)

    content = bus.get_object (conn.bus_name, props['Contents'][0])

    check_state (q, channel, cs.CALL_STATE_PENDING_RECEIVER)

    codecs = jt.get_call_audio_codecs_dbus()

    check_and_accept_offer (q, bus, conn, self_handle, 0,
            content, codecs)
    channel.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)

    # Preparing stanza
    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    # Codecs stanza
    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    # Gabble shouldn't send new presences for a while
    q.forbid_events(forbidden)

    e = q.expect ('dbus-signal', signal = 'StreamAdded')
    cstream = bus.get_object (conn.bus_name, e.args[0])

    candidates = jt.get_call_remote_transports_dbus ()
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    e = q.expect('stream-iq',
        predicate=jp.action_predicate('session-initiate'))
    jt.parse_session_initiate (e.query)

    jt.accept()

   # Bob adds a Video content
    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    presence.addElement ((ns.MUJI, 'muji')).addElement('preparing')
    stream.send(presence)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {},
        [('content', ns.MUJI, { "name": "Voice" },
            [( 'description', ns.JINGLE_RTP, {"media": "audio"},
            jt.generate_payloads(jt.audio_codecs))]),
         ('content', ns.MUJI, { "name": "Camera" },
            [( 'description', ns.JINGLE_RTP, {"media": "video"},
            jt.generate_payloads(jt.video_codecs))]),
        ])
    presence.addChild(jp._simple_xml(muji))
    stream.send(presence)

    # Gabble noticed bob added a content
    e = q.expect('dbus-signal', signal = 'ContentAdded')
    assertEquals (e.args[1], cs.MEDIA_STREAM_TYPE_VIDEO)

    q.unforbid_events (forbidden)
    content = bus.get_object (conn.bus_name, e.args[0])
    check_and_accept_offer (q, bus, conn, self_handle, 0,
            content, jt.get_call_video_codecs_dbus(),
            check_codecs_changed = False)

    # Gabble sends a presence to prepare
    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    # Gabble sends a presence with the video codecs
    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    # Gabble adds a content to the jingle session and thus a stream is added
    e = q.expect ('dbus-signal', signal = 'StreamAdded')
    cstream = bus.get_object (conn.bus_name, e.args[0])

    candidates = jt.get_call_remote_transports_dbus ()
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    # And now the content-add on the jingle streams
    e = q.expect('stream-iq', to = muc + "/bob",
        predicate = lambda x: \
        xpath.queryForNodes("/iq/jingle[@action='content-add']", x.stanza))

    # Bob leaves the call, bye bob
    if bob_leaves_room:
        presence = make_muc_presence('owner', 'moderator', muc, 'bob')
        presence['type'] = 'unavailable'
    else:
        presence = make_muc_presence('owner', 'moderator', muc, 'bob')

    stream.send(presence)
    (cmembers, _, _) = q.expect_many(
        EventPattern ('dbus-signal', signal = 'CallMembersChanged'),
        # Audio and video stream
        EventPattern ('dbus-signal', signal = 'StreamRemoved'),
        EventPattern ('dbus-signal', signal = 'StreamRemoved'))


    # Just bob left
    assertLength (1, cmembers.args[1])

def run_outgoing_test(q, bus, conn, stream, close_channel=False):
    jp = JingleProtocol031 ()
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', muc + '/bob')
    jt.prepare()

    self_handle = conn.GetSelfHandle()

    # Not allowed to have muji related presences before we accept the channel
    forbidden = [ no_muji_presences (muc) ]

    (path, props) = create_muji_channel (q, conn, stream, muc)

    q.forbid_events(forbidden)
    general_tests (jp, q, bus, conn, stream, path, props)

    channel = bus.get_object (conn.bus_name, path)

    props = channel.GetAll (cs.CHANNEL_TYPE_CALL,
        dbus_interface = dbus.PROPERTIES_IFACE)

    content = bus.get_object (conn.bus_name, props['Contents'][0])
    codecs = jt.get_call_audio_codecs_dbus()
    content.SetCodecs(codecs)

    # Accept the channel, which means we can get muji presences
    q.unforbid_events (forbidden)
    channel.Accept()

    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')
    mujinode = xpath.queryForNodes("/presence/muji", e.stanza)
    assertLength (1, mujinode)

    # The one with the codecs
    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    # Gabble shouldn't send new presences for a while
    q.forbid_events(forbidden)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    presence.addElement ((ns.MUJI, 'muji')).addElement('preparing')
    stream.send(presence)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {},
        [('content', ns.MUJI, { "name": "Audio" },
            [( 'description', ns.JINGLE_RTP, {"media": "audio"},
            jt.generate_payloads(jt.audio_codecs))])])
    presence.addChild(jp._simple_xml(muji))
    stream.send(presence)

    q.expect('dbus-signal', signal = 'CallStateChanged')

    # Bob appears and starts a session right afterwards
    q.expect('dbus-signal', signal = 'CallMembersChanged')

    jt.incoming_call(audio = "Audio")

    e = q.expect ('dbus-signal', signal = 'StreamAdded')
    cstream = bus.get_object (conn.bus_name, e.args[0])

    candidates = jt.get_call_remote_transports_dbus ()
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    # Fake our endpoint being connected
    endpoints = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
        "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
    assertLength (1, endpoints)

    endpoint = bus.get_object (conn.bus_name, endpoints[0])

    endpoint.SetStreamState (cs.MEDIA_STREAM_STATE_CONNECTED,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)

    e = q.expect ('stream-iq',
        predicate = jp.action_predicate ('session-accept'))
    stream.send(jp.xml(jp.ResultIq(jt.peer, e.stanza, [])))

    # But we want video as well !
    c = channel.AddContent ("Camera!", cs.MEDIA_STREAM_TYPE_VIDEO,
        dbus_interface=cs.CHANNEL_TYPE_CALL)

    e = q.expect('dbus-signal', signal = 'ContentAdded')
    assertEquals (e.args[1], cs.MEDIA_STREAM_TYPE_VIDEO)

    content = bus.get_object (conn.bus_name, c)
    codecs = jt.get_call_video_codecs_dbus()
    content.SetCodecs(codecs)

    q.unforbid_events(forbidden)

    # preparing
    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    #codecs
    e = q.expect('stream-presence', to = muc + "/test")
    echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    # Bob would like to join our video party
    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {},
        [('content', ns.MUJI, { "name": "Audio" },
            [( 'description', ns.JINGLE_RTP, {"media": "audio"},
            jt.generate_payloads(jt.audio_codecs))]),
         ('content', ns.MUJI, { "name": "Camera!" },
            [( 'description', ns.JINGLE_RTP, {"media": "video"},
            jt.generate_payloads(jt.video_codecs))]),
        ])
    presence.addChild(jp._simple_xml(muji))
    stream.send(presence)

    # new codec offer as bob threw in some codecs
    q.expect('dbus-signal', signal='NewCodecOffer')
    check_and_accept_offer (q, bus, conn, self_handle, 0,
            content, codecs, check_codecs_changed = False)

    # Bob sends a content
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.peer, 'content-add', [
            jp.Content('videostream', 'initiator', 'both',
                jp.Description('video', [
                    jp.PayloadType(name, str(rate), str(id), parameters) for
                        (name, id, rate, parameters) in jt.video_codecs ]),
            jp.TransportGoogleP2P()) ]) ])
    stream.send(jp.xml(node))

    # We get a new stream
    q.expect('dbus-signal', signal = 'StreamAdded')

    # Sync up the stream to ensure we sent out all the xmpp traffic that was
    # the result of a stream being added
    sync_stream (q, stream)

    # happiness.. Now let's hang up
    if close_channel:
        channel.Close()
        hangup_event = EventPattern ('dbus-signal', signal = "Closed",
            path = path)
    else:
        channel.Hangup (0, "", "", dbus_interface=cs.CHANNEL_TYPE_CALL)
        hangup_event = EventPattern ('dbus-signal', signal='CallStateChanged')

    # Should change the call state to ended, send a session-terminate to our
    # only peer and send a muc presence without any mention of muji
    q.forbid_events(forbidden)
    q.expect_many (EventPattern ('stream-presence', to = muc + "/test"),
        EventPattern ('stream-iq',
                predicate=jp.action_predicate ('session-terminate')),
        hangup_event)

    if not close_channel:
        channel.Close()
        q.expect ('dbus-signal', signal="Closed", path = path)

    try:
        channel.Close()
        raise AssertionError ("Channel didn't actually close")
    except DBusException:
        pass


def general_tests (jp, q, bus, conn, stream, path, props):
    assertEquals (cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])

    channel = bus.get_object (conn.bus_name, path)
    chan_props = channel.GetAll (cs.CHANNEL_TYPE_CALL,
        dbus_interface = dbus.PROPERTIES_IFACE)

    contents = chan_props['Contents']
    assertLength(1, contents)

if __name__ == '__main__':
    exec_test (run_outgoing_test)
    exec_test (lambda q,b, c, s: run_outgoing_test (q, b, c, s, True))
    exec_test (run_incoming_test)
    exec_test (lambda q,b, c, s: run_incoming_test (q, b, c, s, True))
