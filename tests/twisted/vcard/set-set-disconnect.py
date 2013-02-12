
"""
Regression test for crash when disconnecting in the middle of a set.
"""

from servicetest import EventPattern, call_async, sync_dbus
from gabbletest import exec_test, acknowledge_iq, expect_and_handle_get_vcard, sync_stream, disconnect_conn
import constants as cs

def test(q, bus, conn, stream):
    expect_and_handle_get_vcard(q, stream)
    sync_stream(q, stream)

    call_async(
        q, conn.Avatars, 'SetAvatar', 'Guy.brush', 'image/x-mighty-pirate')
    expect_and_handle_get_vcard(q, stream)
    iq_event = q.expect(
        'stream-iq', iq_type='set', query_ns='vcard-temp', query_name='vCard')
    call_async(
        q, conn.Avatars, 'SetAvatar', 'LeChuck.brush', 'image/x-ghost-pirate')

    disconnect_conn(q, conn, stream)

    q.expect('dbus-error', method='SetAvatar', name=cs.NOT_AVAILABLE)
    q.expect('dbus-error', method='SetAvatar', name=cs.NOT_AVAILABLE)
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(test)
