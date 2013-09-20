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

    # join the muc
    call_async(q, conn.Requests, 'CreateChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: 'chat@conf.localhost'})

    q.expect_many(
        EventPattern('dbus-signal', signal='MembersChangedDetailed',
            predicate=lambda e:
                e.args[0] == [] and         # added
                e.args[1] == [] and         # removed
                e.args[2] == [] and         # local pending
                len(e.args[3]) == 1 and     # remote pending
                e.args[4].get('actor', 0) == 0 and
                e.args[4].get('change-reason', 0) == 0 and
                e.args[4]['contact-ids'][e.args[3][0]] == 'chat@conf.localhost/test'),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', 'chat@conf.localhost', 'test'))

    event = q.expect('dbus-signal', signal='MembersChangedDetailed',
            predicate=lambda e:
                len(e.args[0]) == 2 and     # added
                e.args[1] == [] and         # removed
                e.args[2] == [] and         # local pending
                e.args[3] == [] and         # remote pending
                e.args[4].get('actor', 0) == 0 and
                e.args[4].get('change-reason', 0) == 0 and
                set([e.args[4]['contact-ids'][h] for h in e.args[0]]) ==
                set(['chat@conf.localhost/test', 'chat@conf.localhost/bob']))

    for h in event.args[0]:
        if event.args[4]['contact-ids'][h] == 'chat@conf.localhost/bob':
            bob_handle = h

    event = q.expect('dbus-return', method='CreateChannel')
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
