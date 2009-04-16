"""
Test various ways in which connections can fail.
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream

import dbus

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

if __name__ == '__main__':
    exec_test(test_network_error, {'port': dbus.UInt32(4243)})
    exec_test(test_conflict_after_connect)
