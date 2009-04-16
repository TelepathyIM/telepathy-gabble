"""
Test various ways in which connections can fail.
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream

import dbus

from servicetest import assertEquals
from gabbletest import exec_test
import constants as cs
import ns

def test_network_error(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR])

def test_conflict_after_connect(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    go_away = domish.Element((xmlstream.NS_STREAMS, 'error'))
    go_away.addElement((ns.STREAMS, 'conflict'))
    stream.send(go_away)

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NAME_IN_USE])

class HostUnknownAuthenticator(xmlstream.Authenticator):
    def __init__(self):
        xmlstream.Authenticator.__init__(self)

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        no = domish.Element((xmlstream.NS_STREAMS, 'error'))
        no.addElement((ns.STREAMS, 'host-unknown'))
        self.xmlstream.send(no)

def test_host_unknown(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    e = q.expect('dbus-signal', signal='StatusChanged')
    status, reason = e.args
    assertEquals(cs.CONN_STATUS_DISCONNECTED, status)
    assertEquals(cs.CSR_AUTHENTICATION_FAILED, reason)

if __name__ == '__main__':
    exec_test(test_network_error, {'port': dbus.UInt32(4243)})
    exec_test(test_conflict_after_connect)
    exec_test(test_host_unknown, {'server': 'localhost',
                     'account': 'test@example.org',
                    }, authenticator=HostUnknownAuthenticator())
