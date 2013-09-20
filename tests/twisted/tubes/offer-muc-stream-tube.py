"""Test stream tube support in the context of a MUC."""

import errno
import os

import dbus

from servicetest import call_async, EventPattern, unwrap, assertContains, assertEquals
from gabbletest import acknowledge_iq, make_muc_presence
import constants as cs
import ns
import tubetestutil as t
from mucutil import join_muc
from bytestream import BytestreamS5BRelay, BytestreamS5BRelayBugged

from twisted.words.xish import xpath
from twisted.internet import reactor

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

def connect_to_tube(stream, q, bytestream_cls, muc, stream_tube_id):
    # The CM is the server, so fake a client wanting to talk to it
    bytestream = bytestream_cls(stream, q, 'alpha', '%s/bob' % muc,
        '%s/test' % muc, True)

    # set the real jid of the target as 'to' because the XMPP server changes
    # it when delivering the IQ
    iq, si = bytestream.create_si_offer(ns.TUBES, 'test@localhost/Resource')

    stream_node = si.addElement((ns.TUBES, 'muc-stream'))
    stream_node['tube'] = str(stream_tube_id)
    stream.send(iq)

    return bytestream

def use_tube(q, bytestream, protocol, conn_id):
    # have the fake client open the stream
    bytestream.open_bytestream()

    # have the fake client send us some data
    bytestream.send_data('hello initiator')

    # the server reply
    event = q.expect('socket-data', data='hello initiator', protocol=protocol)
    data = 'hello joiner'
    protocol.sendData(data)

    # we receive server's data
    binary = bytestream.get_data(len(data))
    assert binary == data, binary

    # peer closes the bytestream
    bytestream.close()
    e = q.expect('dbus-signal', signal='ConnectionClosed')
    assertEquals(conn_id, e.args[0])
    assertEquals(cs.CONNECTION_LOST, e.args[1])

def test(q, bus, conn, stream, bytestream_cls,
       address_type, access_control, access_control_param):
    if bytestream_cls in [BytestreamS5BRelay, BytestreamS5BRelayBugged]:
        # disable SOCKS5 relay tests because proxy can't be used with muc
        # contacts atm
        return

    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, iq_event.stanza)

    t.check_conn_properties(q, conn)

    bob_handle = conn.get_contact_handle_sync('chat@conf.localhost/bob')

    address = t.create_server(q, address_type)

    def new_chan_predicate(e):
        types = []
        for _, props in e.args[0]:
            types.append(props[cs.CHANNEL_TYPE])

        return cs.CHANNEL_TYPE_STREAM_TUBE in types

    def find_stream_tube(channels):
        for path, props in channels:
            if props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE:
                return path, props

        return None, None

    # offer a stream tube to another room (new API)
    address = t.create_server(q, address_type, block_reading=True)

    request = {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: 'chat@conf.localhost',
        cs.STREAM_TUBE_SERVICE: 'newecho',
    }
    _, _, new_tube_path, new_tube_props = \
        join_muc(q, bus, conn, stream, 'chat@conf.localhost', request)

    e = q.expect('dbus-signal', signal='NewChannels',
                 predicate=new_chan_predicate)

    path, prop = find_stream_tube(e.args[0])
    assert prop[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE
    assert prop[cs.INITIATOR_ID] == 'chat@conf.localhost/test'
    assert prop[cs.REQUESTED] == True
    assert prop[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert prop[cs.TARGET_ID] == 'chat@conf.localhost'
    assert prop[cs.STREAM_TUBE_SERVICE] == 'newecho'

    # check that the tube channel is in the channels list
    all_channels = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE, byte_arrays=True)
    assertContains((path, prop), all_channels)

    tube_chan = bus.get_object(conn.bus_name, path)
    stream_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_STREAM_TUBE)
    chan_iface = dbus.Interface(tube_chan, cs.CHANNEL)
    tube_props = tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE, dbus_interface=cs.PROPERTIES_IFACE)

    assert tube_props['State'] == cs.TUBE_CHANNEL_STATE_NOT_OFFERED

    # offer the tube
    call_async(q, stream_tube_iface, 'Offer', address_type, address, access_control, {'foo': 'bar'})

    stream_event, _, status_event = q.expect_many(
        EventPattern('stream-presence', to='chat@conf.localhost/test'),
        EventPattern('dbus-return', method='Offer'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged', args=[cs.TUBE_CHANNEL_STATE_OPEN]))

    tube_self_handle = tube_chan.GetSelfHandle(dbus_interface=cs.CHANNEL_IFACE_GROUP)
    assert conn.inspect_contact_sync(tube_self_handle) == 'chat@conf.localhost/test'

    presence = stream_event.stanza
    tubes_nodes = xpath.queryForNodes('/presence/tubes[@xmlns="%s"]'
        % ns.TUBES, presence)
    assert tubes_nodes is not None
    assert len(tubes_nodes) == 1

    stream_tube_id = 666

    tube_nodes = xpath.queryForNodes('/tubes/tube', tubes_nodes[0])
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    for tube in tube_nodes:
        assert tube['type'] == 'stream'
        assert not tube.hasAttribute('initiator')
        assert tube['service'] == 'newecho'
        assert not tube.hasAttribute('stream-id')
        assert not tube.hasAttribute('dbus-name')

        stream_tube_id = int(tube['id'])

    params = {}
    parameter_nodes = xpath.queryForNodes('/tube/parameters/parameter', tube)
    for node in parameter_nodes:
        assert node['name'] not in params
        params[node['name']] = (node['type'], str(node))
    assert params == {'foo': ('str', 'bar')}

    bob_handle = conn.get_contact_handle_sync('chat@conf.localhost/bob')

    bytestream = connect_to_tube(stream, q, bytestream_cls, 'chat@conf.localhost', stream_tube_id)

    iq_event, socket_event, conn_event = q.expect_many(
        EventPattern('stream-iq', iq_type='result'),
        EventPattern('socket-connected'),
        EventPattern('dbus-signal', signal='NewRemoteConnection',
            interface=cs.CHANNEL_TYPE_STREAM_TUBE))

    handle, access, conn_id = conn_event.args
    assert handle == bob_handle

    protocol = socket_event.protocol
    # start to read from the transport so we can read the control byte
    protocol.transport.startReading()
    t.check_new_connection_access(q, access_control, access, protocol)

    # handle iq_event
    bytestream.check_si_reply(iq_event.stanza)
    tube = xpath.queryForNodes('/iq//si/tube[@xmlns="%s"]' % ns.TUBES, iq_event.stanza)
    assert len(tube) == 1

    use_tube(q, bytestream, protocol, conn_id)

    chan_iface.Close()
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    t.cleanup()

if __name__ == '__main__':
    t.exec_stream_tube_test(test)
