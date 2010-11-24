"""
Test the server sasl channel with the PLAIN mechanism
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream

from base64 import b64decode

import dbus

from servicetest import EventPattern, assertEquals, assertContains, call_async
from gabbletest import exec_test
import constants as cs
from saslutil import SaslComplexAuthenticator, connect_and_get_sasl_channel, \
    abort_auth

JID = "test@example.org"
PASSWORD = "pass"
INITIAL_RESPONSE = '\0' + JID.split('@')[0] + '\0' + PASSWORD

def test_plain_success(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    assertEquals(JID, props.get(cs.SASL_AUTHORIZATION_IDENTITY))

    # On some servers we can't do DIGEST auth without this information.
    assertEquals('example.org', props.get(cs.SASL_DEFAULT_REALM))
    # We can't necessarily do PLAIN auth without this information.
    assertEquals('test', props.get(cs.SASL_DEFAULT_USERNAME))

    chan.SASLAuthentication.StartMechanismWithData('PLAIN', INITIAL_RESPONSE)
    e, _ = q.expect_many(
            EventPattern('sasl-auth', initial_response=INITIAL_RESPONSE),
            EventPattern('dbus-signal', signal='SASLStatusChanged',
                interface=cs.CHANNEL_IFACE_SASL_AUTH,
                args=[cs.SASL_STATUS_IN_PROGRESS, '', {}]),
            )
    authenticator = e.authenticator

    authenticator.success(None)
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

    assertEquals('example.com', props.get(cs.SASL_DEFAULT_REALM))
    assertEquals('', props.get(cs.SASL_DEFAULT_USERNAME))

    chan.SASLAuthentication.StartMechanismWithData('PLAIN', INITIAL_RESPONSE)
    e, _ = q.expect_many(
            EventPattern('sasl-auth', initial_response=INITIAL_RESPONSE),
            EventPattern('dbus-signal', signal='SASLStatusChanged',
                interface=cs.CHANNEL_IFACE_SASL_AUTH,
                args=[cs.SASL_STATUS_IN_PROGRESS, '', {}]),
            )
    authenticator = e.authenticator

    authenticator.success(None)
    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    chan.SASLAuthentication.AcceptSASL()

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', {}])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_plain_fail_helper(q, bus, conn, stream, element, error, csr):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData('PLAIN', INITIAL_RESPONSE)
    e, _ = q.expect_many(
            EventPattern('sasl-auth', initial_response=INITIAL_RESPONSE),
            EventPattern('dbus-signal', signal='SASLStatusChanged',
                interface=cs.CHANNEL_IFACE_SASL_AUTH,
                args=[cs.SASL_STATUS_IN_PROGRESS, '', {}]),
            )
    authenticator = e.authenticator

    authenticator.failure(element)
    e = q.expect('dbus-signal', signal='SASLStatusChanged',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH)
    assertEquals([cs.SASL_STATUS_SERVER_FAILED, error], e.args[:2])
    assertContains('debug-message', e.args[2])

    e = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(error, e.args[0])
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_DISCONNECTED, csr])

def test_plain_fail(q, bus, conn, stream):
    test_plain_fail_helper(q, bus, conn, stream, 'not-authorized',
            cs.AUTHENTICATION_FAILED, cs.CSR_AUTHENTICATION_FAILED)

def test_plain_bad_encoding(q, bus, conn, stream):
    test_plain_fail_helper(q, bus, conn, stream, 'incorrect-encoding',
            cs.AUTHENTICATION_FAILED, cs.CSR_AUTHENTICATION_FAILED)

def test_plain_weak(q, bus, conn, stream):
    test_plain_fail_helper(q, bus, conn, stream, 'mechanism-too-weak',
            cs.AUTHENTICATION_FAILED, cs.CSR_AUTHENTICATION_FAILED)

def test_plain_bad_authzid(q, bus, conn, stream):
    test_plain_fail_helper(q, bus, conn, stream, 'invalid-authzid',
            cs.AUTHENTICATION_FAILED, cs.CSR_AUTHENTICATION_FAILED)

def test_plain_bad_mech(q, bus, conn, stream):
    test_plain_fail_helper(q, bus, conn, stream, 'invalid-mechanism',
            cs.AUTHENTICATION_FAILED, cs.CSR_AUTHENTICATION_FAILED)

def test_plain_tempfail(q, bus, conn, stream):
    test_plain_fail_helper(q, bus, conn, stream, 'temporary-failure',
            cs.AUTHENTICATION_FAILED, cs.CSR_AUTHENTICATION_FAILED)

def test_plain_abort(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData('PLAIN', INITIAL_RESPONSE)
    e, _ = q.expect_many(
            EventPattern('sasl-auth', initial_response=INITIAL_RESPONSE),
            EventPattern('dbus-signal', signal='SASLStatusChanged',
                interface=cs.CHANNEL_IFACE_SASL_AUTH,
                args=[cs.SASL_STATUS_IN_PROGRESS, '', {}]),
            )
    authenticator = e.authenticator

    authenticator.success(None)
    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    abort_auth(q, chan, cs.SASL_ABORT_REASON_INVALID_CHALLENGE,
               "Something is fishy")

def test_bad_usage(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    call_async(q, chan.SASLAuthentication, 'Respond',
            'This is uncalled for')
    q.expect('dbus-error', method='Respond', name=cs.NOT_AVAILABLE)

    chan.SASLAuthentication.StartMechanismWithData('PLAIN', INITIAL_RESPONSE)
    e, _ = q.expect_many(
            EventPattern('sasl-auth', initial_response=INITIAL_RESPONSE),
            EventPattern('dbus-signal', signal='SASLStatusChanged',
                interface=cs.CHANNEL_IFACE_SASL_AUTH,
                args=[cs.SASL_STATUS_IN_PROGRESS, '', {}]),
            )
    authenticator = e.authenticator

    call_async(q, chan.SASLAuthentication, 'StartMechanismWithData',
            'PLAIN', 'foo')
    q.expect('dbus-error', method='StartMechanismWithData',
            name=cs.NOT_AVAILABLE)

    authenticator.success(None)
    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    call_async(q, chan.SASLAuthentication, 'Respond',
            'Responding after success')
    q.expect('dbus-error', method='Respond', name=cs.NOT_AVAILABLE)

if __name__ == '__main__':
    exec_test(
        test_plain_success, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_no_account,
        {'password': None, 'account' : 'example.com'},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_fail, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_bad_encoding, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_weak, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_bad_authzid, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_bad_mech, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_tempfail, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_plain_abort, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))

    exec_test(
        test_bad_usage, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator('test', ['PLAIN']))
