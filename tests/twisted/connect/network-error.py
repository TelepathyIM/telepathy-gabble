
"""
Connection is disconnected because server closes its TCP stream abruptly.
"""

from gabbletest import exec_test
from servicetest import EventPattern
import constants as cs
import sys

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
             args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # server closes its stream
    stream.transport.loseConnection()

    # Gabble disconnect and close its connection
    q.expect('dbus-signal',
             signal='NameOwnerChanged',
             predicate=lambda e: cs.CONN + '.gabble.jabber' in str(e.args[0])
                                 and str(e.args[1]) != ''
                                 and str(e.args[2]) == '')

if __name__ == '__main__':
    exec_test(test)

