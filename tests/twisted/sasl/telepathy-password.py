"""
Test the server sasl channel with the X-TELEPATHY-PASSWORD mechanism.
"""

from servicetest import call_async
from gabbletest import exec_test
import constants as cs
from saslutil import connect_and_get_sasl_channel

PASSWORD = "pass"

def test_close_straight_after_accept(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    call_async(q, chan.SASLAuthentication, 'StartMechanismWithData',
            'X-TELEPATHY-PASSWORD', PASSWORD)

    # In_Progress appears before StartMechanismWithData returns
    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_IN_PROGRESS, '', {}])

    q.expect('dbus-return', method='StartMechanismWithData')

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    # fd.o#32278:
    # When this was breaking, gabble received AcceptSASL and told the
    # success_async GAsyncResult to complete in an idle. But, before
    # the result got its callback called, Close was also received and
    # the auth manager cleared its channel. When the idle function was
    # finally reached it saw it no longer had a channel (it had been
    # cleared in the closed callback) and thought it should be
    # chaining up to the wocky auth registry but of course it should
    # be calling the channel finish function.
    call_async(q, chan.SASLAuthentication, 'AcceptSASL')
    call_async(q, chan, 'Close')

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', {}])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(test_close_straight_after_accept,
              {'password': None, 'account' : "test@example.org"})
