"""
Test the server sasl channel with Jabber auth pseudomechanisms
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream

import hashlib

import dbus

from servicetest import EventPattern, assertEquals, assertSameSets
from gabbletest import exec_test, JabberXmlStream, JabberAuthenticator
import constants as cs
from saslutil import connect_and_get_sasl_channel, abort_auth

JID = "test@localhost"
PASSWORD = "pass"

def test_jabber_pass_success(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    assertSameSets(['X-TELEPATHY-PASSWORD'],
            props.get(cs.SASL_AVAILABLE_MECHANISMS))

    chan.SASLAuthentication.StartMechanismWithData('X-TELEPATHY-PASSWORD',
            PASSWORD)

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    chan.SASLAuthentication.AcceptSASL()

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', {}])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(
        test_jabber_pass_success, {'password': None, 'account' : JID},
        protocol=JabberXmlStream,
        authenticator=JabberAuthenticator(JID.split('@')[0], PASSWORD))
