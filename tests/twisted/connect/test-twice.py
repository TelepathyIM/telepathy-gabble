"""
Test connecting to a server with 2 accounts, testing XmppAuthenticator and
JabberAuthenticator
"""

import os
import sys
import dbus

import constants as cs
from gabbletest import exec_test
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import assertLength

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

    pairs = expect_contact_list_signals(q, bus, conn1,
            ['publish', 'subscribe', 'stored'])

    check_contact_list_signals(q, bus, conn1, pairs.pop(0), cs.HT_LIST,
            'publish', [])
    check_contact_list_signals(q, bus, conn1, pairs.pop(0), cs.HT_LIST,
            'subscribe', [])
    check_contact_list_signals(q, bus, conn1, pairs.pop(0), cs.HT_LIST,
            'stored', [])
    assertLength(0, pairs)      # i.e. we popped and checked all of them

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

    pairs = expect_contact_list_signals(q, bus, conn2,
            ['publish', 'subscribe', 'stored'])

    check_contact_list_signals(q, bus, conn2, pairs.pop(0), cs.HT_LIST,
            'publish', [])
    check_contact_list_signals(q, bus, conn2, pairs.pop(0), cs.HT_LIST,
            'subscribe', [])
    check_contact_list_signals(q, bus, conn2, pairs.pop(0), cs.HT_LIST,
            'stored', [])
    assertLength(0, pairs)      # i.e. we popped and checked all of them

if __name__ == '__main__':
    exec_test(test, num_instances=2, do_connect=False)
