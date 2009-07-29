"""
Tests basic vCard caching functionality.
"""

from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq, sync_stream

import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, event.stanza)
    # Force Gabble to process the vCard before calling any methods.
    sync_stream(q, stream)

    # Request our alias and avatar, expect them to be resolved from cache.

    handle = conn.GetSelfHandle()
    call_async(q, conn.Avatars, 'RequestAvatar', handle)
    call_async(q, conn.Aliasing, 'RequestAliases', [handle])

    # FIXME - find out why RequestAliases returns before RequestAvatar even
    # though everything's cached. Probably due to queueing, which means
    # conn-avatars don't look into the cache before making a request. Prolly
    # should make vcard_request look into the cache itself, and return
    # immediately. Or not, if it's g_idle()'d. So it's better if conn-aliasing
    # look into the cache itself.

    r1, r2 = q.expect_many(
        EventPattern('dbus-return', method='RequestAliases'),
        EventPattern('dbus-error', method='RequestAvatar'))

    # Default alias is our jid
    assert r1.value[0] == ['test@localhost']

    # We don't have a vCard yet
    assert r2.error.get_dbus_name() == cs.NOT_AVAILABLE, \
        r2.error.get_dbus_name()
    assert r2.error.args[0] == 'contact vCard has no photo'

if __name__ == '__main__':
    exec_test(test)
