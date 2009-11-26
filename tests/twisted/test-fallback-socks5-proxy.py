import dbus
import socket
from gabbletest import exec_test, elem, elem_iq, sync_stream, make_presence
from servicetest import EventPattern
from caps_helper import make_caps_disco_reply

from twisted.words.xish import xpath

import ns
import constants as cs
from bytestream import create_from_si_offer, BytestreamS5B

proxy_query_events = [
    EventPattern('stream-iq', to='fallback1-proxy.localhost', iq_type='get', query_ns=ns.BYTESTREAMS),
    EventPattern('stream-iq', to='fallback2-proxy.localhost', iq_type='get', query_ns=ns.BYTESTREAMS)]

proxy_port = {'fallback1-proxy.localhost': '12345', 'fallback2-proxy.localhost': '6789'}

def connect_and_announce_alice(q, bus, conn, stream):
    q.forbid_events(proxy_query_events)

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Send Alice's presence
    caps =  { 'ext': '', 'ver': '0.0.0',
        'node': 'http://example.com/fake-client0' }
    presence = make_presence('alice@localhost/Test', caps=caps)
    stream.send(presence)

    disco_event = q.expect('stream-iq', to='alice@localhost/Test',
        query_ns=ns.DISCO_INFO)

    stream.send(make_caps_disco_reply(stream, disco_event.stanza, [ns.TUBES]))
    sync_stream(q, stream)

    q.unforbid_events(proxy_query_events)

def send_socks5_reply(stream, iq):
    jid = iq['to']
    port = proxy_port[jid]

    reply = elem_iq(stream, 'result', id=iq['id'], from_=jid)(
        elem(ns.BYTESTREAMS, 'query')(
            elem('streamhost', jid=jid, host='127.0.0.1', port=port)()))

    stream.send(reply)

def check_socks5_stanza(stanza):
    tmp = proxy_port.copy()
    nodes = xpath.queryForNodes('/iq/query/streamhost', stanza)
    for node in nodes:
        if node['jid'] in tmp:
            assert node['host'] == '127.0.0.1'
            assert node['port'] == tmp.pop(node['jid'])
    assert tmp == {}

def offer_dbus_tube(q, bus, conn, stream):
    connect_and_announce_alice(q, bus, conn, stream)

    # Offer a private D-Bus tube just to check if the proxy is present in the
    # SOCKS5 offer

    path, props = conn.Requests.CreateChannel({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: 'alice@localhost',
        cs.DBUS_TUBE_SERVICE_NAME: 'com.example.TestCase'})

    # Proxy queries are send when creating the channel
    e1, e2 = q.expect_many(*proxy_query_events)

    send_socks5_reply(stream, e1.stanza)
    send_socks5_reply(stream, e2.stanza)

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
    check_socks5_stanza(e.stanza)

def accept_stream_tube(q, bus, conn, stream):
    connect_and_announce_alice(q, bus, conn, stream)

    # Accept a stream tube, we'll need SOCKS5 proxies each time we'll connect
    # on the tube socket

    # Alice offers us a stream tube
    message = elem('message', to='test@localhost/Resource', from_='alice@localhost/Test')(
      elem(ns.TUBES, 'tube', type='stream', service='http', id='10'))
    stream.send(message)

    # we are interested in the 'NewChannels' announcing the tube channel
    def new_chan_predicate(e):
        path, props = e.args[0][0]
        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE

    # Proxy queries are send when receiving an incoming stream tube
    new_chan, e1, e2 = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels', predicate=new_chan_predicate),
        proxy_query_events[0], proxy_query_events[1])

    send_socks5_reply(stream, e1.stanza)
    send_socks5_reply(stream, e2.stanza)

    path, props = new_chan.args[0][0]
    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE

    tube_chan = bus.get_object(conn.bus_name, path)
    tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_STREAM_TUBE)

    # connect to the socket so a SOCKS5 bytestream will be created
    address = tube_iface.Accept(cs.SOCKET_ADDRESS_TYPE_IPV4,
        cs.SOCKET_ACCESS_CONTROL_LOCALHOST, 0, byte_arrays=True)

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(address)

    e = q.expect('stream-iq', to='alice@localhost/Test')

    bytestream, profile = create_from_si_offer(stream, q, BytestreamS5B, e.stanza,
        'test@localhost/Resource')

    # Alice accepts the connection
    result, si = bytestream.create_si_reply(e.stanza)
    stream.send(result)

    e = q.expect('stream-iq', to='alice@localhost/Test')
    check_socks5_stanza(e.stanza)

if __name__ == '__main__':
    params = {'fallback-socks5-proxies': ['fallback1-proxy.localhost', 'fallback2-proxy.localhost']}
    exec_test(offer_dbus_tube, params=params)
    exec_test(accept_stream_tube, params=params)
