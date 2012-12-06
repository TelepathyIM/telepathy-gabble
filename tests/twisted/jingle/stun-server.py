"""
Test getting STUN server from Google jingleinfo
"""

from functools import partial
import dbus
import socket

from gabbletest import make_result_iq, GoogleXmlStream, elem_iq, elem
from servicetest import (
    make_channel_proxy, EventPattern,
    assertEquals, assertLength, assertNotEquals, assertEquals
    )
from jingletest2 import test_all_dialects, JingleTest2
import constants as cs
import ns

from config import CHANNEL_TYPE_CALL_ENABLED, GOOGLE_RELAY_ENABLED, VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

def test_stun_server(stun_server_prop, expected_stun_servers=None):
    if expected_stun_servers is None:
        # If there is no stun server set, and it can't discover some from the
        # network, then gabble should fallback on the default fallback stun
        # server (stun.telepathy.im)
        #
        # This test uses the test-resolver which is set to
        # have 'stun.telepathy.im' resolve to '6.7.8.9'
        expected_stun_servers=[('6.7.8.9', 3478)]

    assertEquals(expected_stun_servers, stun_server_prop)

def add_jingle_info(jingleinfo, stun_server, stun_port):
    stun = jingleinfo.firstChildElement().addElement('stun')
    server = stun.addElement('server')
    server['host'] = stun_server
    server['udp'] = stun_port
    relay = jingleinfo.firstChildElement().addElement('relay')
    relay.addElement('token', content='jingle all the way')

def handle_jingle_info_query(q, stream, stun_server, stun_port):
    # See: http://code.google.com/apis/talk/jep_extensions/jingleinfo.html
    event = q.expect('stream-iq', query_ns=ns.GOOGLE_JINGLE_INFO,
        to=stream.authenticator.bare_jid)
    jingleinfo = make_result_iq(stream, event.stanza)
    add_jingle_info(jingleinfo, stun_server, stun_port)
    stream.send(jingleinfo)

def push_jingle_info(q, stream, stun_server, stun_port):
    iq = elem_iq(stream, 'set')(elem(ns.GOOGLE_JINGLE_INFO, 'query'))
    add_jingle_info(iq, stun_server, stun_port)
    stream.send(iq)
    q.expect('stream-iq', iq_type='result', iq_id=iq['id'])

def init_test(jp, q, conn, stream, google=False, google_push_replacements=None):
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    if google:
        handle_jingle_info_query(q, stream, 'resolves-to-1.2.3.4', '12345')

        if google_push_replacements is not None:
            # oh no! the server changed its mind!
            server, port = google_push_replacements
            push_jingle_info(q, stream, server, port)
    else:
        # We shouldn't be sending google:jingleinfo queries if the server
        # doesn't support it.
        q.forbid_events([
            EventPattern('stream-iq', query_ns=ns.GOOGLE_JINGLE_INFO),
            ])

    jt.send_presence_and_caps()

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    return jt, remote_handle

def test_streamed_media(jp, q, bus, conn, stream,
         expected_stun_servers=None, google=False, google_push_replacements=None,
         expected_relays=[]):
    # Initialize the test values
    jt, remote_handle = init_test(jp, q, conn, stream, google, google_push_replacements)

    # Remote end calls us
    jt.incoming_call()

    # FIXME: these signals are not observable by real clients, since they
    #        happen before NewChannels.
    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, cs.GC_REASON_INVITED])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    media_chan = make_channel_proxy(conn, e.path, 'Channel.Interface.Group')

    # Exercise channel properties
    channel_props = media_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == 1
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == 'foo@bar.com'
    assert channel_props['InitiatorHandle'] == remote_handle

    # The new API for STUN servers etc.
    sh_props = stream_handler.GetAll(
            'org.freedesktop.Telepathy.Media.StreamHandler',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assert sh_props['NATTraversal'] == 'gtalk-p2p'
    assert sh_props['CreatedLocally'] == False

    test_stun_server(sh_props['STUNServers'], expected_stun_servers)
    assert sh_props['RelayInfo'] == expected_relays

    # consistency check, since we currently reimplement Get separately
    for k in sh_props:
        assert sh_props[k] == stream_handler.Get(
                'org.freedesktop.Telepathy.Media.StreamHandler', k,
                dbus_interface=dbus.PROPERTIES_IFACE), k

    # The old API for STUN servers etc. still needs supporting, for farsight 1
    tp_prop_list = media_chan.ListProperties(dbus_interface=cs.TP_AWKWARD_PROPERTIES)
    tp_props = {}
    tp_prop_ids = {}

    for spec in tp_prop_list:
        tp_prop_ids[spec[0]] = spec[1]
        tp_props[spec[1]] = { 'id': spec[0], 'sig': spec[2], 'flags': spec[3] }

    assert 'nat-traversal' in tp_props
    assert tp_props['nat-traversal']['sig'] == 's'
    assert tp_props['nat-traversal']['flags'] == cs.PROPERTY_FLAG_READ
    assert 'stun-server' in tp_props
    assert tp_props['stun-server']['sig'] == 's'
    assert 'stun-port' in tp_props
    assert tp_props['stun-port']['sig'] in ('u', 'q')
    assert 'gtalk-p2p-relay-token' in tp_props
    assert tp_props['gtalk-p2p-relay-token']['sig'] == 's'

    assert tp_props['stun-server']['flags'] == cs.PROPERTY_FLAG_READ
    assert tp_props['stun-port']['flags'] == cs.PROPERTY_FLAG_READ

    if google:
        assert tp_props['gtalk-p2p-relay-token']['flags'] == cs.PROPERTY_FLAG_READ
    else:
        assert tp_props['gtalk-p2p-relay-token']['flags'] == 0

    tp_prop_values = media_chan.GetProperties(
            [tp_props[k]['id'] for k in tp_props if tp_props[k]['flags']],
            dbus_interface=cs.TP_AWKWARD_PROPERTIES)

    for value in tp_prop_values:
        assert value[0] in tp_prop_ids
        tp_props[tp_prop_ids[value[0]]]['value'] = value[1]

    assert tp_props['nat-traversal']['value'] == 'gtalk-p2p'

    if expected_stun_servers is not None:
        expected_stun_server, expected_stun_port = expected_stun_servers[0]
        assert tp_props['stun-server']['value'] == expected_stun_server
        assert tp_props['stun-port']['value'] == expected_stun_port

    if google:
        assert tp_props['gtalk-p2p-relay-token']['value'] == 'jingle all the way'

    media_chan.RemoveMembers([dbus.UInt32(1)], 'rejected')

    q.expect_many(
            EventPattern('stream-iq',
                predicate=jp.action_predicate('session-terminate')),
            EventPattern('dbus-signal', signal='Closed'),
            )

def test_call(jp, q, bus, conn, stream,
         expected_stun_servers=None, google=False, google_push_replacements=None,
         expected_relays=[]):
    # Initialize the test values
    jt, remote_handle = init_test(jp, q, conn, stream, google, google_push_replacements)

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

    # Remote end calls us
    jt.incoming_call()

    e = q.expect('dbus-signal', signal='ServerInfoRetrieved')
    assertLength(0, e.args)
    assertEquals(e.interface, cs.CALL_STREAM_IFACE_MEDIA)

    e = q.expect('dbus-signal', signal='NewChannels',
        predicate=lambda e: cs.CHANNEL_TYPE_CONTACT_LIST not in e.args)
    assert e.args[0][0][0]

    call_chan = make_channel_proxy(conn, e.args[0][0][0], 'Channel')

    # Exercise channel properties
    channel_props = call_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals(remote_handle, channel_props['TargetHandle'])
    assertEquals(1, channel_props['TargetHandleType'])
    assertEquals('foo@bar.com', channel_props['TargetID'])
    assertEquals(False, channel_props['Requested'])
    assertEquals('foo@bar.com', channel_props['InitiatorID'])
    assertEquals(remote_handle, channel_props['InitiatorHandle'])

    # Get the call's Content object
    channel_props = call_chan.Get(cs.CHANNEL_TYPE_CALL, 'Contents',
        dbus_interface=dbus.PROPERTIES_IFACE)
    assertLength(1, channel_props)
    assert len(channel_props[0]) > 0
    assertNotEquals('/', channel_props[0])

    # Get the call's Stream object
    call_content = make_channel_proxy(conn,
        channel_props[0], 'Call.Content.Draft')
    content_props = call_content.Get(cs.CALL_CONTENT, 'Streams',
        dbus_interface=dbus.PROPERTIES_IFACE)
    assertLength(1, content_props)
    assert len(content_props[0]) > 0
    assertNotEquals('/', content_props[0])

    # Test the call's Stream's properties
    call_stream = make_channel_proxy(conn,
        content_props[0], 'Call.Stream.Interface.Media.Draft')
    stream_props = call_stream.GetAll(cs.CALL_STREAM_IFACE_MEDIA,
        dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals(cs.CALL_STREAM_TRANSPORT_GTALK_P2P, stream_props['Transport'])

    test_stun_server(stream_props['STUNServers'], expected_stun_servers)

    assertEquals(expected_relays, stream_props['RelayInfo'])
    assertEquals(True, stream_props['HasServerInfo'])

if __name__ == '__main__':
    # StreamedMedia tests
    test_all_dialects(partial(test_streamed_media,
        google=False))
    test_all_dialects(partial(test_streamed_media,
        google=False, expected_stun_servers=[('5.4.3.2', 54321)]),
        params={'fallback-stun-server': 'resolves-to-5.4.3.2',
            'fallback-stun-port': dbus.UInt16(54321)})
    test_all_dialects(partial(test_streamed_media, google=False,
                expected_stun_servers=[('5.4.3.2', 1)]),
        params={'account': 'test@stunning.localhost'})

    if GOOGLE_RELAY_ENABLED:
        test_all_dialects(partial(test_streamed_media,
            google=True, expected_stun_servers=[('1.2.3.4', 12345)]),
            protocol=GoogleXmlStream)
        test_all_dialects(partial(test_streamed_media,
            google=True, expected_stun_servers=[('5.4.3.2', 54321)]),
            protocol=GoogleXmlStream,
            params={'stun-server': 'resolves-to-5.4.3.2',
                'stun-port': dbus.UInt16(54321)})
        test_all_dialects(partial(test_streamed_media,
            google=True, expected_stun_servers=[('1.2.3.4', 12345)]),
            protocol=GoogleXmlStream,
            params={'fallback-stun-server': 'resolves-to-5.4.3.2',
                'fallback-stun-port': dbus.UInt16(54321)})
        test_all_dialects(partial(test_streamed_media,
            google=True, google_push_replacements=('resolves-to-5.4.3.2', '3838'),
            expected_stun_servers=[('5.4.3.2', 3838)]),
            protocol=GoogleXmlStream)
    else:
        print "NOTE: built with --disable-google-relay; omitting StreamedMedia tests with Google relay"

    # Call tests
    if CHANNEL_TYPE_CALL_ENABLED:
        test_all_dialects(partial(test_call,
            google=False))
        test_all_dialects(partial(test_call,
            google=False, expected_stun_servers=[('5.4.3.2', 54321)]),
            params={'fallback-stun-server': 'resolves-to-5.4.3.2',
                'fallback-stun-port': dbus.UInt16(54321)})
        test_all_dialects(partial(test_call,
            google=False, expected_stun_servers=[('5.4.3.2', 1)]),
            params={'account': 'test@stunning.localhost'})
    else:
        print "NOTE: built with --disable-channel-type-call; omitting Call tests"

    if CHANNEL_TYPE_CALL_ENABLED and GOOGLE_RELAY_ENABLED:
        test_all_dialects(partial(test_call,
            google=True, expected_stun_servers=[('1.2.3.4', 12345)]),
            protocol=GoogleXmlStream)
        test_all_dialects(partial(test_call,
            google=True, expected_stun_servers=[('5.4.3.2', 54321)]),
            protocol=GoogleXmlStream,
            params={'stun-server': 'resolves-to-5.4.3.2',
                'stun-port': dbus.UInt16(54321)})
        test_all_dialects(partial(test_call,
            google=True, expected_stun_servers=[('1.2.3.4', 12345)]),
            protocol=GoogleXmlStream,
            params={'fallback-stun-server': 'resolves-to-5.4.3.2',
                'fallback-stun-port': dbus.UInt16(54321)})
        test_all_dialects(partial(test_call,
            google=True, google_push_replacements=('resolves-to-5.4.3.2', '3838'),
            expected_stun_servers=[('5.4.3.2', 3838)]),
            protocol=GoogleXmlStream)
    else:
        print "NOTE: built with --disable-channel-type-call or with --disable-google-relay; omitting Call tests with Google relay"

