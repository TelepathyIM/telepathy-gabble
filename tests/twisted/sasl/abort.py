"""
Test the server sasl aborting at different stages
"""

import dbus

from servicetest import EventPattern, assertEquals
from gabbletest import exec_test, call_async
import constants as cs
from saslutil import SaslComplexAuthenticator, connect_and_get_sasl_channel, \
    abort_auth, SaslPlainAuthenticator

JID = "test@example.org"
PASSWORD = "pass"
EXCHANGE = [("", "remote challenge"),
            ("Another step", "Here we go"),
            ("local response", "")]
MECHANISMS = ["PLAIN", "DIGEST-MD5", "ABORT-TEST"]

def test_abort_early(q, bus, conn, stream):
    pass

def test_abort_mid(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    chan.SaslAuthentication.StartMechanism("ABORT-TEST", EXCHANGE[0][1])

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', ''])

    abort_auth(q, chan, cs.SASL_ABORT_REASON_INVALID_CHALLENGE,
               "wrong data from server")

def test_disconnect_mid(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    chan.SaslAuthentication.StartMechanism("ABORT-TEST", EXCHANGE[0][1])

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', ''])

    call_async(q, conn, 'Disconnect')
    q.expect_many(EventPattern('dbus-signal', signal='StatusChanged',
                               args=[cs.CONN_STATUS_DISCONNECTED,
                                     cs.CSR_REQUESTED]),
                  EventPattern('dbus-return', method='Disconnect'))

def test_abort_connected(q, bus, conn, stream):
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

    try:
        chan.SaslAuthentication.Abort(cs.SASL_ABORT_REASON_USER_ABORT,
                                      "aborting too late")
    except dbus.DBusException:
        pass
    else:
        raise AssertionError, \
            "Aborting after success should raise an exception"

if __name__ == '__main__':
    exec_test(test_abort_early,
              {'password': None,'account' : JID})

    exec_test(test_abort_mid,
              {'password': None,'account' : JID},
              authenticator=SaslComplexAuthenticator(JID.split('@')[0],
                                                     EXCHANGE,
                                                     MECHANISMS))
    exec_test(test_disconnect_mid,
              {'password': None,'account' : JID},
             authenticator=SaslComplexAuthenticator(JID.split('@')[0],
                                                    EXCHANGE,
                                                    MECHANISMS))

    exec_test(
        test_abort_connected, {'password': None,'account' : JID},
        authenticator=SaslPlainAuthenticator(JID.split('@')[0], PASSWORD))
