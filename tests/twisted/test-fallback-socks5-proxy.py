import dbus
from gabbletest import exec_test, elem, elem_iq, sync_stream, make_presence
from servicetest import EventPattern
from caps_helper import make_caps_disco_reply

from twisted.words.xish import xpath

import ns
import constants as cs
from bytestream import create_from_si_offer, BytestreamS5B

def test(q, bus, conn, stream):
    conn.Connect()

    _, e1, e2 = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to='fallback1-proxy.localhost', iq_type='get', query_ns=ns.BYTESTREAMS),
        EventPattern('stream-iq', to='fallback2-proxy.localhost', iq_type='get', query_ns=ns.BYTESTREAMS),
        )

    proxy_port = {'fallback1-proxy.localhost': '12345', 'fallback2-proxy.localhost': '6789'}

    def send_socks5_reply(iq):
        jid = iq['to']
        port = proxy_port[jid]

        reply = elem_iq(stream, 'result', id=iq['id'], from_=jid)(
            elem(ns.BYTESTREAMS, 'query')(
                elem('streamhost', jid=jid, host='127.0.0.1', port=port)()))
        stream.send(reply)

    send_socks5_reply(e1.stanza)
    send_socks5_reply(e2.stanza)

    # Offer a private D-Bus tube just to check if the proxy is present in the
    # SOCKS5 offer

    # Send Alice's presence
    caps =  { 'ext': '', 'ver': '0.0.0',
        'node': 'http://example.com/fake-client0' }
    presence = make_presence('alice@localhost/Test', caps=caps)
    stream.send(presence)

    disco_event = q.expect('stream-iq', to='alice@localhost/Test',
        query_ns=ns.DISCO_INFO)

    stream.send(make_caps_disco_reply(stream, disco_event.stanza, [ns.TUBES]))
    sync_stream(q, stream)

    path, props = conn.Requests.CreateChannel({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: 'alice@localhost',
        cs.DBUS_TUBE_SERVICE_NAME: 'com.example.TestCase'})

    tube_chan = bus.get_object(conn.bus_name, path)
    dbus_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)

    dbus_tube_iface.Offer({}, cs.SOCKET_ACCESS_CONTROL_CREDENTIALS)

    e = q.expect('stream-iq', to='alice@localhost/Test')

    bytestream, profile = create_from_si_offer(stream, q, BytestreamS5B, e.stanza,
        'test@localhost/Resource')

    # Alice accepts the tube
    result, si = bytestream.create_si_reply(e.stanza)
    si.addElement((ns.TUBES, 'tube'))
    stream.send(result)

    e = q.expect('stream-iq', to='alice@localhost/Test')

    found = False
    nodes = xpath.queryForNodes('/iq/query/streamhost', e.stanza)
    for node in nodes:
        if node['jid'] in proxy_port:
            assert node['host'] == '127.0.0.1'
            assert node['port'] == proxy_port.pop(node['jid'])
    assert proxy_port == {}

if __name__ == '__main__':
    exec_test(test, params={'fallback-socks5-proxies':
        ['fallback1-proxy.localhost', 'fallback2-proxy.localhost']})
