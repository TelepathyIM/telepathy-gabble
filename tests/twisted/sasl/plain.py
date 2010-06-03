"""
Test the server sasl channel with the PLAIN mechanism
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream

from base64 import b64decode

import dbus

from servicetest import EventPattern, assertEquals
from gabbletest import exec_test
import constants as cs
from saslutil import SaslPlainAuthenticator, connect_and_get_sasl_channel, \
    abort_auth

JID = "test@example.org"
PASSWORD = "pass"

def test_plain_success(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    assertEquals('@'.join((auth_info['username'], auth_info['realm'])), JID)

    chan.SaslAuthentication.StartMechanism(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', ''])

    chan.SaslAuthentication.Accept()

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', ''])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_plain_no_account(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    chan.SaslAuthentication.StartMechanism(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', ''])

    chan.SaslAuthentication.Accept()

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', ''])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_plain_fail(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    assertEquals('@'.join((auth_info['username'], auth_info['realm'])), JID)

    chan.SaslAuthentication.StartMechanism(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + 'wrong')

    e = q.expect('dbus-signal', signal='StateChanged',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH)

    assertEquals(e.args[:2],
                 [cs.SASL_STATUS_SERVER_FAILED,
                  'org.freedesktop.Telepathy.Error.AuthenticationFailed'])

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_AUTHENTICATION_FAILED])

def test_plain_abort(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    assertEquals('@'.join((auth_info['username'], auth_info['realm'])), JID)

    chan.SaslAuthentication.StartMechanism(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', ''])

    abort_auth(q, chan, cs.SASL_ABORT_REASON_INVALID_CHALLENGE,
               "Something is fishy")

def test_bad_usage(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    try:
        chan.SaslAuthentication.Respond("This is uncalled for");
    except dbus.DBusException, e:
        assertEquals (e.get_dbus_name(), cs.NOT_AVAILABLE)
    else:
        raise AssertionError, \
            "Calling Respond() before StartMechanism() should raise an error."

    chan.SaslAuthentication.StartMechanism(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    try:
        chan.SaslAuthentication.StartMechanism('PLAIN', "foo")
    except dbus.DBusException, e:
        assertEquals (e.get_dbus_name(), cs.NOT_AVAILABLE)
    else:
        raise AssertionError, \
            "Calling StartMechanism() twice should raise an error."

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', ''])

    try:
        chan.SaslAuthentication.Respond("Responding after success");
    except dbus.DBusException, e:
        assertEquals (e.get_dbus_name(), cs.NOT_AVAILABLE)
    else:
        raise AssertionError, \
            "Calling Respond() after success should raise an error."

if __name__ == '__main__':
    exec_test(
        test_plain_success, {'password': None, 'account' : JID},
        authenticator=SaslPlainAuthenticator(JID.split('@')[0], PASSWORD))

    exec_test(
        test_plain_no_account,
        {'password': None, 'account' : 'example.com'},
        authenticator=SaslPlainAuthenticator(JID.split('@')[0], PASSWORD))

    exec_test(
        test_plain_fail, {'password': None, 'account' : JID},
        authenticator=SaslPlainAuthenticator(JID.split('@')[0], PASSWORD))

    exec_test(
        test_plain_abort, {'password': None, 'account' : JID},
        authenticator=SaslPlainAuthenticator(JID.split('@')[0], PASSWORD))

    exec_test(
        test_bad_usage, {'password': None, 'account' : JID},
        authenticator=SaslPlainAuthenticator(JID.split('@')[0], PASSWORD))
