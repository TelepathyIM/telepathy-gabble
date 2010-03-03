
"""
Test connecting to a server with 2 accounts, testing XmppAuthenticator and
JabberAuthenticator
"""

import os
import sys
import dbus
import servicetest

from gabbletest import exec_test
import constants as cs

def test(q, bus, conns, streams):

    conn1, conn2 = conns
    stream1, stream2 = streams

    # Connection 1
    conn1.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED],
             path=conn1.object.object_path)
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
             args=[{1L: (0L, {u'available': {}})}],
             path=conn1.object.object_path)
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED],
             path=conn1.object.object_path)
    for target_id in ('publish', 'subscribe', 'stored'):
        event = q.expect('dbus-signal', signal='NewChannels',
                         path=conn1.object.object_path)
        channels = event.args[0]
        assert len(channels) == 1
        path, props = channels[0]
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_CONTACT_LIST, props
        assert props[cs.TARGET_ID] == target_id, props

    # Connection 2
    conn2.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED],
             path=conn2.object.object_path)
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
             args=[{1L: (0L, {u'available': {}})}],
             path=conn2.object.object_path)
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED],
             path=conn2.object.object_path)

    for target_id in ('publish', 'subscribe', 'stored'):
        event = q.expect('dbus-signal', signal='NewChannels',
                         path=conn2.object.object_path)
        channels = event.args[0]
        assert len(channels) == 1
        path, props = channels[0]
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_CONTACT_LIST, props
        assert props[cs.TARGET_ID] == target_id, props

if __name__ == '__main__':
    exec_test(test, num_instances=2)
