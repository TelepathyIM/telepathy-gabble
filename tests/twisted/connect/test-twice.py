
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
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Connection 2
    conn2.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])


if __name__ == '__main__':
    exec_test(test, num_instances=2)
