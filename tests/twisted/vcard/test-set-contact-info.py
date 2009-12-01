
"""
Test ContactInfo setting support.
"""

from servicetest import EventPattern, call_async
from gabbletest import exec_test, acknowledge_iq
import constants as cs


def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    call_async(q, conn.ContactInfo, 'SetContactInfo',
               [(u'fn', [], [u'Bob']),
                (u'n', [], [u'', u'Bob', u'', u'', u'']),
                (u'nickname', [], [u'bob'])])
    acknowledge_iq(stream, event.stanza)

    event = q.expect('stream-iq', iq_type='set', query_ns='vcard-temp',
        query_name='vCard')
    acknowledge_iq(stream, event.stanza)

    event = q.expect('dbus-return', method='SetContactInfo')


if __name__ == '__main__':
    exec_test(test)
