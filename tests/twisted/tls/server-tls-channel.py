"""
Copyright (C) 2010 Collabora Ltd.
Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.
"""

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
from gabbletest import exec_test, XmppAuthenticator
from servicetest import ProxyWrapper, EventPattern, assertEquals, assertLength
import constants as cs

JID = "test@example.org"

CA_CERT = 'tls-cert.pem'
CA_KEY  = 'tls-key.pem'

# the certificate is for the domain name 'weasel-juice.org'.
# the files are copied from wocky/tests/certs/tls-[cert,key].pem

class TlsAuthenticator(XmppAuthenticator):
    def __init__(self, username, password, resource=None):
        XmppAuthenticator.__init__(self, username, password, resource)
        self.tls_encrypted = False

    def streamTLS(self):
        features = domish.Element((xmlstream.NS_STREAMS, 'features'))
        starttls = features.addElement((ns.NS_XMPP_TLS, 'starttls'))
        starttls.addElement('required')

        mechanisms = features.addElement((ns.NS_XMPP_SASL, 'mechanisms'))
        mechanism = mechanisms.addElement('mechanism', content='PLAIN')
        self.xmlstream.send(features)

        self.xmlstream.addOnetimeObserver("/starttls", self.tlsAuth)

    def streamStarted(self, root=None):
        self.streamInitialize(root)

        if self.authenticated and self.tls_encrypted:
            self.streamIQ()
        elif self.tls_encrypted:
            self.streamSASL()
        else:
            self.streamTLS()

    def tlsAuth(self, auth):
        with open(CA_KEY, 'rb') as file:
            pem_key = file.read()
            file.close()
            pkey = crypto.load_privatekey(crypto.FILETYPE_PEM, pem_key, "")

        with open(CA_CERT, 'rb') as file:
            pem_cert = file.read()
            file.close()
            cert = crypto.load_certificate(crypto.FILETYPE_PEM, pem_cert)

        tls_ctx = ssl.CertificateOptions(privateKey=pkey, certificate=cert)

        self.xmlstream.send(domish.Element((ns.NS_XMPP_TLS, 'proceed')))
        self.xmlstream.transport.startTLS(tls_ctx)
        self.xmlstream.reset()
        self.tls_encrypted = True

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

def test_connect_early_close_success(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn)

    # close the channel early
    chan.Close()

    # we expect the fallback verification process to connect successfully,
    # even if the certificate doesn't match the hostname, as encryption-required is not set
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
        )

def test_connect_early_close_fail(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn)

    # close the channel early
    chan.Close()

    # we expect the fallback verification process to fail, as there's a hostname mismatch,
    # encryption-required is set and ignore-ssl-errors is not
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_CERT_HOSTNAME_MISMATCH])
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

    # we first expect the certificate to be rejected
    q.expect('dbus-signal', signal='Rejected', predicate=rejection_list_match)

    # this should trigger a ConnectionError
    q.expect('dbus-signal', signal='ConnectionError', args=[cs.CERT_UNTRUSTED, {}])

    # at this point the channel should be closed
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed')
        )

    # finally, we should receive a StatusChanged signal on the connection
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_CERT_UNTRUSTED])

def test_connect_success(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn)

    certificate = TlsCertificateWrapper(bus.get_object(conn.bus_name, certificate_path))
    certificate.TLSCertificate.Accept()

    q.expect('dbus-signal', signal='Accepted')

    cert_props = dbus.Interface(certificate, cs.PROPERTIES_IFACE)
    state = cert_props.Get(cs.AUTH_TLS_CERT, 'State')
    rejections = cert_props.Get(cs.AUTH_TLS_CERT, 'Rejections')

    assertLength(0, rejections)

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
    exec_test(test_connect_early_close_success,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : False },
              authenticator=TlsAuthenticator(username='test', password='pass'))
    exec_test(test_connect_early_close_fail,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : True },
              authenticator=TlsAuthenticator(username='test', password='pass'))
