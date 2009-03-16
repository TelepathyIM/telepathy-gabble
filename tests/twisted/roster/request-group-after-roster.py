"""
Regression test for a bug where CreateChannel times out when requesting a group
channel after the roster has been received.
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

    call_async(q, conn.Requests, 'CreateChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.ContactList',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_GROUP,
              'org.freedesktop.Telepathy.Channel.TargetHandle': test_handle,
              })

    event = q.expect('dbus-return', method='CreateChannel')
    ret_path, ret_props = event.value

    event = q.expect('dbus-signal', signal='NewChannels')
    path, props = event.args[0][0]
    assert props['org.freedesktop.Telepathy.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.ContactList', props
    assert props['org.freedesktop.Telepathy.Channel.TargetHandleType'] ==\
            HT_GROUP, props
    assert props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            test_handle, props
    assert props['org.freedesktop.Telepathy.Channel.TargetID'] ==\
            'test', props

    assert ret_path == path, (ret_path, path)
    assert ret_props == props, (ret_props, props)

if __name__ == '__main__':
    exec_test(test)
