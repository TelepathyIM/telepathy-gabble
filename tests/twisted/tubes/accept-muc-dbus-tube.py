import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq, make_muc_presence
import constants as c

from twisted.words.xish import xpath
import ns

from mucutil import join_muc_and_check

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
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

    assert props[c.CHANNEL_TYPE] == c.CHANNEL_TYPE_DBUS_TUBE
    assert props[c.INITIATOR_ID] == 'chat@conf.localhost/bob'
    bob_handle = props[c.INITIATOR_HANDLE]
    assert props[c.INTERFACES] == [c.CHANNEL_IFACE_GROUP, c.CHANNEL_IFACE_TUBE]
    assert props[c.REQUESTED] == False
    assert props[c.TARGET_ID] == 'chat@conf.localhost'
    assert props[c.DBUS_TUBE_SERVICE_NAME] == 'com.example.Test'
    assert props[c.TUBE_PARAMETERS] == {'foo': 'bar'}

    tube_chan = bus.get_object(conn.bus_name, path)
    tube_iface = dbus.Interface(tube_chan, c.CHANNEL_IFACE_TUBE)
    dbus_tube_iface = dbus.Interface(tube_chan, c.CHANNEL_TYPE_DBUS_TUBE)
    tube_chan_iface = dbus.Interface(tube_chan, c.CHANNEL)

    # only Bob is in DBusNames
    dbus_names = tube_chan.Get(c.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames', dbus_interface=c.PROPERTIES_IFACE)
    assert dbus_names == {bob_handle: bob_bus_name}

    call_async(q, dbus_tube_iface, 'Accept')

    return_event, names_changed, presence_event = q.expect_many(
        EventPattern('dbus-return', method='Accept'),
        EventPattern('dbus-signal', signal='DBusNamesChanged', interface=c.CHANNEL_TYPE_DBUS_TUBE),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))

    tube_addr = return_event.value[0]
    assert len(tube_addr) > 0

    # check presence stanza
    tube_node = xpath.queryForNodes('/presence/tubes/tube', presence_event.stanza)[0]
    assert tube_node['initiator'] == 'chat@conf.localhost/bob'
    assert tube_node['service'] == 'com.example.Test'
    assert tube_node['stream-id'] == '10'
    assert tube_node['type'] == 'dbus'
    assert tube_node['id'] == '1'
    self_bus_name = tube_node['dbus-name']

    tubes_self_handle = tube_chan.GetSelfHandle(dbus_interface=c.CHANNEL_IFACE_GROUP)
    assert tubes_self_handle != 0

    # both of us are in DBusNames now
    dbus_names = tube_chan.Get(c.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames', dbus_interface=c.PROPERTIES_IFACE)
    assert dbus_names == {bob_handle: bob_bus_name, tubes_self_handle: self_bus_name}

    added, removed = names_changed.args
    assert added == {tubes_self_handle: self_bus_name}
    assert removed == []

    tube_chan_iface.Close()
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    # OK, we're done
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
