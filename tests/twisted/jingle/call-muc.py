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
from jingletest2 import JingleTest2, test_all_dialects, JingleProtocol031
import ns

from gabbletest import make_muc_presence
from mucutil import join_muc_and_check

from callutils import *

muc = "muji@test"

def run_incoming_test(q, bus, conn, stream):
    jp = JingleProtocol031 ()
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', muc + "/bob")
    jt.prepare()
    forbidden = [ no_muji_presences () ]

    self_handle = conn.GetSelfHandle()

    _, _, test_handle, bob_handle = \
        join_muc_and_check(q, bus, conn, stream, muc)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {},
        [('content', ns.MUJI, { "name": "Voice" },
            [( 'description', None, {"media": "audio"},
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
    echo_presence (q, stream, e.stanza, 'none', 'participant')

    # Codecs stanza
    e = q.expect('stream-presence', to = muc + "/test")
    echo_presence (q, stream, e.stanza, 'none', 'participant')

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
    muji =  ('muji', ns.MUJI, {}, [('preparing' )])
    stream.send(presence)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {},
        [('content', ns.MUJI, { "name": "Voice" },
            [( 'description', None, {"media": "audio"},
            jt.generate_payloads(jt.audio_codecs))]),
         ('content', ns.MUJI, { "name": "Camera" },
            [( 'description', None, {"media": "video"},
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
            content, jt.get_call_video_codecs_dbus())

    # Gabble sends a presence to prepare
    e = q.expect('stream-presence', to = muc + "/test")
    echo_presence (q, stream, e.stanza, 'none', 'participant')

    # Gabble sends a presence with the video codecs
    e = q.expect('stream-presence', to = muc + "/test")
    echo_presence (q, stream, e.stanza, 'none', 'participant')

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

    # success!


def echo_presence (q, stream, stanza, affiliation, role):
    x = stanza.addElement((ns.MUC_USER, 'x'))
    stanza['from'] = stanza['to']
    del stanza['to']

    item = x.addElement('item')
    item['affiliation'] = affiliation
    item['role'] = role

    stream.send (stanza)

def no_muji_presences ():
    return EventPattern ('stream-presence',
        to = muc + "/test",
        predicate = lambda x:
            xpath.queryForNodes("/presence/muji", x.stanza))

def run_outgoing_test(q, bus, conn, stream):
    jp = JingleProtocol031 ()
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', muc + '/bob')
    jt.prepare()

    # Not allowed to have muji releated presences before we accept the channel
    forbidden = [ no_muji_presences () ]

    q.forbid_events(forbidden)
    call_async (q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc,
          cs.CALL_INITIAL_AUDIO: True,
         }, byte_arrays = True)

    e = q.expect('stream-presence', to = muc + "/test")
    echo_presence (q, stream, e.stanza, 'none', 'participant')

    e = q.expect ('dbus-return', method='CreateChannel')

    (path, props) = e.value

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

    presence = q.expect('stream-presence', to = muc + "/test")
    mujinode = xpath.queryForNodes("/presence/muji", presence.stanza)
    assertLength (1, mujinode)

    # Gabble shouldn't send new presences for a while
    q.forbid_events(forbidden)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {}, [('preparing' )])
    stream.send(presence)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji =  ('muji', ns.MUJI, {},
        [('content', ns.MUJI, { "name": "Audio" },
            [( 'description', None, {"media": "audio"},
            jt.generate_payloads(jt.audio_codecs))])])
    presence.addChild(jp._simple_xml(muji))
    stream.send(presence)

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

    q.expect ('stream-iq', predicate = jp.action_predicate ('session-accept'))

def general_tests (jp, q, bus, conn, stream, path, props):
    assertEquals (cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])

    channel = bus.get_object (conn.bus_name, path)
    chan_props = channel.GetAll (cs.CHANNEL_TYPE_CALL,
        dbus_interface = dbus.PROPERTIES_IFACE)

    contents = chan_props['Contents']
    assertLength(1, contents)

if __name__ == '__main__':
    exec_test (run_outgoing_test)
    exec_test (run_incoming_test)
