"""Test IBB tube support in the context of a MUC."""

import base64

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, assertContains, assertEquals, wrap_channel
from gabbletest import exec_test, acknowledge_iq, elem, make_muc_presence, sync_stream
import ns
import constants as cs
import tubetestutil as t

from twisted.words.xish import xpath

from mucutil import join_muc, echo_muc_presence

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

def check_tube_in_presence(presence, initiator):
    tubes_nodes = xpath.queryForNodes('/presence/tubes[@xmlns="%s"]'
        % ns.TUBES, presence)
    assert tubes_nodes is not None
    assert len(tubes_nodes) == 1

    tube_nodes = xpath.queryForNodes('/tubes/tube', tubes_nodes[0])
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    for tube in tube_nodes:
        tube['type'] = 'dbus'
        assert tube['initiator'] == initiator
        assert tube['service'] == 'com.example.TestCase'
        dbus_stream_id = tube['stream-id']
        my_bus_name = tube['dbus-name']
        dbus_tube_id = tube['id']

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

    return dbus_stream_id, my_bus_name, dbus_tube_id


def fire_signal_on_tube(q, tube, chatroom, dbus_stream_id, my_bus_name):
    signal = SignalMessage('/', 'foo.bar', 'baz')
    signal.append(42, signature='u')
    tube.send_message(signal)

    event = q.expect('stream-message', to=chatroom,
        message_type='groupchat')
    message = event.stanza

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % ns.MUC_BYTESTREAM,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == dbus_stream_id
    binary = base64.b64decode(str(ibb_data))
    # little and big endian versions of: SIGNAL, NO_REPLY, protocol v1,
    # 4-byte payload
    assert binary.startswith('l\x04\x01\x01' '\x04\x00\x00\x00') or \
           binary.startswith('B\x04\x01\x01' '\x00\x00\x00\x04')
    # little and big endian versions of the 4-byte payload, UInt32(42)
    assert (binary[0] == 'l' and binary.endswith('\x2a\x00\x00\x00')) or \
           (binary[0] == 'B' and binary.endswith('\x00\x00\x00\x2a'))
    # XXX: verify that it's actually in the "sender" slot, rather than just
    # being in the message somewhere
    assert my_bus_name in binary

    # Send another big signal which has to be split on 3 stanzas
    signal = SignalMessage('/', 'foo.bar', 'baz')
    signal.append('a' * 100000, signature='s')
    tube.send_message(signal)

    def wait_for_data(q):
        event = q.expect('stream-message', to=chatroom,
            message_type='groupchat')

        data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % ns.MUC_BYTESTREAM,
            event.stanza)
        ibb_data = data_nodes[0]

        return ibb_data['frag']

    frag = wait_for_data(q)
    assertEquals(frag, 'first')

    frag = wait_for_data(q)
    assertEquals(frag, 'middle')

    frag = wait_for_data(q)
    assertEquals(frag, 'last')

def test(q, bus, conn, stream, access_control):
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, iq_event.stanza)

    # check if we can request muc D-Bus tube
    t.check_conn_properties(q, conn)

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    self_name = conn.inspect_contact_sync(self_handle)

    # offer a D-Bus tube to another room using new API
    muc = 'chat2@conf.localhost'
    request = {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: 'chat2@conf.localhost',
        cs.DBUS_TUBE_SERVICE_NAME: 'com.example.TestCase',
    }
    join_muc(q, bus, conn, stream, muc, request=request)

    e = q.expect('dbus-signal', signal='NewChannels')

    channels = e.args[0]
    assert len(channels) == 1
    path, prop = channels[0]
    assert prop[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE
    assert prop[cs.INITIATOR_ID] == 'chat2@conf.localhost/test'
    assert prop[cs.REQUESTED] == True
    assert prop[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert prop[cs.TARGET_ID] == 'chat2@conf.localhost'
    assert prop[cs.DBUS_TUBE_SERVICE_NAME] == 'com.example.TestCase'
    assert prop[cs.DBUS_TUBE_SUPPORTED_ACCESS_CONTROLS] == [cs.SOCKET_ACCESS_CONTROL_CREDENTIALS,
            cs.SOCKET_ACCESS_CONTROL_LOCALHOST]

    # check that the tube channel is in the channels list
    all_channels = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE, byte_arrays=True)
    assertContains((path, prop), all_channels)

    tube_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'DBusTube')
    dbus_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)
    chan_iface = dbus.Interface(tube_chan, cs.CHANNEL)
    tube_props = tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE, dbus_interface=cs.PROPERTIES_IFACE,
        byte_arrays=True)

    assert tube_props['State'] == cs.TUBE_CHANNEL_STATE_NOT_OFFERED

    # try to offer using a wrong access control
    try:
        dbus_tube_iface.Offer(sample_parameters, cs.SOCKET_ACCESS_CONTROL_PORT)
    except dbus.DBusException, e:
        assertEquals(e.get_dbus_name(), cs.INVALID_ARGUMENT)
    else:
        assert False

    # offer the tube
    call_async(q, dbus_tube_iface, 'Offer', sample_parameters, access_control)

    presence_event, return_event, status_event, dbus_changed_event = q.expect_many(
        EventPattern('stream-presence', to='chat2@conf.localhost/test'),
        EventPattern('dbus-return', method='Offer'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged', args=[cs.TUBE_CHANNEL_STATE_OPEN]),
        EventPattern('dbus-signal', signal='DBusNamesChanged', interface=cs.CHANNEL_TYPE_DBUS_TUBE))

    tube_self_handle = tube_chan.Properties.Get(cs.CHANNEL_IFACE_GROUP, 'SelfHandle')
    assert tube_self_handle != 0

    # handle presence_event
    # We announce our newly created tube in our muc presence
    presence = presence_event.stanza
    dbus_stream_id, my_bus_name, dbus_tube_id = check_tube_in_presence(presence,
                                                                       'chat2@conf.localhost/test')

    # handle dbus_changed_event
    added, removed = dbus_changed_event.args
    assert added == {tube_self_handle: my_bus_name}
    assert removed == []

    dbus_tube_adr = return_event.value[0]

    bob_bus_name = ':2.Ym9i'
    bob_handle = conn.get_contact_handle_sync('chat2@conf.localhost/bob')

    def bob_in_tube():
        presence = elem('presence', from_='chat2@conf.localhost/bob', to='chat2@conf.localhost')(
            elem('x', xmlns=ns.MUC_USER),
            elem('tubes', xmlns=ns.TUBES)(
                elem('tube', type='dbus', initiator='chat2@conf.localhost/test',
                    service='com.example.TestCase', id=str(dbus_tube_id))(
                        elem('parameters')(
                            elem('parameter', name='ay', type='bytes')(u'aGVsbG8='),
                            elem('parameter', name='s', type='str')(u'hello'),
                            elem('parameter', name='i', type='int')(u'-123'),
                            elem('parameter', name='u', type='uint')(u'123')
                            ))))

        # have to add stream-id and dbus-name attributes manually as we can't use
        # keyword with '-'...
        tube_node = xpath.queryForNodes('/presence/tubes/tube', presence)[0]
        tube_node['stream-id'] = dbus_stream_id
        tube_node['dbus-name'] = bob_bus_name
        stream.send(presence)

    # Bob joins the tube
    bob_in_tube()

    dbus_changed_event = q.expect('dbus-signal', signal='DBusNamesChanged',
        interface=cs.CHANNEL_TYPE_DBUS_TUBE)

    added, removed = dbus_changed_event.args
    assert added == {bob_handle: bob_bus_name}
    assert removed == []

    tube = Connection(dbus_tube_adr)
    fire_signal_on_tube(q, tube, 'chat2@conf.localhost', dbus_stream_id, my_bus_name)

    names = tube_chan.Get(cs.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames', dbus_interface=cs.PROPERTIES_IFACE)
    assert names == {tube_self_handle: my_bus_name, bob_handle: bob_bus_name}

    # Bob leave the tube
    presence = elem('presence', from_='chat2@conf.localhost/bob', to='chat2@conf.localhost')(
        elem('x', xmlns=ns.MUC_USER),
        elem('tubes', xmlns=ns.TUBES))
    stream.send(presence)

    dbus_changed_event = q.expect('dbus-signal', signal='DBusNamesChanged',
        interface=cs.CHANNEL_TYPE_DBUS_TUBE)

    added, removed = dbus_changed_event.args
    assert added == {}
    assert removed == [bob_handle]

    names = tube_chan.Get(cs.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames', dbus_interface=cs.PROPERTIES_IFACE)
    assert names == {tube_self_handle: my_bus_name}

    chan_iface.Close()
    _, _, event = q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('stream-presence', to='chat2@conf.localhost/test',
                     presence_type='unavailable'))

    # we must echo the MUC presence so the room will actually close
    # and we should wait to make sure gabble has actually parsed our
    # echo before trying to rejoin
    echo_muc_presence(q, stream, event.stanza, 'none', 'participant')
    sync_stream(q, stream)

    # rejoin the room
    call_async(q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: 'chat2@conf.localhost' })

    q.expect('stream-presence', to='chat2@conf.localhost/test')

    # Bob is in the room and in the tube
    bob_in_tube()

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', muc, 'test'))

    def new_tube(e):
        path, props = e.args[0][0]
        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE

    def new_text(e):
        path, props = e.args[0][0]
        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT

    # tube and text is created
    text_event, tube_event = q.expect_many(EventPattern('dbus-signal', signal='NewChannels',
                                                        predicate=new_text),
                                           EventPattern('dbus-signal', signal='NewChannels',
                                                        predicate=new_tube))

    channels = e.args[0]
    tube_path, props = tube_event.args[0][0]
    assertEquals(cs.CHANNEL_TYPE_DBUS_TUBE, props[cs.CHANNEL_TYPE])
    assertEquals('chat2@conf.localhost/test', props[cs.INITIATOR_ID])
    assertEquals(False, props[cs.REQUESTED])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals('com.example.TestCase', props[cs.DBUS_TUBE_SERVICE_NAME])

    _, props = text_event.args[0][0]
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(True, props[cs.REQUESTED])

    # tube is local-pending
    tube_chan = bus.get_object(conn.bus_name, tube_path)
    state = tube_chan.Get(cs.CHANNEL_IFACE_TUBE, 'State',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals(cs.TUBE_STATE_LOCAL_PENDING, state)

if __name__ == '__main__':
    # We can't use t.exec_dbus_tube_test() as we can use only the muc bytestream
    exec_test(lambda q, bus, conn, stream:
        test(q, bus, conn, stream, cs.SOCKET_ACCESS_CONTROL_CREDENTIALS))
    exec_test(lambda q, bus, conn, stream:
        test(q, bus, conn, stream, cs.SOCKET_ACCESS_CONTROL_LOCALHOST))
