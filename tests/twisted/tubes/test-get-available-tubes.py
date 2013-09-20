"""Test GetAvailableStreamTubeTypes and GetAvailableTubeTypes"""

import os
import sys

import dbus

from servicetest import call_async, EventPattern, tp_name_prefix,\
    assertContains, assertEquals, assertLength
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
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, iq_event.stanza)

    # muc stream tube
    call_async(q, conn.Requests, 'CreateChannel', {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: 'chat@conf.localhost',
        cs.STREAM_TUBE_SERVICE: 'test'})

    q.expect('stream-presence', to='chat@conf.localhost/test')

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    _, event = q.expect_many(
            EventPattern('dbus-signal', signal='MembersChangedDetailed',
                predicate=lambda e:
                    len(e.args[0]) == 2 and     # added
                    e.args[1] == [] and         # removed
                    e.args[2] == [] and         # local pending
                    e.args[3] == [] and         # remote pending
                    e.args[4].get('actor', 0) == 0 and
                    e.args[4].get('change-reason', 0) == 0 and
                    set([e.args[4]['contact-ids'][h] for h in e.args[0]]) ==
                    set(['chat@conf.localhost/test', 'chat@conf.localhost/bob'])),
        EventPattern('dbus-return', method='CreateChannel'))

    path = event.value[0]
    props = event.value[1]

    tube = bus.get_object(conn.bus_name, path)
    stream_tubes_types = tube.Get(cs.CHANNEL_TYPE_STREAM_TUBE, 'SupportedSocketTypes',
        dbus_interface=cs.PROPERTIES_IFACE)

    if os.name == 'posix':
        assert cs.SOCKET_ACCESS_CONTROL_LOCALHOST in \
            stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_UNIX], \
            stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_UNIX]
        # so far we only guarantee to support credentials-passing on Linux
        if sys.platform == 'linux2':
            assert cs.SOCKET_ACCESS_CONTROL_CREDENTIALS in \
                stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_UNIX], \
                stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_UNIX]

    assertEquals([cs.SOCKET_ACCESS_CONTROL_LOCALHOST, cs.SOCKET_ACCESS_CONTROL_PORT],
        stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_IPV4])
    assertEquals([cs.SOCKET_ACCESS_CONTROL_LOCALHOST, cs.SOCKET_ACCESS_CONTROL_PORT],
        stream_tubes_types[cs.SOCKET_ADDRESS_TYPE_IPV6])

if __name__ == '__main__':
    exec_test(test)
