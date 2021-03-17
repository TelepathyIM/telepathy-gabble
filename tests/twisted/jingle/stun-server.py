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

from config import GOOGLE_RELAY_ENABLED, VOIP_ENABLED

if not VOIP_ENABLED:
    print("NOTE: built with --disable-voip")
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

    remote_handle = conn.get_contact_handle_sync("foo@bar.com/Foo")

    return jt, remote_handle

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
                cs.CHANNEL_TYPE_CALL + '/ice',
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
    # Call tests
    test_all_dialects(partial(test_call,
        google=False))
    test_all_dialects(partial(test_call,
        google=False, expected_stun_servers=[('5.4.3.2', 54321)]),
        params={'fallback-stun-server': 'resolves-to-5.4.3.2',
            'fallback-stun-port': dbus.UInt16(54321)})
    test_all_dialects(partial(test_call,
        google=False, expected_stun_servers=[('5.4.3.2', 1)]),
        params={'account': 'test@stunning.localhost'})

    if GOOGLE_RELAY_ENABLED:
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
        print("NOTE: built with --disable-google-relay; omitting Call tests with Google relay")

