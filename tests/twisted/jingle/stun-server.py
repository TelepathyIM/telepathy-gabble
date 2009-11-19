"""
Test getting STUN server from Google jingleinfo
"""

import dbus
import socket

from gabbletest import exec_test, make_result_iq, sync_stream, GoogleXmlStream
from servicetest import (
    make_channel_proxy, EventPattern,
    assertEquals, assertLength, assertNotEquals, assertEquals
    )
import jingletest
import constants as cs

def test_stun_server(stun_server_prop,
        expected_stun_server=None, expected_stun_port=None):
    if expected_stun_server == None:
        # If there is no stun server set then gabble should fallback on the
        # default fallback stunserver (stun.collabora.co.uk)
        # This test uses the test-resolver which is set to
        # have 'stun.collabora.co.uk' resolve to '6.7.8.9'
        expected_stun_server = '6.7.8.9'
        expected_stun_port = 3478

    assertEquals ([(expected_stun_server, expected_stun_port)],
        stun_server_prop)

def init_test(q, conn, stream, google=False):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    expected = [EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])]

    if google:
        # See: http://code.google.com/apis/talk/jep_extensions/jingleinfo.html
        expected.append(EventPattern('stream-iq', query_ns='google:jingleinfo',
                to='test@localhost'))

    events = q.expect_many(*expected)

    if google:
        event = events[-1]
        jingleinfo = make_result_iq(stream, event.stanza)
        stun = jingleinfo.firstChildElement().addElement('stun')
        server = stun.addElement('server')
        server['host'] = 'resolves-to-1.2.3.4'
        server['udp'] = '12345'
        relay = jingleinfo.firstChildElement().addElement('relay')
        relay.addElement('token', content='jingle all the way')
        stream.send(jingleinfo)

    # We need remote end's presence for capabilities
    jt.send_remote_presence()

    # Gabble doesn't trust it, so makes a disco
    event = q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
             to='foo@bar.com/Foo')

    jt.send_remote_disco_reply(event.stanza)

    # Force Gabble to process the caps before calling RequestChannel
    sync_stream(q, stream)

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    return jt, remote_handle

def test_streamed_media(q, bus, conn, stream,
         expected_stun_server=None, expected_stun_port=None, google=False,
         expected_relays=[]):
    # Initialize the test values
    jt, remote_handle = init_test(q, conn, stream, google)

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

    test_stun_server(sh_props['STUNServers'],
            expected_stun_server, expected_stun_port)

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

    if expected_stun_server is not None:
        assert tp_props['stun-server']['value'] == expected_stun_server

    if expected_stun_port is not None:
        assert tp_props['stun-port']['value'] == expected_stun_port

    if google:
        assert tp_props['gtalk-p2p-relay-token']['value'] == 'jingle all the way'

    media_chan.RemoveMembers([dbus.UInt32(1)], 'rejected')

    q.expect_many(
            EventPattern('stream-iq',
                predicate=lambda e: e.query is not None and
                    e.query.name == 'jingle' and
                    e.query['action'] == 'session-terminate'),
            EventPattern('dbus-signal', signal='Closed'),
            )

def test_call(q, bus, conn, stream,
         expected_stun_server=None, expected_stun_port=None, google=False,
         expected_relays=[]):
    # Initialize the test values
    jt, remote_handle = init_test(q, conn, stream, google)

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

    e = q.expect('dbus-signal', signal='NewChannels')
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
    assertEquals(cs.CALL_STREAM_TRANSPORT_GOOGLE, stream_props['Transport'])

    test_stun_server(stream_props['STUNServers'],
            expected_stun_server, expected_stun_port)

    assertEquals(expected_relays, stream_props['RelayInfo'])

if __name__ == '__main__':
    # StreamedMedia tests
    exec_test(lambda q, b, c, s: test_streamed_media(q, b, c, s,
        google=False))
    exec_test(lambda q, b, c, s: test_streamed_media(q, b, c, s,
        google=True, expected_stun_server='1.2.3.4', expected_stun_port=12345),
        protocol=GoogleXmlStream)
    exec_test(lambda q, b, c, s: test_streamed_media(q, b, c, s,
        google=True, expected_stun_server='5.4.3.2', expected_stun_port=54321),
        protocol=GoogleXmlStream,
        params={'stun-server': 'resolves-to-5.4.3.2',
            'stun-port': dbus.UInt16(54321)})
    exec_test(lambda q, b, c, s: test_streamed_media(q, b, c, s,
        google=True, expected_stun_server='1.2.3.4', expected_stun_port=12345),
        protocol=GoogleXmlStream,
        params={'fallback-stun-server': 'resolves-to-5.4.3.2',
            'fallback-stun-port': dbus.UInt16(54321)})
    exec_test(lambda q, b, c, s: test_streamed_media(q, b, c, s,
        google=False, expected_stun_server='5.4.3.2', expected_stun_port=54321),
        params={'fallback-stun-server': 'resolves-to-5.4.3.2',
            'fallback-stun-port': dbus.UInt16(54321)})

    # Call tests
    exec_test(lambda q, b, c, s: test_call(q, b, c, s,
        google=False))
    exec_test(lambda q, b, c, s: test_call(q, b, c, s,
        google=True, expected_stun_server='1.2.3.4', expected_stun_port=12345),
        protocol=GoogleXmlStream)
    exec_test(lambda q, b, c, s: test_call(q, b, c, s,
        google=True, expected_stun_server='5.4.3.2', expected_stun_port=54321),
        protocol=GoogleXmlStream,
        params={'stun-server': 'resolves-to-5.4.3.2',
            'stun-port': dbus.UInt16(54321)})
    exec_test(lambda q, b, c, s: test_call(q, b, c, s,
        google=True, expected_stun_server='1.2.3.4', expected_stun_port=12345),
        protocol=GoogleXmlStream,
        params={'fallback-stun-server': 'resolves-to-5.4.3.2',
            'fallback-stun-port': dbus.UInt16(54321)})
    exec_test(lambda q, b, c, s: test_call(q, b, c, s,
        google=False, expected_stun_server='5.4.3.2', expected_stun_port=54321),
        params={'fallback-stun-server': 'resolves-to-5.4.3.2',
            'fallback-stun-port': dbus.UInt16(54321)})
