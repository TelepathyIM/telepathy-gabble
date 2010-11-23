"""
Test the server sasl channel with the PLAIN mechanism
"""
import dbus

from servicetest import EventPattern, assertEquals, assertSameSets
from gabbletest import exec_test
import constants as cs
from saslutil import SaslComplexAuthenticator, connect_and_get_sasl_channel

JID = "test@example.org"
INITIAL_RESPONSE = 'Thunder and lightning. Enter three Witches.'
CR_PAIRS = [
        ('When shall we three meet again?', 'Ere the set of sun.'),
        ('Where the place?', 'Upon the heath.'),
        ]
SUCCESS_DATA = 'Exeunt.'
MECHANISMS = ["PLAIN", "DIGEST-MD5", "SCOTTISH-PLAY"]

def test_complex_success(q, bus, conn, stream, with_extra_data=True,
        accept_early=False):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    assertSameSets(MECHANISMS, props.get(cs.SASL_AVAILABLE_MECHANISMS))

    try:
        chan.SASLAuthentication.StartMechanismWithData("FOO", "")
    except dbus.DBusException:
        pass
    else:
        raise AssertionError, \
           "Expected DBusException when choosing unavailable mechanism"

    if with_extra_data:
        chan.SASLAuthentication.StartMechanismWithData("SCOTTISH-PLAY",
                INITIAL_RESPONSE)
        e = q.expect('sasl-auth', initial_response=INITIAL_RESPONSE)
    else:
        chan.SASLAuthentication.StartMechanism("SCOTTISH-PLAY")
        e = q.expect('sasl-auth', has_initial_response=False)

    authenticator = e.authenticator

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', {}])

    if not with_extra_data:
        # send the stage directions in-band instead
        authenticator.challenge('')
        e = q.expect('dbus-signal', signal='NewChallenge',
                     interface=cs.CHANNEL_IFACE_SASL_AUTH)
        # this ought to be '' but dbus-python has fd.o #28131
        assert e.args in ([''], ['None'])
        chan.SASLAuthentication.Respond(INITIAL_RESPONSE)
        q.expect('sasl-response', response=INITIAL_RESPONSE)

    for challenge, response in CR_PAIRS:
        authenticator.challenge(challenge)
        q.expect('dbus-signal', signal='NewChallenge',
                     interface=cs.CHANNEL_IFACE_SASL_AUTH,
                     args=[challenge])
        chan.SASLAuthentication.Respond(response)
        q.expect('sasl-response', response=response)

    if with_extra_data:
        authenticator.success(SUCCESS_DATA)
    else:
        # The success data is sent in-band as a challenge
        authenticator.challenge(SUCCESS_DATA)

    q.expect('dbus-signal', signal='NewChallenge',
                 interface=cs.CHANNEL_IFACE_SASL_AUTH,
                 args=[SUCCESS_DATA])

    if accept_early:
        # the UI can tell that this challenge isn't actually a challenge,
        # it's a success in disguise
        chan.SASLAuthentication.AcceptSASL()
        q.expect('dbus-signal', signal='SASLStatusChanged',
                interface=cs.CHANNEL_IFACE_SASL_AUTH,
                args=[cs.SASL_STATUS_CLIENT_ACCEPTED, '', {}])
    else:
        chan.SASLAuthentication.Respond(dbus.ByteArray(''))

    if with_extra_data:
        # Wocky removes the distinction between a challenge containing
        # success data followed by a plain success, and a success
        # containing initial data, so we won't get to Server_Succeeded
        # til we "respond" to the "challenge". However, at the XMPP level,
        # we shouldn't get a response to a success.
        q.forbid_events([EventPattern('sasl-response')])
    else:
        q.expect('sasl-response', response='')
        authenticator.success(None)

    if not accept_early:
        # *now* we accept
        q.expect('dbus-signal', signal='SASLStatusChanged',
                interface=cs.CHANNEL_IFACE_SASL_AUTH,
                args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])
        # We're willing to accept this SASL transaction
        chan.SASLAuthentication.AcceptSASL()

    q.expect('dbus-signal', signal='SASLStatusChanged',
            interface=cs.CHANNEL_IFACE_SASL_AUTH,
            args=[cs.SASL_STATUS_SUCCEEDED, '', {}])

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
    chan.Close()
    # ... and check that the Connection is still OK
    conn.GetSelfHandle()

def test_complex_success_data(q, bus, conn, stream):
    test_complex_success(q, bus, conn, stream, True)

def test_complex_success_no_data(q, bus, conn, stream):
    test_complex_success(q, bus, conn, stream, False)

def test_complex_success_data_accept(q, bus, conn, stream):
    test_complex_success(q, bus, conn, stream, True, True)

def test_complex_success_no_data_accept(q, bus, conn, stream):
    test_complex_success(q, bus, conn, stream, False, True)

if __name__ == '__main__':
    exec_test(
        test_complex_success_data, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator(JID.split('@')[0], MECHANISMS))
    exec_test(
        test_complex_success_no_data, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator(JID.split('@')[0], MECHANISMS))
    exec_test(
        test_complex_success_data_accept, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator(JID.split('@')[0], MECHANISMS))
    exec_test(
        test_complex_success_no_data_accept, {'password': None, 'account' : JID},
        authenticator=SaslComplexAuthenticator(JID.split('@')[0], MECHANISMS))
