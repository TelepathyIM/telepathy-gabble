"""
Test the server sasl channel with Jabber auth pseudomechanisms
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream

import hashlib

import dbus

from servicetest import EventPattern, assertEquals
from gabbletest import exec_test, JabberXmlStream, JabberAuthenticator
import constants as cs
from saslutil import SaslPlainAuthenticator, connect_and_get_sasl_channel, \
    abort_auth

JID = "test@localhost"
PASSWORD = "pass"

def test_jabber_pass_success(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    assertEquals (
        avail_mechs, ['X-WOCKY-JABBER-PASSWORD', 'X-WOCKY-JABBER-DIGEST'])

    assert auth_info.has_key('session-id')

    digest = hashlib.sha1(auth_info['session-id'] + PASSWORD).hexdigest()

    chan.SaslAuthentication.StartMechanism('X-WOCKY-JABBER-DIGEST', digest)

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', ''])

    chan.SaslAuthentication.Accept()

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', ''])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(
        test_jabber_pass_success, {'password': None, 'account' : JID},
        protocol=JabberXmlStream,
        authenticator=JabberAuthenticator(JID.split('@')[0], PASSWORD))
