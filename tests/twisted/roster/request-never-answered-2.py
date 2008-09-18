"""
Exhibit a bug where RequestChannel times out when requesting a group channel
after the roster has been received.
"""

import dbus

from gabbletest import exec_test, sync_stream
from servicetest import sync_dbus, call_async

HT_CONTACT_LIST = 3
HT_GROUP = 4

def test(q, bus, conn, stream):
    conn.Connect()
    # q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    roster_event = q.expect('stream-iq', query_ns='jabber:iq:roster')
    roster_event.stanza['type'] = 'result'

    call_async(q, conn, "RequestHandles", HT_GROUP, ['test'])

    event = q.expect('dbus-return', method='RequestHandles')
    test_handle = event.value[0][0]

    # send an empty roster
    stream.send(roster_event.stanza)

    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.ContactList', HT_GROUP,
        test_handle, True)

    event = q.expect('dbus-signal', signal='NewChannel')
    path, type, handle_type, handle, suppress_handler = event.args
    assert handle_type == HT_GROUP, handle_type
    assert handle == test_handle, (handle, test_handle)

    event = q.expect('dbus-return', method='RequestChannel')
    assert event.value[0] == path, (event.args[0], path)

if __name__ == '__main__':
    exec_test(test)
