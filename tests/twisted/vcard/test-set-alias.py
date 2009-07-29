
"""
Test alias setting support.
"""

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    conn.Aliasing.SetAliases({1: 'lala'})

    iq_event = q.expect('stream-iq', iq_type='set', query_ns='vcard-temp',
        query_name='vCard')
    acknowledge_iq(stream, iq_event.stanza)

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(1, u'lala')]])

if __name__ == '__main__':
    exec_test(test)
