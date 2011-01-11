"""
Disconnect the connection but the server doesn't send its stream close stanza.
After a while Gabble gives up, force the closing and the Disconnect
D-Bus call returns.
"""

from gabbletest import exec_test
from servicetest import call_async, EventPattern
import constants as cs

def test(q, bus, conn, stream):
    call_async(q, conn, 'Disconnect')

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-closed'))

    q.expect('dbus-return', method='Disconnect')


if __name__ == '__main__':
    # Gabble will time out after 5 seconds
    exec_test(test, timeout=10)
