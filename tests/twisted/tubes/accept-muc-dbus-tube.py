import dbus

from servicetest import assertEquals, assertNotEquals, call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq, make_muc_presence
import constants as cs

from twisted.words.xish import xpath
import ns

from mucutil import join_muc_and_check

def test(q, bus, conn, stream, access_control):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    muc = 'chat@conf.localhost'
    _, _, test_handle, bob_handle = \
        join_muc_and_check(q, bus, conn, stream, muc)

    # Bob offers a stream tube
    bob_bus_name = ':2.Ym9i'
    presence = make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob')
    tubes = presence.addElement((ns.TUBES, 'tubes'))
    tube = tubes.addElement((None, 'tube'))
    tube['type'] = 'dbus'
    tube['initiator'] = 'chat@conf.localhost/bob'
    tube['stream-id'] = '10'
    tube['id'] = '1'
    tube['service'] = 'com.example.Test'
    tube['dbus-name'] = bob_bus_name
    parameters = tube.addElement((None, 'parameters'))
    parameter = parameters.addElement((None, 'parameter'))
    parameter['type'] = 'str'
    parameter['name'] = 'foo'
    parameter.addContent('bar')
    stream.send(presence)

    # tubes channel is created
    event = q.expect('dbus-signal', signal='NewChannels')
    channels = event.args[0]
    path, props = channels[0]

    # tube channel is created
    event = q.expect('dbus-signal', signal='NewChannels')
    channels = event.args[0]
    path, props = channels[0]

    assertEquals(cs.CHANNEL_TYPE_DBUS_TUBE, props[cs.CHANNEL_TYPE])
    assertEquals('chat@conf.localhost/bob', props[cs.INITIATOR_ID])
    bob_handle = props[cs.INITIATOR_HANDLE]
    assertEquals([cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_TUBE],
        props[cs.INTERFACES])
    assertEquals(False, props[cs.REQUESTED])
    assertEquals('chat@conf.localhost', props[cs.TARGET_ID])
    assertEquals('com.example.Test', props[cs.DBUS_TUBE_SERVICE_NAME])
    assertEquals({'foo': 'bar'}, props[cs.TUBE_PARAMETERS])
    assertEquals([cs.SOCKET_ACCESS_CONTROL_CREDENTIALS,
                    cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
        props[cs.DBUS_TUBE_SUPPORTED_ACCESS_CONTROLS])

    tube_chan = bus.get_object(conn.bus_name, path)
    tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_IFACE_TUBE)
    dbus_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)
    tube_chan_iface = dbus.Interface(tube_chan, cs.CHANNEL)

    # only Bob is in DBusNames
    dbus_names = tube_chan.Get(cs.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames', dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals({bob_handle: bob_bus_name}, dbus_names)

    call_async(q, dbus_tube_iface, 'Accept', access_control)

    return_event, names_changed1, names_changed2, presence_event = q.expect_many(
        EventPattern('dbus-return', method='Accept'),
        EventPattern('dbus-signal', signal='DBusNamesChanged', interface=cs.CHANNEL_TYPE_DBUS_TUBE),
        EventPattern('dbus-signal', signal='DBusNamesChanged', interface=cs.CHANNEL_TYPE_DBUS_TUBE),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))

    tube_addr = return_event.value[0]
    assert len(tube_addr) > 0

    # check presence stanza
    tube_node = xpath.queryForNodes('/presence/tubes/tube', presence_event.stanza)[0]
    assertEquals('chat@conf.localhost/bob', tube_node['initiator'])
    assertEquals('com.example.Test', tube_node['service'])
    assertEquals('10', tube_node['stream-id'])
    assertEquals('dbus', tube_node['type'])
    assertEquals('1', tube_node['id'])
    self_bus_name = tube_node['dbus-name']

    tubes_self_handle = tube_chan.GetSelfHandle(dbus_interface=cs.CHANNEL_IFACE_GROUP)
    assertNotEquals(0, tubes_self_handle)

    # both of us are in DBusNames now
    dbus_names = tube_chan.Get(cs.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames', dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals({bob_handle: bob_bus_name, tubes_self_handle: self_bus_name}, dbus_names)

    added, removed = names_changed1.args
    assertEquals({bob_handle: bob_bus_name}, added)
    assertEquals([], removed)

    added, removed = names_changed2.args
    assertEquals({tubes_self_handle: self_bus_name}, added)
    assertEquals([], removed)

    tube_chan_iface.Close()
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

if __name__ == '__main__':
    # We can't use t.exec_dbus_tube_test() as we can use only the muc bytestream
    exec_test(lambda q, bus, conn, stream:
        test(q, bus, conn, stream, cs.SOCKET_ACCESS_CONTROL_CREDENTIALS))
    exec_test(lambda q, bus, conn, stream:
        test(q, bus, conn, stream, cs.SOCKET_ACCESS_CONTROL_LOCALHOST))
