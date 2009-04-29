"""Test GetAvailableStreamTubeTypes and GetAvailableTubeTypes"""

import dbus

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import (
    exec_test, make_result_iq, acknowledge_iq, make_muc_presence)
import constants as cs

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    call_async(q, conn, 'RequestHandles', cs.HT_ROOM, ['chat@conf.localhost'])

    event = q.expect('stream-iq', to='conf.localhost',
            query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]

    # request tubes channel
    call_async(q, conn, 'RequestChannel',
        tp_name_prefix + '.Channel.Type.Tubes', cs.HT_ROOM, handles[0], True)

    _, stream_event = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    new_chans, members, event = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [2, 3], [], [], [], 0, 0]),
        EventPattern('dbus-return', method='RequestChannel'))

    channels = new_chans.args[0]
    assert len(channels) == 2

    assert conn.InspectHandles(cs.HT_CONTACT, [2]) == ['chat@conf.localhost/test']
    assert conn.InspectHandles(cs.HT_CONTACT, [3]) == ['chat@conf.localhost/bob']
    bob_handle = 3

    tubes_chan = bus.get_object(conn.bus_name, event.value[0])
    tubes_iface_muc = dbus.Interface(tubes_chan,
            tp_name_prefix + '.Channel.Type.Tubes')

    # FIXME: these using a "1-1" tubes channel too

    # test GetAvailableTubeTypes
    tube_types = tubes_iface_muc.GetAvailableTubeTypes()

    assert len(tube_types) == 2
    assert cs.TUBE_TYPE_DBUS in tube_types # D-Bus tube
    assert cs.TUBE_TYPE_STREAM in tube_types # Stream tube

    # test GetAvailableStreamTubeTypes
    stream_tubes_types = tubes_iface_muc.GetAvailableStreamTubeTypes()
    assert len(stream_tubes_types) == 3
    assert stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_UNIX] == [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]
    assert stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_IPV4] == [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]
    assert stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_IPV6] == [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]

    # OK, we're done
    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(test)
