"""
Test ensuring roster channels
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

    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')
    call_async(q, requestotron, 'EnsureChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.ContactList',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_GROUP,
              'org.freedesktop.Telepathy.Channel.TargetHandle': test_handle,
              })
    call_async(q, requestotron, 'EnsureChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.ContactList',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_GROUP,
              'org.freedesktop.Telepathy.Channel.TargetHandle': test_handle,
              })

    while True:
        sig = q.expect('dbus-signal', signal='NewChannels')
        sig_path, sig_props = sig.args[0][0]
        if sig_props['org.freedesktop.Telepathy.Channel.TargetHandleType'] ==\
            HT_GROUP and\
           sig_props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            test_handle:
            break
    ret = q.expect('dbus-return', method='EnsureChannel')
    ret2 = q.expect('dbus-return', method='EnsureChannel')

    yours, path, props = ret.value
    yours2, path2, props2 = ret2.value

    assert props['org.freedesktop.Telepathy.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.ContactList', props
    assert props['org.freedesktop.Telepathy.Channel.TargetHandleType'] ==\
            HT_GROUP, props
    assert props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            test_handle, props
    assert props['org.freedesktop.Telepathy.Channel.TargetID'] ==\
            'test', props

    assert yours != yours2, (yours, yours2)
    assert path == path2, (path, path2)
    assert props == props2, (props, props2)

    assert sig_props == props, (sig_props, props)


if __name__ == '__main__':
    exec_test(test)
