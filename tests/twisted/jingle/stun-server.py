"""
Test getting STUN server from Google jingleinfo
"""

from gabbletest import exec_test, make_result_iq, sync_stream, \
        GoogleXmlStream
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        EventPattern
import jingletest
import gabbletest
import constants as c
import dbus
import time

def test(q, bus, conn, stream,
         expected_stun_server=None, expected_stun_port=None, google=False):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('stream-authenticated'),
            EventPattern('dbus-signal', signal='PresenceUpdate',
                args=[{1L: (0L, {u'available': {}})}]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
            )

    if google:
        # See: http://code.google.com/apis/talk/jep_extensions/jingleinfo.html
        event = q.expect('stream-iq', query_ns='google:jingleinfo',
                to='test@localhost')
        jingleinfo = make_result_iq(stream, event.stanza)
        stun = jingleinfo.firstChildElement().addElement('stun')
        server = stun.addElement('server')
        server['host'] = '1.2.3.4'
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

    # Remote end calls us
    jt.incoming_call()

    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, 0])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    media_chan = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.Group')

    # Exercise channel properties
    channel_props = media_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == 1
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == 'foo@bar.com'
    assert channel_props['InitiatorHandle'] == remote_handle

    tp_prop_list = media_chan.ListProperties(dbus_interface=c.TP_AWKWARD_PROPERTIES)
    tp_props = {}
    tp_prop_ids = {}

    for spec in tp_prop_list:
        tp_prop_ids[spec[0]] = spec[1]
        tp_props[spec[1]] = { 'id': spec[0], 'sig': spec[2], 'flags': spec[3] }

    assert 'nat-traversal' in tp_props
    assert tp_props['nat-traversal']['sig'] == 's'
    assert tp_props['nat-traversal']['flags'] == c.PROPERTY_FLAG_READ
    assert 'stun-server' in tp_props
    assert tp_props['stun-server']['sig'] == 's'
    assert 'stun-port' in tp_props
    assert tp_props['stun-port']['sig'] in ('u', 'q')
    assert 'gtalk-p2p-relay-token' in tp_props
    assert tp_props['gtalk-p2p-relay-token']['sig'] == 's'

    if expected_stun_server is None:
        assert tp_props['stun-server']['flags'] == 0
    else:
        assert tp_props['stun-server']['flags'] == c.PROPERTY_FLAG_READ

    if expected_stun_port is None:
        assert tp_props['stun-port']['flags'] == 0
    else:
        assert tp_props['stun-port']['flags'] == c.PROPERTY_FLAG_READ

    if google:
        assert tp_props['gtalk-p2p-relay-token']['flags'] == c.PROPERTY_FLAG_READ
    else:
        assert tp_props['gtalk-p2p-relay-token']['flags'] == 0

    tp_prop_values = media_chan.GetProperties(
            [tp_props[k]['id'] for k in tp_props if tp_props[k]['flags']],
            dbus_interface=c.TP_AWKWARD_PROPERTIES)

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

    iq, signal = q.expect_many(
            EventPattern('stream-iq'),
            EventPattern('dbus-signal', signal='Closed'),
            )
    assert iq.query.name == 'jingle'
    assert iq.query['action'] == 'session-terminate'

    # Tests completed, close the connection

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(lambda q, b, c, s: test(q, b, c, s,
        google=False, expected_stun_server=None, expected_stun_port=None))
    exec_test(lambda q, b, c, s: test(q, b, c, s,
        google=True, expected_stun_server='1.2.3.4', expected_stun_port=12345),
        protocol=GoogleXmlStream)
    exec_test(lambda q, b, c, s: test(q, b, c, s,
        google=True, expected_stun_server='5.4.3.2', expected_stun_port=54321),
        protocol=GoogleXmlStream,
        params={'stun-server': '5.4.3.2', 'stun-port': dbus.UInt16(54321)})
    exec_test(lambda q, b, c, s: test(q, b, c, s,
        google=True, expected_stun_server='1.2.3.4', expected_stun_port=12345),
        protocol=GoogleXmlStream,
        params={'fallback-stun-server': '5.4.3.2',
            'fallback-stun-port': dbus.UInt16(54321)})
