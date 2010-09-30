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

    # FIXME: this is G_IO_ERROR_FAILED, which we can't really map to anything
    # better than NetworkError. The debug message says "Connection refused",
    # so something, somewhere, ought to be able to do better, and give us
    # enough information to produce cs.CONNECTION_REFUSED.
    new = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(cs.NETWORK_ERROR, new.args[0])

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR])

def test_conflict_after_connect(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    stream.send_stream_error('conflict')

    new = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(cs.CONNECTION_REPLACED, new.args[0])

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NAME_IN_USE])

class StreamErrorAuthenticator(xmlstream.Authenticator):
    def __init__(self, stream_error):
        xmlstream.Authenticator.__init__(self)
        self.__stream_error = stream_error

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        no = domish.Element((xmlstream.NS_STREAMS, 'error'))
        no.addElement((ns.STREAMS, self.__stream_error))
        self.xmlstream.send(no)

def test_stream_conflict_during_connect(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    new = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(cs.ALREADY_CONNECTED, new.args[0])

    old = q.expect('dbus-signal', signal='StatusChanged')
    status, reason = old.args
    assertEquals(cs.CONN_STATUS_DISCONNECTED, status)
    assertEquals(cs.CSR_NAME_IN_USE, reason)

def test_host_unknown(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    new = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(cs.AUTHENTICATION_FAILED, new.args[0])

    old = q.expect('dbus-signal', signal='StatusChanged')
    status, reason = old.args
    assertEquals(cs.CONN_STATUS_DISCONNECTED, status)
    assertEquals(cs.CSR_AUTHENTICATION_FAILED, reason)

if __name__ == '__main__':
    exec_test(test_network_error, {'port': dbus.UInt32(4243)})
    exec_test(test_conflict_after_connect)
    exec_test(test_stream_conflict_during_connect,
            authenticator=StreamErrorAuthenticator('conflict'))
    exec_test(test_host_unknown, {'server': 'localhost',
                     'account': 'test@example.org',
                    }, authenticator=StreamErrorAuthenticator('host-unknown'))
