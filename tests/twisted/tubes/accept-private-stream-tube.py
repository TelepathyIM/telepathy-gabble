"""
Receives several tube offers:
- Test to accept a 1-1 stream tube
  - using UNIX sockets and IPv4 sockets
  - using the old tube iface and the new tube iface
- Test to accept with bad parameters
- Test to refuse the tube offer
  - using the old tube iface and the new tube iface
"""

import dbus

from servicetest import call_async, EventPattern, sync_dbus, assertEquals
from gabbletest import acknowledge_iq, send_error_reply, make_result_iq

from twisted.words.xish import domish, xpath
from twisted.internet import reactor
import ns
import constants as cs
from bytestream import create_from_si_offer, announce_socks5_proxy
import tubetestutil as t

bob_jid = 'bob@localhost/Bob'
stream_tube_id = 49

def receive_tube_offer(q, bus, conn, stream):
    global stream_tube_id
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    message['from'] = bob_jid
    tube_node = message.addElement((ns.TUBES, 'tube'))
    tube_node['type'] = 'stream'
    tube_node['service'] = 'http'
    stream_tube_id += 1
    tube_node['id'] = str(stream_tube_id)
    stream.send(message)

    def new_chan_predicate(e):
        path, props = e.args[0][0]
        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE

    new_sig = q.expect('dbus-signal', signal='NewChannels',
                       predicate=new_chan_predicate)

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1

    path, props = new_sig.args[0][0]
    assertEquals(cs.CHANNEL_TYPE_STREAM_TUBE, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(False, props[cs.REQUESTED])

    # create channel proxies
    new_tube_chan = bus.get_object(conn.bus_name, path)
    new_tube_iface = dbus.Interface(new_tube_chan, cs.CHANNEL_TYPE_STREAM_TUBE)

    return (new_tube_chan, new_tube_iface)

def expect_tube_activity(q, bus, conn, stream, bytestream_cls, address_type,
    address, access_control, access_control_param):

    event_socket, event_iq, conn_id = t.connect_to_cm_socket(q, bob_jid,
        address_type, address, access_control, access_control_param)

    protocol = event_socket.protocol
    data = b"hello initiator"
    protocol.sendData(data)

    bytestream, profile = create_from_si_offer(stream, q, bytestream_cls, event_iq.stanza,
        'test@localhost/Resource')

    assert profile == ns.TUBES

    stream_node = xpath.queryForNodes('/iq/si/stream[@xmlns="%s"]' %
        ns.TUBES, event_iq.stanza)[0]
    assert stream_node is not None
    assert stream_node['tube'] == str(stream_tube_id)

    result, si = bytestream.create_si_reply(event_iq.stanza)
    si.addElement((ns.TUBES, 'tube'))
    stream.send(result)

    bytestream.wait_bytestream_open()

    binary = bytestream.get_data(len(data))
    assert data == binary, binary

    # reply to the initiator
    bytestream.send_data(b'hello joiner')

    e = q.expect('socket-data')
    assert e.data == b'hello joiner'

    return bytestream, conn_id

def test(q, bus, conn, stream, bytestream_cls,
        address_type, access_control, access_control_param):
    if access_control == cs.SOCKET_ACCESS_CONTROL_CREDENTIALS:
        print("Skip Socket_Access_Control_Credentials (fdo #45445)")
        return

    vcard_event, roster_event, disco_event = q.expect_many(
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns=ns.ROSTER),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, vcard_event.stanza)

    announce_socks5_proxy(q, stream, disco_event.stanza)

    roster = roster_event.stanza
    roster['type'] = 'result'
    item = roster_event.query.addElement('item')
    item['jid'] = 'bob@localhost' # Bob can do tubes
    item['subscription'] = 'both'
    stream.send(roster)

    # Send Bob presence and his caps
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@localhost/Bob'
    presence['to'] = 'test@localhost/Resource'
    c = presence.addElement('c')
    c['xmlns'] = 'http://jabber.org/protocol/caps'
    c['node'] = 'http://example.com/ICantBelieveItsNotTelepathy'
    c['ver'] = '1.2.3'
    stream.send(presence)

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/disco#info',
        to='bob@localhost/Bob')
    assert event.query['node'] == \
        'http://example.com/ICantBelieveItsNotTelepathy#1.2.3'
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES
    stream.send(result)

    sync_dbus(bus, q, conn)

    # Receive a tube offer from Bob
    (new_tube_chan, new_tube_iface) = \
        receive_tube_offer(q, bus, conn, stream)

    # Try bad parameters on the new iface
    call_async(q, new_tube_iface, 'Accept', 20, 0, '',
            byte_arrays=True)
    q.expect('dbus-error', method='Accept')
    call_async(q, new_tube_iface, 'Accept', 0, 1, '',
            byte_arrays=True)
    q.expect('dbus-error', method='Accept')

    # Receive a tube offer from Bob
    (new_tube_chan, new_tube_iface) = \
        receive_tube_offer(q, bus, conn, stream)

    # Accept the tube with new iface, and use UNIX sockets
    call_async(q, new_tube_iface, 'Accept', address_type, access_control,
        access_control_param, byte_arrays=True)

    accept_return_event, _ = q.expect_many(
        EventPattern('dbus-return', method='Accept'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged',
            args=[2]))

    socket_address = accept_return_event.value[0]
    if isinstance(socket_address, bytes):
        socket_address = socket_address.decode()

    bytestream, conn_id = expect_tube_activity(q, bus, conn, stream, bytestream_cls,
        address_type, socket_address, access_control, access_control_param)

    # peer closes the bytestream
    bytestream.close()
    e = q.expect('dbus-signal', signal='ConnectionClosed')
    assertEquals(conn_id, e.args[0])
    assertEquals(cs.CONNECTION_LOST, e.args[1])

    # establish another tube connection
    event_socket, si_event, conn_id = t.connect_to_cm_socket(q, bob_jid,
    address_type, socket_address, access_control, access_control_param)

    # bytestream is refused
    send_error_reply(stream, si_event.stanza)
    e = q.expect('dbus-signal', signal='ConnectionClosed')
    assertEquals(conn_id, e.args[0])
    assertEquals(cs.CONNECTION_REFUSED, e.args[1])

    new_tube_chan.Close()

    # Receive a tube offer from Bob
    (new_tube_chan, new_tube_iface) = \
        receive_tube_offer(q, bus, conn, stream)
    # Just close the tube
    new_tube_chan.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

if __name__ == '__main__':
    t.exec_stream_tube_test(test)
