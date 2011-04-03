
"""
Test connecting to a server with 2 accounts. Check one account does not block
the second account.
"""

import os
import sys
import dbus
import servicetest

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream
import twisted.internet.protocol
from twisted.internet import reactor

from servicetest import (Event, unwrap)

from gabbletest import (
    make_connection, make_stream, XmppAuthenticator, XmppXmlStream,
    disconnect_conn, GabbleAuthenticator)
import constants as cs

NS_XMPP_TLS = 'urn:ietf:params:xml:ns:xmpp-tls'
NS_XMPP_SASL = 'urn:ietf:params:xml:ns:xmpp-sasl'

class BlockForeverTlsAuthenticator(GabbleAuthenticator):
    """A TLS stream authenticator that is deliberately broken. It sends
    <proceed/> to the client but then do nothing, so the TLS handshake will
    not work. Useful for testing regression of bug #14341."""

    def __init__(self, username, password):
        GabbleAuthenticator.__init__(self, username, password)
        self.username = username
        self.password = password
        self.authenticated = False

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        features = domish.Element((xmlstream.NS_STREAMS, 'features'))
        mechanisms = features.addElement((NS_XMPP_SASL, 'mechanisms'))
        mechanism = mechanisms.addElement('mechanism', content='DIGEST-MD5')
        starttls = features.addElement((NS_XMPP_TLS, 'starttls'))
        starttls.addElement('required')
        self.xmlstream.send(features)

        self.xmlstream.addOnetimeObserver("/starttls", self.auth)

    def auth(self, auth):
        proceed = domish.Element((NS_XMPP_TLS, 'proceed'))
        self.xmlstream.send(proceed)

        return; # auth blocks

        self.xmlstream.reset()
        self.authenticated = True


def test(q, bus, conn1, conn2, stream1, stream2):
    # Connection 1
    conn1.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    # Connection 1 blocks because the fake jabber server behind conn1 does not
    # proceed to the tls handshake. The second connection is independant and
    # should work.

    # Connection 2
    conn2.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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
    conn1, jid1 = make_connection(bus, queue.append, params)
    authenticator = BlockForeverTlsAuthenticator('test1', 'pass')
    stream1 = make_stream(queue.append, authenticator, protocol=XmppXmlStream)

    factory = twisted.internet.protocol.Factory()
    factory.protocol = lambda:stream1
    port1 = reactor.listenTCP(4242, factory, interface='localhost')

    params = {
        'account': 'test2@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4343),
        }
    conn2, jid2 = make_connection(bus, queue.append, params)
    authenticator = XmppAuthenticator('test2', 'pass')
    stream2 = make_stream(queue.append, authenticator, protocol=XmppXmlStream)

    factory = twisted.internet.protocol.Factory()
    factory.protocol = lambda:stream2
    port1 = reactor.listenTCP(4343, factory, interface='localhost')


    bus.add_signal_receiver(
        lambda *args, **kw:
            queue.append(Event('dbus-signal',
                               path=unwrap(kw['path']),
                               signal=kw['member'], args=map(unwrap, args),
                               interface=kw['interface'])),
        None,       # signal name
        None,       # interface
        None,
        path_keyword='path',
        member_keyword='member',
        interface_keyword='interface',
        byte_arrays=True
        )

    try:
        test(queue, bus, conn1, conn2, stream1, stream2)
    finally:
        try:
            conn1.Disconnect()
            conn2.Disconnect()
        except dbus.DBusException, e:
            pass

