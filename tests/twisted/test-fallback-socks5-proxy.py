import dbus
from gabbletest import exec_test, make_result_iq, elem, elem_iq, sync_stream
from servicetest import call_async, EventPattern

from twisted.words.xish import domish, xpath

import ns
import constants as cs
from bytestream import create_from_si_offer, BytestreamS5B

# FIXME: stolen from offer-private-dbus-tube
def make_caps_disco_reply(stream, req, features):
    iq = make_result_iq(stream, req)
    query = iq.firstChildElement()

    for f in features:
        el = domish.Element((None, 'feature'))
        el['var'] = f
        query.addChild(el)

    return iq

# FIXME: stolen from offer-private-dbus-tube
def make_presence(fromjid, tojid, caps=None):
    el = domish.Element(('jabber:client', 'presence',))
    el['from'] = fromjid
    el['to'] = tojid

    if caps:
        cel = domish.Element(('http://jabber.org/protocol/caps', 'c'))
        for key,value in caps.items():
            cel[key] = value
        el.addChild(cel)

    return el

def test(q, bus, conn, stream):
    conn.Connect()

    # discard activities request and status change
    _, e = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to='fallback-proxy.localhost', iq_type='get', query_ns=ns.BYTESTREAMS))

    reply = elem_iq(stream, 'result', id=e.stanza['id'])(
        elem(ns.BYTESTREAMS, 'query')(
            elem('streamhost', jid='fallback-proxy.localhost', host='127.0.0.1', port='12345')()))
    stream.send(reply)

    # Offer a private D-Bus tube just to check if the proxy is present in the
    # SOCKS5 offer
    requestotron = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    # Send Alice's presence
    caps =  { 'ext': '', 'ver': '0.0.0',
        'node': 'http://example.com/fake-client0' }
    presence = make_presence('alice@localhost/Test', 'test@localhost', caps)
    stream.send(presence)

    disco_event = q.expect('stream-iq', to='alice@localhost/Test',
        query_ns=ns.DISCO_INFO)

    stream.send(make_caps_disco_reply(stream, disco_event.stanza, [ns.TUBES]))
    sync_stream(q, stream)

    path, props = requestotron.CreateChannel({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: 'alice@localhost',
        cs.DBUS_TUBE_SERVICE_NAME: 'com.example.TestCase'})

    tube_chan = bus.get_object(conn.bus_name, path)
    dbus_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)

    dbus_tube_iface.OfferDBusTube({})

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
        if node['jid'] == 'fallback-proxy.localhost':
            found = True
            assert node['host'] == '127.0.0.1'
            assert node['port'] == '12345'
            break
    assert found

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
    return True

if __name__ == '__main__':
    exec_test(test, params={'fallback-socks5-proxy': 'fallback-proxy.localhost'})

