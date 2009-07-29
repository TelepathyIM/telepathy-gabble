"""
Regression test for a bug where CreateChannel times out when requesting a group
channel after the roster has been received.
"""

from gabbletest import exec_test, sync_stream
from servicetest import sync_dbus, call_async
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    roster_event = q.expect('stream-iq', query_ns=ns.ROSTER)
    roster_event.stanza['type'] = 'result'

    call_async(q, conn, "RequestHandles", cs.HT_GROUP, ['test'])

    event = q.expect('dbus-return', method='RequestHandles')
    test_handle = event.value[0][0]

    # send an empty roster
    stream.send(roster_event.stanza)

    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
              cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
              cs.TARGET_HANDLE: test_handle,
              })

    event = q.expect('dbus-return', method='CreateChannel')
    ret_path, ret_props = event.value

    event = q.expect('dbus-signal', signal='NewChannels')
    path, props = event.args[0][0]
    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_CONTACT_LIST, props
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_GROUP, props
    assert props[cs.TARGET_HANDLE] == test_handle, props
    assert props[cs.TARGET_ID] == 'test', props

    assert ret_path == path, (ret_path, path)
    assert ret_props == props, (ret_props, props)

if __name__ == '__main__':
    exec_test(test)
