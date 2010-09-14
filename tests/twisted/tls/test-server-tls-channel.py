"""
Test the server TLS channel
"""

import base64
import dbus

from OpenSSL import crypto

from twisted.words.protocols.jabber import xmlstream
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
from twisted.internet import ssl

import ns
from gabbletest import exec_test, GabbleAuthenticator
from servicetest import ProxyWrapper, EventPattern, assertEquals
import constants as cs

JID = "test@example.org"

CA_CERT = 'ca-0-cert.pem'
CA_KEY  = 'ca-0-key.pem'

class TlsAuthenticator(GabbleAuthenticator):
    def __init__(self, username, password, resource=None):
        GabbleAuthenticator.__init__(self, username, password, resource)
        self.tls_encrypted = False
        self.sasl_authenticated = False

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        if self.sasl_authenticated and self.tls_encrypted:
            # Initiator authenticated and encrypted itself, and has started
            # a new stream.

            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            bind = features.addElement((ns.NS_XMPP_BIND, 'bind'))
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver(
                "/iq/bind[@xmlns='%s']" % ns.NS_XMPP_BIND, self.bindIq)
        elif self.tls_encrypted:
            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            mechanisms = features.addElement((ns.NS_XMPP_SASL, 'mechanisms'))
            mechanism = mechanisms.addElement('mechanism', content='PLAIN')
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver("/auth", self.auth)
            
        else:
            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            starttls = features.addElement((ns.NS_XMPP_TLS, 'starttls'))
            starttls.addElement('required')

            mechanisms = features.addElement((ns.NS_XMPP_SASL, 'mechanisms'))
            mechanism = mechanisms.addElement('mechanism', content='PLAIN')

            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver("/starttls", self.tlsAuth)

    def tlsAuth(self, auth):
        file = open(CA_KEY, 'rb')
        pem_key = file.read()
        file.close()
        pkey = crypto.load_privatekey(crypto.FILETYPE_PEM, pem_key, "")

        file = open(CA_CERT, 'rb')
        pem_cert = file.read()
        file.close()
        cert = crypto.load_certificate(crypto.FILETYPE_PEM, pem_cert)

        tls_ctx = ssl.CertificateOptions(privateKey=pkey, certificate=cert)

        self.xmlstream.send(domish.Element((ns.NS_XMPP_TLS, 'proceed')))
        self.xmlstream.transport.startTLS(tls_ctx)
        self.xmlstream.reset()
        self.tls_encrypted = True

    def auth(self, auth):
        assert (base64.b64decode(str(auth)) ==
            '\x00%s\x00%s' % (self.username, self.password))

        success = domish.Element((ns.NS_XMPP_SASL, 'success'))
        self.xmlstream.send(success)
        self.xmlstream.reset()
        self.sasl_authenticated = True

    def bindIq(self, iq):
        resource = xpath.queryForString('/iq/bind/resource', iq)
        if self.resource is not None:
            assertEquals(self.resource, resource)
        else:
            assert resource is not None

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        bind = result.addElement((ns.NS_XMPP_BIND, 'bind'))
        self.bare_jid = '%s@localhost' % self.username
        self.full_jid = '%s/%s' % (self.bare_jid, resource)
        jid = bind.addElement('jid', content=self.full_jid)
        self.xmlstream.send(result)

        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

class ServerTlsChanWrapper(ProxyWrapper):
    def __init__(self, object, default=cs.CHANNEL, interfaces={
            "ServerTLSConnection" : cs.CHANNEL_TYPE_SERVER_TLS_CONNECTION}):
        ProxyWrapper.__init__(self, object, default, interfaces)

class TlsCertificateWrapper(ProxyWrapper):
    def __init__(self, object, default=cs.AUTH_TLS_CERT, interfaces={
            "TLSCertificate" : cs.AUTH_TLS_CERT}):
        ProxyWrapper.__init__(self, object, default, interfaces)

def is_server_tls_chan_event(event):
    channels = event.args[0];

    if len(channels) > 1:
        return False

    path, props = channels[0]
    return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_SERVER_TLS_CONNECTION

def connect_and_get_tls_objects(q, bus, conn):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    ev, = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels',
                     predicate=is_server_tls_chan_event))

    channels = ev.args[0]
    path, props = channels[0]

    chan = ServerTlsChanWrapper(bus.get_object(conn.bus_name, path))
    hostname = props[cs.TLS_HOSTNAME]
    certificate_path = props[cs.TLS_CERT_PATH]

    assertEquals(hostname, 'example.org')

    return chan, hostname, certificate_path

def test_connect_early_close(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn)

    # close the channel early
    chan.Close()

    # we expect the fallback verification process to connect successfully
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
        )

def rejection_list_match(event):
    rejections = event.args[0];

    if len(rejections) != 1:
        return False

    return rejections == [(cs.TLS_REJECT_REASON_UNTRUSTED, cs.CERT_UNTRUSTED, {})]

def test_connect_fail(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn)

    certificate = TlsCertificateWrapper(bus.get_object(conn.bus_name, certificate_path))
    certificate.TLSCertificate.Reject([(cs.TLS_REJECT_REASON_UNTRUSTED, cs.CERT_UNTRUSTED, {})])

    q.expect_many(
        EventPattern('dbus-signal', signal='Rejected',
                     predicate=rejection_list_match),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='ConnectionError',
                     args=[cs.CERT_UNTRUSTED, {}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_CERT_UNTRUSTED])
        )

def test_connect_success(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn)

    certificate = TlsCertificateWrapper(bus.get_object(conn.bus_name, certificate_path))
    certificate.TLSCertificate.Accept()

    q.expect('dbus-signal', signal='Accepted')

    cert_props = dbus.Interface(certificate, cs.PROPERTIES_IFACE)
    state = cert_props.Get(cs.AUTH_TLS_CERT, 'State')
    rejections = cert_props.Get(cs.AUTH_TLS_CERT, 'Rejections')

    assertEquals (len(rejections), 0)

    chan.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
        )

if __name__ == '__main__':
    exec_test(test_connect_success, { 'account' : JID },
              authenticator=TlsAuthenticator(username='test', password='pass'))
    exec_test(test_connect_fail, { 'account' : JID },
              authenticator=TlsAuthenticator(username='test', password='pass'))
    exec_test(test_connect_early_close, { 'account' : JID },
              authenticator=TlsAuthenticator(username='test', password='pass'))
