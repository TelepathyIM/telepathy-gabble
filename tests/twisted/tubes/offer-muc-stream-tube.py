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
from muctubeutil import get_muc_tubes_channel
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

    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    self_handle = conn.GetSelfHandle()
    self_name = conn.InspectHandles(cs.HT_CONTACT, [self_handle])[0]

    t.check_conn_properties(q, conn)

    room_handle, tubes_chan, tubes_iface = get_muc_tubes_channel(q, bus, conn,
        stream, 'chat@conf.localhost')

    tubes_self_handle = tubes_chan.GetSelfHandle(dbus_interface=cs.CHANNEL_IFACE_GROUP)

    bob_handle = conn.RequestHandles(cs.HT_CONTACT, ['chat@conf.localhost/bob'])[0]

    address = t.create_server(q, address_type)

    # offer stream tube (old API) using an Unix socket
    call_async(q, tubes_iface, 'OfferStreamTube',
        'echo', sample_parameters, address_type, address,
        access_control, access_control_param)

    new_tube_event, stream_event, _, new_channels_event = q.expect_many(
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('stream-presence', to='chat@conf.localhost/test'),
        EventPattern('dbus-return', method='OfferStreamTube'),
        EventPattern('dbus-signal', signal='NewChannels'))

    # handle new_tube_event
    stream_tube_id = new_tube_event.args[0]
    assert new_tube_event.args[1] == tubes_self_handle
    assert new_tube_event.args[2] == 1       # Stream
    assert new_tube_event.args[3] == 'echo'
    assert new_tube_event.args[4] == sample_parameters
    assert new_tube_event.args[5] == cs.TUBE_CHANNEL_STATE_OPEN

    # handle stream_event
    # We announce our newly created tube in our muc presence
    presence = stream_event.stanza
    x_nodes = xpath.queryForNodes('/presence/x[@xmlns="http://jabber.org/'
            'protocol/muc"]', presence)
    assert x_nodes is not None
    assert len(x_nodes) == 1

    tubes_nodes = xpath.queryForNodes('/presence/tubes[@xmlns="%s"]'
        % ns.TUBES, presence)
    assert tubes_nodes is not None
    assert len(tubes_nodes) == 1

    tube_nodes = xpath.queryForNodes('/tubes/tube', tubes_nodes[0])
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    for tube in tube_nodes:
        assert tube['type'] == 'stream'
        assert not tube.hasAttribute('initiator')
        assert tube['service'] == 'echo'
        assert not tube.hasAttribute('stream-id')
        assert not tube.hasAttribute('dbus-name')
        assert tube['id'] == str(stream_tube_id)

    params = {}
    parameter_nodes = xpath.queryForNodes('/tube/parameters/parameter', tube)
    for node in parameter_nodes:
        assert node['name'] not in params
        params[node['name']] = (node['type'], str(node))
    assert params == {'ay': ('bytes', 'aGVsbG8='),
                      's': ('str', 'hello'),
                      'i': ('int', '-123'),
                      'u': ('uint', '123'),
                     }

    # tube is also announced using new API
    channels = new_channels_event.args[0]
    assert len(channels) == 1
    path, props = channels[0]
    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE
    assert props[cs.INITIATOR_HANDLE] == tubes_self_handle
    assert props[cs.INITIATOR_ID] == 'chat@conf.localhost/test'
    assert props[cs.INTERFACES] == [cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_TUBE]
    assert props[cs.REQUESTED] == True
    assert props[cs.TARGET_HANDLE] == room_handle
    assert props[cs.TARGET_ID] == 'chat@conf.localhost'
    assert props[cs.STREAM_TUBE_SERVICE] == 'echo'

    tube_chan = bus.get_object(conn.bus_name, path)
    tube_props = tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE, dbus_interface=cs.PROPERTIES_IFACE,
        byte_arrays=True)
    assert tube_props['Parameters'] == sample_parameters
    assert tube_props['State'] == cs.TUBE_CHANNEL_STATE_OPEN

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert tubes == [(
        stream_tube_id,
        tubes_self_handle,
        1,      # Stream
        'echo',
        sample_parameters,
        cs.TUBE_CHANNEL_STATE_OPEN
        )]

    assert len(tubes) == 1, unwrap(tubes)
    expected_tube = (stream_tube_id, tubes_self_handle, cs.TUBE_TYPE_STREAM,
        'echo', sample_parameters, cs.TUBE_STATE_OPEN)
    t.check_tube_in_tubes(expected_tube, tubes)

    # FIXME: if we use an unknown JID here, everything fails
    # (the code uses lookup where it should use ensure)

    bytestream = connect_to_tube(stream, q, bytestream_cls, 'chat@conf.localhost', stream_tube_id)

    iq_event, socket_event, _, conn_event = q.expect_many(
        EventPattern('stream-iq', iq_type='result'),
        EventPattern('socket-connected'),
        EventPattern('dbus-signal', signal='StreamTubeNewConnection',
            args=[stream_tube_id, bob_handle], interface=cs.CHANNEL_TYPE_TUBES),
        EventPattern('dbus-signal', signal='NewRemoteConnection',
            interface=cs.CHANNEL_TYPE_STREAM_TUBE))

    protocol = socket_event.protocol

    # handle iq_event
    bytestream.check_si_reply(iq_event.stanza)
    tube = xpath.queryForNodes('/iq//si/tube[@xmlns="%s"]' % ns.TUBES, iq_event.stanza)
    assert len(tube) == 1

    handle, access, conn_id = conn_event.args
    assert handle == bob_handle

    use_tube(q, bytestream, protocol, conn_id)

    # offer a stream tube to another room (new API)
    address = t.create_server(q, address_type, block_reading=True)

    request = {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: 'chat2@conf.localhost',
        cs.STREAM_TUBE_SERVICE: 'newecho',
    }
    _, _, new_tube_path, new_tube_props = \
        join_muc(q, bus, conn, stream, 'chat2@conf.localhost', request)

    # first text and tubes channels are announced
    event = q.expect('dbus-signal', signal='NewChannels')
    channels = event.args[0]
    assert len(channels) == 2
    path1, prop1 = channels[0]
    path2, prop2 = channels[1]
    assert sorted([prop1[cs.CHANNEL_TYPE], prop2[cs.CHANNEL_TYPE]]) == \
        [cs.CHANNEL_TYPE_TEXT, cs.CHANNEL_TYPE_TUBES]

    got_text, got_tubes = False, False
    for path, props in channels:
        if props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT:
            got_text = True
        elif props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TUBES:
            got_tubes = True
        else:
            assert False

        assert props[cs.INITIATOR_HANDLE] == self_handle
        assert props[cs.INITIATOR_ID] == self_name
        assert cs.CHANNEL_IFACE_GROUP in props[cs.INTERFACES]
        assert props[cs.TARGET_ID] == 'chat2@conf.localhost'
        assert props[cs.REQUESTED] == False

    assert (got_text, got_tubes) == (True, True)

    # now the tube channel is announced
    # FIXME: in this case, all channels should probably be announced together
    event = q.expect('dbus-signal', signal='NewChannels')
    channels = event.args[0]
    assert len(channels) == 1
    path, prop = channels[0]
    assert prop[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE
    assert prop[cs.INITIATOR_ID] == 'chat2@conf.localhost/test'
    assert prop[cs.REQUESTED] == True
    assert prop[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert prop[cs.TARGET_ID] == 'chat2@conf.localhost'
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

    new_tube_event, stream_event, _, status_event = q.expect_many(
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('stream-presence', to='chat2@conf.localhost/test'),
        EventPattern('dbus-return', method='Offer'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged', args=[cs.TUBE_CHANNEL_STATE_OPEN]))

    tube_self_handle = tube_chan.GetSelfHandle(dbus_interface=cs.CHANNEL_IFACE_GROUP)
    assert conn.InspectHandles(cs.HT_CONTACT, [tube_self_handle]) == ['chat2@conf.localhost/test']

    # handle new_tube_event
    stream_tube_id = new_tube_event.args[0]
    assert new_tube_event.args[2] == 1       # Stream
    assert new_tube_event.args[3] == 'newecho'
    assert new_tube_event.args[4] == {'foo': 'bar'}
    assert new_tube_event.args[5] == cs.TUBE_CHANNEL_STATE_OPEN

    presence = stream_event.stanza
    x_nodes = xpath.queryForNodes('/presence/x[@xmlns="http://jabber.org/'
            'protocol/muc"]', presence)
    assert x_nodes is not None
    assert len(x_nodes) == 1

    tubes_nodes = xpath.queryForNodes('/presence/tubes[@xmlns="%s"]'
        % ns.TUBES, presence)
    assert tubes_nodes is not None
    assert len(tubes_nodes) == 1

    tube_nodes = xpath.queryForNodes('/tubes/tube', tubes_nodes[0])
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    for tube in tube_nodes:
        assert tube['type'] == 'stream'
        assert not tube.hasAttribute('initiator')
        assert tube['service'] == 'newecho'
        assert not tube.hasAttribute('stream-id')
        assert not tube.hasAttribute('dbus-name')
        assert tube['id'] == str(stream_tube_id)

    params = {}
    parameter_nodes = xpath.queryForNodes('/tube/parameters/parameter', tube)
    for node in parameter_nodes:
        assert node['name'] not in params
        params[node['name']] = (node['type'], str(node))
    assert params == {'foo': ('str', 'bar')}

    bob_handle = conn.RequestHandles(cs.HT_CONTACT, ['chat2@conf.localhost/bob'])[0]

    bytestream = connect_to_tube(stream, q, bytestream_cls, 'chat2@conf.localhost', stream_tube_id)

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

if __name__ == '__main__':
    t.exec_stream_tube_test(test)
