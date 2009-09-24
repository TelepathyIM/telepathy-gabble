"""
Test that Gabble times out the connection process after a while if the server
stops responding at various points. Real Gabbles time out after a minute; the
test suite's Gabble times out after a couple of seconds.
"""

from servicetest import assertEquals
from gabbletest import exec_test, XmppAuthenticator

import constants as cs
import ns

class NoStreamHeader(XmppAuthenticator):
    def __init__(self):
        XmppAuthenticator.__init__(self, 'test', 'pass')

    def streamStarted(self, root=None):
        return

class NoAuthInfoResult(XmppAuthenticator):
    def __init__(self):
        XmppAuthenticator.__init__(self, 'test', 'pass')

    def auth(self, auth):
        return

class NoAuthResult(XmppAuthenticator):
    def __init__(self):
        XmppAuthenticator.__init__(self, 'test', 'pass')

    def bindIq(self, iq):
        return

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    e = q.expect('dbus-signal', signal='StatusChanged')
    status, reason = e.args
    assertEquals(cs.CONN_STATUS_DISCONNECTED, status)
    assertEquals(cs.CSR_NETWORK_ERROR, reason)

if __name__ == '__main__':
    exec_test(test, authenticator=NoStreamHeader())
    exec_test(test, authenticator=NoAuthInfoResult())
    exec_test(test, authenticator=NoAuthResult())
