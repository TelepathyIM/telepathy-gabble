
"""
Test connecting to a server with 2 accounts, testing XmppAuthenticator and
JabberAuthenticator
"""

import os
import sys
import dbus
import servicetest

from gabbletest import (
    make_connection, make_stream, JabberAuthenticator, XmppAuthenticator,
    XmppXmlStream, JabberXmlStream, disconnect_conn)
import constants as cs

def test(q, bus, conn1, conn2, stream1, stream2):
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

    # Disconnection 1
    disconnect_conn(q, conn1, stream1)

    # Disconnection 2
    disconnect_conn(q, conn2, stream2)

if __name__ == '__main__':
    queue = servicetest.IteratingEventQueue(None)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()

    params = {
        'account': 'test1@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        }
    conn1 = make_connection(bus, queue.append, params)
    authenticator = JabberAuthenticator('test1', 'pass',
        resource=params['resource'])
    stream1, port1 = make_stream(queue.append, authenticator, protocol=JabberXmlStream,
                          port=4242)

    params = {
        'account': 'test2@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4343),
        }
    conn2 = make_connection(bus, queue.append, params)
    authenticator = XmppAuthenticator('test2', 'pass',
        resource=params['resource'])
    stream2, port2 = make_stream(queue.append, authenticator, protocol=XmppXmlStream,
                          port=4343)

    test(queue, bus, conn1, conn2, stream1, stream2)
