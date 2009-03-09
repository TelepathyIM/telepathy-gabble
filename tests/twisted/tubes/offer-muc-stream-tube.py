"""Test stream tube support in the context of a MUC."""

import errno
import os

import dbus

from servicetest import call_async, EventPattern, EventProtocolFactory, unwrap
from gabbletest import make_result_iq, acknowledge_iq, make_muc_presence
import constants as cs
import ns
import tubetestutil as t

from twisted.words.xish import xpath
from twisted.internet import reactor

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

def set_up_listener_socket(q, path):
    factory = EventProtocolFactory(q)
    full_path = os.getcwd() + path
    try:
        os.remove(full_path)
    except OSError, e:
        if e.errno != errno.ENOENT:
            raise
    reactor.listenUNIX(full_path, factory)
    return full_path

def test(q, bus, conn, stream, bytestream_cls):
    srv_path = set_up_listener_socket(q, '/stream')
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    t.check_conn_properties(q, conn)

    self_handle = conn.GetSelfHandle()
    self_name = conn.InspectHandles(1, [self_handle])[0]

    call_async(q, conn, 'RequestHandles', cs.HT_ROOM,
        ['chat@conf.localhost'])

    event = q.expect('stream-iq', to='conf.localhost',
            query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]
    chat_handle = handles[0]

    # request tubes channel
    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TUBES,
        cs.HT_ROOM, chat_handle, True)

    _, stream_event = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', 'chat@conf.localhost', 'test'))

    q.expect('dbus-signal', signal='MembersChanged',
            args=[u'', [2, 3], [], [], [], 0, 0])

    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/test']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']
    bob_handle = 3

    # text and tubes channels are created
    # FIXME: We can't check NewChannel signals (old API) because two of them
    # would be fired and we can't catch twice the same signals without specifying
    # all their arguments.
    new_sig, returned = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-return', method='RequestChannel'))

    channels = new_sig.args[0]
    assert len(channels) == 2

    for channel in channels:
        path, props = channel
        type = props[cs.CHANNEL_TYPE]

        if type == cs.CHANNEL_TYPE_TEXT:
            # check text channel properties
            assert props[cs.TARGET_HANDLE] == chat_handle
            assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
            assert props[cs.TARGET_ID] == 'chat@conf.localhost'
            assert props[cs.REQUESTED] == False
            assert props[cs.INITIATOR_HANDLE] == self_handle
            assert props[cs.INITIATOR_ID] == self_name
        elif type == cs.CHANNEL_TYPE_TUBES:
            # check tubes channel properties
            assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
            assert props[cs.TARGET_HANDLE] == chat_handle
            assert props[cs.TARGET_ID] == 'chat@conf.localhost'
            assert props[cs.REQUESTED] == True
            assert props[cs.INITIATOR_HANDLE] == self_handle
            assert props[cs.INITIATOR_ID] == self_name
        else:
            assert True

    tubes_chan = bus.get_object(conn.bus_name, returned.value[0])
    tubes_iface = dbus.Interface(tubes_chan, cs.CHANNEL_TYPE_TUBES)

    tubes_self_handle = tubes_chan.GetSelfHandle(dbus_interface=cs.CHANNEL_IFACE_GROUP)

    # offer stream tube (old API) using an Unix socket
    call_async(q, tubes_iface, 'OfferStreamTube',
        'echo', sample_parameters, 0, dbus.ByteArray(srv_path), 0, "")

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
    assert props[cs.TARGET_HANDLE] == chat_handle
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

    # The CM is the server, so fake a client wanting to talk to it
    bytestream = bytestream_cls(stream, q, 'alpha', 'chat@conf.localhost/bob',
        'test@localhost/Resource', True)

    iq, si = bytestream.create_si_offer(ns.TUBES)

    stream_node = si.addElement((ns.TUBES, 'muc-stream'))
    stream_node['tube'] = str(stream_tube_id)

    stream.send(iq)

    event = q.expect('socket-connected')
    protocol = event.protocol

    iq_event, _ = q.expect_many(
        EventPattern('stream-iq', iq_type='result'),
        EventPattern('dbus-signal', signal='StreamTubeNewConnection',
            args=[stream_tube_id, bob_handle]))

    # handle iq_event
    bytestream.check_si_reply(iq_event.stanza)
    tube = xpath.queryForNodes('/iq//si/tube[@xmlns="%s"]' % ns.TUBES, iq_event.stanza)
    assert len(tube) == 1

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

    # offer a stream tube to another room (new API)
    srv_path = set_up_listener_socket(q, '/stream2')
    requestotron = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    call_async(q, requestotron, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
         cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
         cs.TARGET_ID: 'chat2@conf.localhost',
         cs.STREAM_TUBE_SERVICE: 'newecho',
        })

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat2@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', 'chat2@conf.localhost', 'test'))

    event = q.expect('dbus-return', method='CreateChannel')
    new_tube_path, new_tube_props = event.value

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

    tube_chan = bus.get_object(conn.bus_name, path)
    stream_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_STREAM_TUBE)
    chan_iface = dbus.Interface(tube_chan, cs.CHANNEL)
    tube_props = tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE, dbus_interface=cs.PROPERTIES_IFACE)

    assert tube_props['State'] == cs.TUBE_CHANNEL_STATE_NOT_OFFERED

    # offer the tube
    call_async(q, stream_tube_iface, 'OfferStreamTube',
        cs.SOCKET_ADDRESS_TYPE_UNIX, dbus.ByteArray(srv_path), cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "",
        {'foo': 'bar'})

    new_tube_event, stream_event, _, status_event = q.expect_many(
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('stream-presence', to='chat2@conf.localhost/test'),
        EventPattern('dbus-return', method='OfferStreamTube'),
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

    chan_iface.Close()
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    # OK, we're done
    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    t.exec_tube_test(test)
