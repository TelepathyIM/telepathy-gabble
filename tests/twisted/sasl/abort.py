"""
Test the server sasl aborting at different stages
"""

import dbus

from servicetest import EventPattern, assertEquals
from gabbletest import exec_test, call_async
import constants as cs
from saslutil import SaslEventAuthenticator, connect_and_get_sasl_channel, \
    abort_auth

JID = "test@example.org"
PASSWORD = "pass"
EXCHANGE = [("", "remote challenge"),
            ("Another step", "Here we go"),
            ("local response", "")]
MECHANISMS = ["PLAIN", "DIGEST-MD5", "ABORT-TEST"]

def test_abort_early(q, bus, conn, stream):
    pass

def test_abort_mid(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData("ABORT-TEST", EXCHANGE[0][1])

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', {}])

    e = q.expect('sasl-auth', initial_response=EXCHANGE[0][1])
    authenticator = e.authenticator

    authenticator.challenge(EXCHANGE[1][0])
    q.expect('dbus-signal', signal='NewChallenge',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH,
                 args=[EXCHANGE[1][0]])

    abort_auth(q, chan, cs.SASL_ABORT_REASON_INVALID_CHALLENGE,
               "wrong data from server")

def test_disconnect_mid(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData("ABORT-TEST", EXCHANGE[0][1])

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', {}])

    e = q.expect('sasl-auth', initial_response=EXCHANGE[0][1])
    authenticator = e.authenticator

    authenticator.challenge(EXCHANGE[1][0])
    q.expect('dbus-signal', signal='NewChallenge',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH,
                 args=[EXCHANGE[1][0]])

    call_async(q, conn, 'Disconnect')
    q.expect_many(EventPattern('dbus-signal', signal='StatusChanged',
                               args=[cs.CONN_STATUS_DISCONNECTED,
                                     cs.CSR_REQUESTED]),
                  EventPattern('dbus-return', method='Disconnect'))

def test_abort_connected(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.SASLAuthentication.StartMechanismWithData('PLAIN',
        '\0' + JID.split('@')[0] + '\0' + PASSWORD)
    e, _ = q.expect_many(
            EventPattern('sasl-auth'),
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

    call_async(q, chan.SASLAuthentication, 'AbortSASL',
            cs.SASL_ABORT_REASON_USER_ABORT, "aborting too late")
    q.expect('dbus-error', method='AbortSASL', name=cs.NOT_AVAILABLE)
    chan.Close()

if __name__ == '__main__':
    exec_test(test_abort_early,
              {'password': None,'account' : JID})

    exec_test(test_abort_mid,
              {'password': None,'account' : JID},
              authenticator=SaslEventAuthenticator(JID.split('@')[0],
                                                     MECHANISMS))
    exec_test(test_disconnect_mid,
              {'password': None,'account' : JID},
             authenticator=SaslEventAuthenticator(JID.split('@')[0],
                                                    MECHANISMS))

    exec_test(
        test_abort_connected, {'password': None,'account' : JID},
        authenticator=SaslEventAuthenticator(JID.split('@')[0], ['PLAIN']))
