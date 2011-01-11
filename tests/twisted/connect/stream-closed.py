
"""
Connection is disconnected because server closes its XMPP stream.
"""

from gabbletest import exec_test
from servicetest import EventPattern
import constants as cs

def test(q, bus, conn, stream):
    # server closes its stream
    stream.sendFooter()

    # Gabble disconnect and close its connection
    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR]),
        EventPattern('stream-closed'))


if __name__ == '__main__':
    exec_test(test)

