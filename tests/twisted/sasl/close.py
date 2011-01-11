"""Test the SASL channel being undispatchable."""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test
import constants as cs
from saslutil import connect_and_get_sasl_channel

JID = 'weaver@crobuzon.fic'

def test_no_password(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.Close()

    q.expect_many(
            EventPattern('dbus-signal', path=chan.object_path,
                signal='Closed'),
            EventPattern('dbus-signal', path=conn.object_path,
                signal='ChannelClosed', args=[chan.object_path]),
            EventPattern('dbus-signal', path=conn.object_path,
                signal='ConnectionError',
                predicate=lambda e: e.args[0] == cs.AUTHENTICATION_FAILED),
            EventPattern('dbus-signal', path=conn.object_path,
                signal='StatusChanged', args=[cs.CONN_STATUS_DISCONNECTED,
                    cs.CSR_AUTHENTICATION_FAILED]),
            )

if __name__ == '__main__':
    exec_test(test_no_password, {'password': None,'account' : JID}, do_connect=False)
