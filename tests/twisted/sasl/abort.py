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
    chan, props = connect_and_get_sasl_channel(q, bus, conn)
    abort_auth(q, chan, 31337, "maybe if I use an undefined code you'll crash")

def start_mechanism(q, bus, conn,
                    mechanism="ABORT-TEST", initial_response=EXCHANGE[0][1]):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)
    chan.SASLAuthentication.StartMechanismWithData(mechanism, initial_response)

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', {}])

    e = q.expect('sasl-auth', initial_response=initial_response)
    return chan, e.authenticator

def test_abort_mid(q, bus, conn, stream):
    chan, authenticator = start_mechanism(q, bus, conn)

    authenticator.challenge(EXCHANGE[1][0])
    q.expect('dbus-signal', signal='NewChallenge',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH,
                 args=[EXCHANGE[1][0]])

    abort_auth(q, chan, cs.SASL_ABORT_REASON_INVALID_CHALLENGE,
               "wrong data from server")

def test_disconnect_mid(q, bus, conn, stream):
    chan, authenticator = start_mechanism(q, bus, conn)

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
    initial_response = '\0' + JID.split('@')[0] + '\0' + PASSWORD
    chan, authenticator = start_mechanism(q, bus, conn,
        mechanism='PLAIN',
        initial_response=initial_response)

    authenticator.success(None)
    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    chan.SASLAuthentication.AcceptSASL()
    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', {}])
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    call_async(q, chan.SASLAuthentication, 'AbortSASL',
            cs.SASL_ABORT_REASON_USER_ABORT, "aborting too late")
    q.expect('dbus-error', method='AbortSASL', name=cs.NOT_AVAILABLE)
    chan.Close()

def test_give_up_while_waiting(q, bus, conn, stream,
                               channel_method,
                               authenticator_method):
    """Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=52146
    where closing the auth channel while waiting for a challenge from the
    server would not abort the SASL exchange, and the challenge subsequently
    arriving would make Gabble assert.

    """
    chan, authenticator = start_mechanism(q, bus, conn)

    # While Gabble is waiting for waiting for the server to make the next move,
    # a Telepathy client does something to try to end the authentication
    # process.
    channel_method(chan)

    # And then we hear something back from the server.
    authenticator_method(authenticator)

    # FIXME: Gabble should probably send <abort/> and wait for the server to
    # say <failure><aborted/> rather than unceremoniously closing the
    # connection.
    #
    # In the bug we're testing for, the stream connection would indeed be lost,
    # but Gabble would also crash and leave a core dump behind. So this test
    # would appear to pass, but 'make check' would fail as we want.
    q.expect_many(
        EventPattern('stream-connection-lost'),
        EventPattern('dbus-signal', signal='ConnectionError'),
        EventPattern(
            'dbus-signal', signal="StatusChanged",
            args=[cs.CONN_STATUS_DISCONNECTED,
                  cs.CSR_AUTHENTICATION_FAILED]),
        )

def test_close_then_challenge(q, bus, conn, stream):
    """This is the specific scenario for which 52146 was reported. The channel
    was being closed because its handler crashed.

    """
    test_give_up_while_waiting(q, bus, conn, stream,
        lambda chan: chan.Close(),
        lambda authenticator: authenticator.challenge(EXCHANGE[1][0]))

def test_close_then_success(q, bus, conn, stream):
    test_give_up_while_waiting(q, bus, conn, stream,
        lambda chan: chan.Close(),
        lambda authenticator: authenticator.success())

def test_close_then_failure(q, bus, conn, stream):
    test_give_up_while_waiting(q, bus, conn, stream,
        lambda chan: chan.Close(),
        lambda authenticator: authenticator.not_authorized())

def test_abort_then_challenge(q, bus, conn, stream):
    test_give_up_while_waiting(q, bus, conn, stream,
        lambda chan: chan.SASLAuthentication.AbortSASL(
            cs.SASL_ABORT_REASON_USER_ABORT, "bored now"),
        lambda authenticator: authenticator.challenge(EXCHANGE[1][0]))

def test_abort_then_success(q, bus, conn, stream):
    """FIXME: this test fails because the channel changes its state from
    TP_SASL_STATUS_CLIENT_FAILED to TP_SASL_STATUS_SERVER_SUCCEEDED and waits
    for the client to ack it when <success> arrives, which is dumb.

    """
    test_give_up_while_waiting(q, bus, conn, stream,
        lambda chan: chan.SASLAuthentication.AbortSASL(
            cs.SASL_ABORT_REASON_USER_ABORT, "bored now"),
        lambda authenticator: authenticator.success())

def test_abort_then_failure(q, bus, conn, stream):
    test_give_up_while_waiting(q, bus, conn, stream,
        lambda chan: chan.SASLAuthentication.AbortSASL(
            cs.SASL_ABORT_REASON_USER_ABORT, "bored now"),
        lambda authenticator: authenticator.not_authorized())

def abort_and_close(chan):
    chan.SASLAuthentication.AbortSASL(
        cs.SASL_ABORT_REASON_USER_ABORT, "bored now")
    chan.Close()

def test_abort_and_close_then_challenge(q, bus, conn, stream):
    test_give_up_while_waiting(q, bus, conn, stream,
        abort_and_close,
        lambda authenticator: authenticator.challenge(EXCHANGE[1][0]))

def test_abort_and_close_then_failure(q, bus, conn, stream):
    test_give_up_while_waiting(q, bus, conn, stream,
        abort_and_close,
        lambda authenticator: authenticator.not_authorized())

def exec_test_(func):
    # Can't use functools.partial, because the authenticator is stateful.
    authenticator = SaslEventAuthenticator(JID.split('@')[0], MECHANISMS)
    exec_test(func, do_connect=False, authenticator=authenticator,
        params={'password': None,
                'account' : JID,
               })

if __name__ == '__main__':
    exec_test_(test_abort_early)
    exec_test_(test_abort_mid)
    exec_test_(test_disconnect_mid)
    exec_test_(test_abort_connected)
    exec_test_(test_close_then_challenge)
    # exec_test_(test_close_then_success)
    exec_test_(test_close_then_failure)
    exec_test_(test_abort_then_challenge)
    # exec_test_(test_abort_then_success)
    exec_test_(test_abort_then_failure)
    exec_test_(test_abort_and_close_then_challenge)
    exec_test_(test_abort_and_close_then_failure)
