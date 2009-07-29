"""
Test ensuring roster channels
"""

from gabbletest import exec_test
from servicetest import call_async
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

    call_async(q, conn.Requests, 'EnsureChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
              cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
              cs.TARGET_HANDLE: test_handle,
              })
    call_async(q, conn.Requests, 'EnsureChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
              cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
              cs.TARGET_HANDLE: test_handle,
              })

    ret = q.expect('dbus-return', method='EnsureChannel')
    ret2 = q.expect('dbus-return', method='EnsureChannel')

    # We don't test the NewChannels signal here - depending on exact timing,
    # it might happen between the two EnsureChannel calls, or after the second
    # one.

    yours, path, props = ret.value
    yours2, path2, props2 = ret2.value

    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_CONTACT_LIST, props
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_GROUP, props
    assert props[cs.TARGET_HANDLE] == test_handle, props
    assert props[cs.TARGET_ID] == 'test', props

    assert yours != yours2, (yours, yours2)
    assert path == path2, (path, path2)
    assert props == props2, (props, props2)

if __name__ == '__main__':
    exec_test(test)
