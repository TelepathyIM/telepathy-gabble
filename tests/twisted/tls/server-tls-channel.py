# coding=utf-8

"""
Copyright © 2010 Collabora Ltd.
Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.
"""

"""
Test the server TLS channel
"""

import base64
import dbus
import os

from OpenSSL import crypto

from twisted.words.protocols.jabber import xmlstream
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
from twisted.internet import ssl

import ns
from gabbletest import exec_test, XmppAuthenticator
from servicetest import ProxyWrapper, EventPattern
from servicetest import assertEquals, assertLength, assertSameSets
import constants as cs

JID = "test@example.org"

# the certificate is for the domain name 'weasel-juice.org'.
# the files are copied from wocky/tests/certs/tls-[cert,key].pem
CA_CERT_HOSTNAME = 'weasel-juice.org'

CA_CERT = os.environ.get('GABBLE_TWISTED_PATH', '.') + '/tls-cert.pem'
CA_KEY  = os.environ.get('GABBLE_TWISTED_PATH', '.') + '/tls-key.pem'

class TlsAuthenticator(XmppAuthenticator):
    def __init__(self, username, password, resource=None):
        XmppAuthenticator.__init__(self, username, password, resource)
        self.tls_encrypted = False

    def streamTLS(self):
        features = domish.Element((xmlstream.NS_STREAMS, 'features'))
        starttls = features.addElement((ns.NS_XMPP_TLS, 'starttls'))
        starttls.addElement('required')

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
        try:
            file = open(CA_KEY, 'rb')
            pem_key = file.read()
            pkey = crypto.load_privatekey(crypto.FILETYPE_PEM, pem_key, "")
        finally:
            file.close()

        try:
            file = open(CA_CERT, 'rb')
            pem_cert = file.read()
            cert = crypto.load_certificate(crypto.FILETYPE_PEM, pem_cert)
        finally:
            file.close()

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

def test_disconnect_inbetween(q, bus, conn, stream):
    # we don't expect a channel at all in this case,
    # as we lose the connection before the TLS channel is created
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED])

def is_server_tls_chan_event(event):
    channels = event.args[0];

    if len(channels) > 1:
        return False

    path, props = channels[0]
    return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_SERVER_TLS_CONNECTION

def connect_and_get_tls_objects(q, bus, conn, expect_example_jid=True):
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

    if expect_example_jid:
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

def connect_and_get_tls_properties(q, bus, conn):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn)
    chan_props = dbus.Interface(chan, cs.PROPERTIES_IFACE)
    return chan_props.GetAll(cs.CHANNEL_TYPE_SERVER_TLS_CONNECTION)

def test_channel_reference_identity(q, bus, conn, stream):
    props = connect_and_get_tls_properties (q, bus, conn)

    reference_identities = props["ReferenceIdentities"]
    assertSameSets(reference_identities, [ "example.org", "localhost"])
    assertEquals(props["Hostname"], "example.org")

def test_channel_reference_identity_with_extra(q, bus, conn, stream):
    props = connect_and_get_tls_properties (q, bus, conn)

    reference_identities = props["ReferenceIdentities"]
    assertSameSets(reference_identities,
                   [ "example.org", "hypnotoad.example.org", "localhost" ])
    assertEquals(props["Hostname"], "example.org")

def test_channel_reference_identity_with_extra_multiple(q, bus, conn, stream):
    props = connect_and_get_tls_properties (q, bus, conn)

    reference_identities = props["ReferenceIdentities"]
    assertSameSets(reference_identities,
                   [ "example.org", "hypnotoad.example.org", "localhost", "other.local" ])
    assertEquals(props["Hostname"], "example.org")

def test_channel_accept_hostname(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn, False)

    # close the channel early
    chan.Close()

    # we expect the fallback verification process to accept the hostname (because
    # it matches the JID's hostname or because it's in extra-certificate-identities),
    # but the certificate verification will fail anyway as we don't trust the CA
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_CERT_UNTRUSTED])
        )

def test_channel_reject_hostname(q, bus, conn, stream):
    chan, hostname, certificate_path = connect_and_get_tls_objects(q, bus, conn, False)

    # close the channel early
    chan.Close()

    # we expect the fallback verification process to not accept the hostname;
    # it doesn't match the JID's hostname nor the hostnames in the
    # extra-certificate-identities parameter
    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_CERT_HOSTNAME_MISMATCH])
        )

if __name__ == '__main__':
    exec_test(test_connect_success, { 'account' : JID },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_connect_fail, { 'account' : JID },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_connect_early_close_success,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : False },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_connect_early_close_fail,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : True },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_disconnect_inbetween, { 'account' : JID },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)

    # Certificate verification reference identity checks
    exec_test(test_channel_reference_identity, { 'account' : JID },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_channel_reference_identity_with_extra,
              { 'account' : JID,
                'extra-certificate-identities' : [ 'hypnotoad.example.org' ] },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_channel_reference_identity_with_extra_multiple,
              { 'account' : JID,
                'extra-certificate-identities' : [ 'hypnotoad.example.org', 'other.local', '' ] },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)

    # Hostname verification
    exec_test(test_channel_accept_hostname,
              { 'account' : 'test@' + CA_CERT_HOSTNAME,
                'ignore-ssl-errors' : False,
                'require-encryption' : True },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_channel_accept_hostname,
              { 'account' : 'test@' + CA_CERT_HOSTNAME,
                'ignore-ssl-errors' : False,
                'require-encryption' : True,
                'extra-certificate-identities' : [ 'other.local', CA_CERT_HOSTNAME ] },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_channel_accept_hostname,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : True,
                'extra-certificate-identities' : [ 'other.local', CA_CERT_HOSTNAME ] },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)

    exec_test(test_channel_reject_hostname,
              { 'account' : 'test@alternative.' + CA_CERT_HOSTNAME,
                'ignore-ssl-errors' : False,
                'require-encryption' : True },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_channel_reject_hostname,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : True,
                'extra-certificate-identities' : [ 'other.local', 'hypnotoad.example.org' ] },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_channel_reject_hostname,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : True },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
    exec_test(test_channel_reject_hostname,
              { 'account' : JID,
                'ignore-ssl-errors' : False,
                'require-encryption' : True,
                'extra-certificate-identities' : [ 'alternative.' + CA_CERT_HOSTNAME ] },
              authenticator=TlsAuthenticator(username='test', password='pass'), do_connect=False)
