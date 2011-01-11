
"""
Connection is disconnected because server closes its TCP stream abruptly.
"""

from gabbletest import exec_test
from servicetest import EventPattern
import constants as cs
import sys

def test(q, bus, conn, stream):
    # server closes its stream
    stream.transport.loseConnection()

    # Gabble disconnect and close its connection
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NONE_SPECIFIED])

    q.expect('dbus-signal',
             signal='NameOwnerChanged',
             predicate=lambda e: cs.CONN + '.gabble.jabber' in str(e.args[0])
                                 and str(e.args[1]) != ''
                                 and str(e.args[2]) == '')

if __name__ == '__main__':
    exec_test(test)

