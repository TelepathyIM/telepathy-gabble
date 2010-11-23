"""
Test the server sasl channel with the PLAIN mechanism
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream

from base64 import b64decode

import dbus

from servicetest import EventPattern, assertEquals, assertContains
from gabbletest import exec_test
import constants as cs
from saslutil import SaslPlainAuthenticator, connect_and_get_sasl_channel, \
    abort_auth

JID = "test@example.org"
PASSWORD = "pass"

def test_plain_success(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    assertEquals(JID, props.get(cs.SASL_AUTHORIZATION_IDENTITY))

    # On some servers we can't do DIGEST auth without this information.
    assertEquals('example.org', props.get(cs.SASL_DEFAULT_REALM))

    chan.SASLAuthentication.StartMechanismWithData(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    chan.SASLAuthentication.AcceptSASL()

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', {}])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_plain_no_account(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    chan.SASLAuthentication.AcceptSASL()

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', {}])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_plain_fail(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + 'wrong')

    e = q.expect('dbus-signal', signal='SASLStatusChanged',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH,
                 args=[cs.SASL_STATUS_IN_PROGRESS, '', {}])

    e = q.expect('dbus-signal', signal='SASLStatusChanged',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH)
    assertEquals(e.args[:2],
                 [cs.SASL_STATUS_SERVER_FAILED,
                  cs.AUTHENTICATION_FAILED])
    assertContains('debug-message', e.args[2])

    e = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(cs.AUTHENTICATION_FAILED, e.args[0])
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_AUTHENTICATION_FAILED])

def test_plain_abort(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    abort_auth(q, chan, cs.SASL_ABORT_REASON_INVALID_CHALLENGE,
               "Something is fishy")

def test_bad_usage(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    try:
        chan.SASLAuthentication.Respond("This is uncalled for");
    except dbus.DBusException, e:
        assertEquals (e.get_dbus_name(), cs.NOT_AVAILABLE)
    else:
        raise AssertionError, \
            "Calling Respond() before StartMechanism() should raise an error."

    chan.SASLAuthentication.StartMechanismWithData(
        'PLAIN', '\0' + JID.split('@')[0] + '\0' + PASSWORD)

    try:
        chan.SASLAuthentication.StartMechanismWithData('PLAIN', "foo")
    except dbus.DBusException, e:
        assertEquals (e.get_dbus_name(), cs.NOT_AVAILABLE)
    else:
        raise AssertionError, \
            "Calling StartMechanismWithData() twice should raise an error."

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    try:
        chan.SASLAuthentication.Respond("Responding after success");
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
