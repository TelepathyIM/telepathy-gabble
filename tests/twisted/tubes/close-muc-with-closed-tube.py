"""Test IBB stream tube support in the context of a MUC."""

import dbus

from servicetest import call_async, EventPattern, unwrap
from gabbletest import exec_test, make_result_iq, acknowledge_iq, make_muc_presence
import constants as cs
import ns
import tubetestutil as t

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
    room_handle = handles[0]

    # join the muc
    call_async(q, conn, 'RequestChannel',
        cs.CHANNEL_TYPE_TEXT, cs.HT_ROOM, room_handle, True)

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

    assert conn.InspectHandles(cs.HT_CONTACT, [2, 3]) == \
        ['chat@conf.localhost/test', 'chat@conf.localhost/bob']
    bob_handle = 3

    event = q.expect('dbus-return', method='RequestChannel')
    text_chan = bus.get_object(conn.bus_name, event.value[0])

    # Bob offers a muc tube
    tube_id = 666
    stream_id = 1234
    presence = make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob')
    tubes = presence.addElement((ns.TUBES, 'tubes'))
    tube = tubes.addElement((None, 'tube'))
    tube['type'] = 'dbus'
    tube['service'] = 'org.telepathy.freedesktop.test'
    tube['id'] = str(tube_id)
    tube['stream-id'] = str(stream_id)
    tube['dbus-name'] = ':2.Y2Fzc2lkeS10ZXN0MgAA'
    tube['initiator'] = 'chat@conf.localhost/bob'
    parameters = tube.addElement((None, 'parameters'))
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 's'
    parameter['type'] = 'str'
    parameter.addContent('hello')
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 'ay'
    parameter['type'] = 'bytes'
    parameter.addContent('aGVsbG8=')
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 'u'
    parameter['type'] = 'uint'
    parameter.addContent('123')
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 'i'
    parameter['type'] = 'int'
    parameter.addContent('-123')

    stream.send(presence)

    # tubes channel is automatically created
    event = q.expect('dbus-signal', signal='NewChannel')

    if event.args[1] == cs.CHANNEL_TYPE_TEXT:
        # skip this one, try the next one
        event = q.expect('dbus-signal', signal='NewChannel')

    assert event.args[1] == cs.CHANNEL_TYPE_TUBES, event.args
    assert event.args[2] == cs.HT_ROOM
    assert event.args[3] == room_handle

    tubes_chan = bus.get_object(conn.bus_name, event.args[0])
    tubes_iface = dbus.Interface(tubes_chan, event.args[1])

    channel_props = tubes_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == 'chat@conf.localhost', channel_props
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == ''
    assert channel_props['InitiatorHandle'] == 0

    tubes_self_handle = tubes_chan.GetSelfHandle(
        dbus_interface=cs.CHANNEL_IFACE_GROUP)

    q.expect('dbus-signal', signal='NewTube',
        args=[tube_id, bob_handle, 0, 'org.telepathy.freedesktop.test', sample_parameters, 0])

    expected_tube = (tube_id, bob_handle, cs.TUBE_TYPE_DBUS,
        'org.telepathy.freedesktop.test', sample_parameters,
        cs.TUBE_STATE_LOCAL_PENDING)
    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 1, unwrap(tubes)
    t.check_tube_in_tubes(expected_tube, tubes)

    # reject the tube
    tubes_iface.CloseTube(tube_id)
    q.expect('dbus-signal', signal='TubeClosed', args=[tube_id])

    # close the text channel
    text_chan.Close()

if __name__ == '__main__':
    exec_test(test)
