
"""
Test connecting with different ContactList.DownloadAtConnection values
"""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test, sync_stream, call_async
import constants as cs
import ns

forbidden = [EventPattern('stream-iq', query_ns=ns.ROSTER)]

def test_get_roster(q, bus, conn, stream):
    # DownloadAtConnection = True, so gabble should get the roster
    # automatically
    q.expect('stream-iq', query_ns=ns.ROSTER)

    # but calling ContactList.Download should not try and get the
    # roster again
    q.forbid_events(forbidden)
    conn.ContactList.Download()
    sync_stream(q, stream)
    q.unforbid_events(forbidden)

def test_dont_get_roster(q, bus, conn, stream):
    # DownloadAtConnection = False, so let's make sure the roster
    # isn't fetched automatically
    q.forbid_events(forbidden)

    conn.Connect()
    q.expect_many(EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))
    sync_stream(q, stream)
    q.unforbid_events(forbidden)

    # seems fine, now calling Download should try and get the roster
    # successfully.
    call_async(q, conn.ContactList, 'Download')

    q.expect_many(EventPattern('stream-iq', query_ns=ns.ROSTER),
                  EventPattern('dbus-return', method='Download'))

if __name__ == '__main__':
    # Parameter DownloadAtConnection = True
    exec_test(test_get_roster,
              params={cs.CONN_IFACE_CONTACT_LIST + '.DownloadAtConnection': True})

    # Parameter DownloadAtConnection = False
    exec_test(test_dont_get_roster,
              params={cs.CONN_IFACE_CONTACT_LIST + '.DownloadAtConnection': False},
              do_connect=False)

