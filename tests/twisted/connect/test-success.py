
"""
Test connecting to a server.
"""

from gabbletest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(test)

