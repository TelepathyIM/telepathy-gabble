
"""
Regression test for crash when disconnecting in the middle of a set.
"""

from servicetest import EventPattern, call_async, sync_dbus
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

    call_async(
        q, conn.Avatars, 'SetAvatar', 'Guy.brush', 'image/x-mighty-pirate')
    iq_event = q.expect(
        'stream-iq', iq_type='set', query_ns='vcard-temp', query_name='vCard')
    call_async(
        q, conn.Avatars, 'SetAvatar', 'LeChuck.brush', 'image/x-ghost-pirate')
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
    q.expect('dbus-error', method='SetAvatar', name=cs.NOT_AVAILABLE)
    q.expect('dbus-error', method='SetAvatar', name=cs.NOT_AVAILABLE)
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(test)
