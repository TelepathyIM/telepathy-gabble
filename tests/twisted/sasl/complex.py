"""
Test the server sasl channel with the PLAIN mechanism
"""
import dbus

from servicetest import EventPattern, assertEquals
from gabbletest import exec_test
import constants as cs
from saslutil import SaslComplexAuthenticator, connect_and_get_sasl_channel

JID = "test@example.org"
EXCHANGE = [("", "Hi dear"),
            ("Hello", "How are you?"),
            ("Swell, how are you?", "Fine, thank you"),
            ("Do anything fun today?", "Nah"),
            ("Server data", "")]
MECHANISMS = ["PLAIN", "DIGEST-MD5", "POLITE-TEST"]

def test_complex_success(q, bus, conn, stream):
    chan, auth_info, avail_mechs = connect_and_get_sasl_channel(q, bus, conn)

    assertEquals(MECHANISMS, avail_mechs)

    try:
        chan.SaslAuthentication.StartMechanism("FOO", "")
    except dbus.DBusException:
        pass
    else:
        raise AssertionError, \
           "Expected DBusException when choosing unavailable mechanism"

    if EXCHANGE[0][0] == "":
        initial_data = EXCHANGE[0][1]
    else:
        initial_data = ""

    chan.SaslAuthentication.StartMechanism("POLITE-TEST", initial_data)

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', ''])

    for i, pair in enumerate(EXCHANGE[1:]):
        challenge, response = pair
        e = q.expect('dbus-signal', signal='NewChallenge',
                     interface=cs.CHANNEL_IFACE_SASL_AUTH,
                     args=[challenge or 'None'])

        challenge_prop = ''.join(
            map(chr, chan.Properties.Get(
                    cs.CHANNEL_IFACE_SASL_AUTH, "CurrentChallenge")))

        assertEquals(challenge, challenge_prop)

        if i == len(EXCHANGE) - 2:
            chan.SaslAuthentication.Accept()
            q.expect('dbus-signal', signal='StateChanged',
                     interface=cs.CHANNEL_IFACE_SASL_AUTH,
                     args=[cs.SASL_STATUS_CLIENT_ACCEPTED, '', ''])
        else:
            chan.SaslAuthentication.Respond(response)

    q.expect('dbus-signal', signal='StateChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', ''])

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    # Data on success
    exec_test(
        test_complex_success, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator(JID.split('@')[0],
                                               EXCHANGE, MECHANISMS))

    # No data on success
    exec_test(
        test_complex_success, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator(JID.split('@')[0],
                                               EXCHANGE, MECHANISMS, False))
