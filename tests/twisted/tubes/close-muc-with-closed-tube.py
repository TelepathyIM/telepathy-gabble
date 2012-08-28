"""Test IBB stream tube support in the context of a MUC."""

import dbus

from servicetest import call_async, EventPattern, unwrap, assertEquals
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
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, iq_event.stanza)

    call_async(q, conn, 'RequestHandles', cs.HT_ROOM, ['chat@conf.localhost'])

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

    def new_chan_predicate(e):
        path, props = e.args[0][0]
        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE

    # tube channel is automatically created
    event = q.expect('dbus-signal', signal='NewChannels',
                     predicate=new_chan_predicate)

    path, props = event.args[0][0]

    assertEquals(cs.CHANNEL_TYPE_DBUS_TUBE, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(room_handle, props[cs.TARGET_HANDLE])
    assertEquals('chat@conf.localhost', props[cs.TARGET_ID])
    assertEquals(False, props[cs.REQUESTED])

    tube_chan = bus.get_object(conn.bus_name, path)
    tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)

    # reject the tube
    tube_iface.Close(dbus_interface=cs.CHANNEL)
    q.expect('dbus-signal', signal='ChannelClosed')

    # close the text channel
    text_chan.Close()

if __name__ == '__main__':
    exec_test(test)
