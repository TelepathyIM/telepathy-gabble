"""
Test connecting to a server with 2 accounts, testing XmppAuthenticator and
JabberAuthenticator
"""

import os
import sys
import dbus

import constants as cs
from gabbletest import exec_test

def test(q, bus, conns, streams):

    conn1, conn2 = conns
    stream1, stream2 = streams

    # Connection 1
    conn1.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED],
             path=conn1.object.object_path)
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresencesChanged',
             args=[{1L: (cs.PRESENCE_AVAILABLE, 'available', '')}],
             path=conn1.object.object_path)
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED],
             path=conn1.object.object_path)

    q.expect('dbus-signal', signal='ContactListStateChanged',
             args=[cs.CONTACT_LIST_STATE_SUCCESS], path=conn1.object.object_path)

    # Connection 2
    conn2.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED],
             path=conn2.object.object_path)
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresencesChanged',
             args=[{1L: (cs.PRESENCE_AVAILABLE, 'available', '')}],
             path=conn2.object.object_path)
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED],
             path=conn2.object.object_path)

    q.expect('dbus-signal', signal='ContactListStateChanged',
             args=[cs.CONTACT_LIST_STATE_SUCCESS], path=conn2.object.object_path)

if __name__ == '__main__':
    exec_test(test, num_instances=2, do_connect=False)
