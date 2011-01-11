"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=31412
"""

import dbus

from servicetest import call_async, EventPattern, sync_dbus
from gabbletest import (exec_test, make_result_iq, acknowledge_iq,
        disconnect_conn)
import constants as cs

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, event.stanza)

    handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@foo.com'])[0]
    call_async(q, conn.Aliasing, 'RequestAliases', [handle])

    # First, Gabble sends a PEP query
    event = q.expect('stream-iq', to='bob@foo.com', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub', query_name='pubsub')

    # We disconnect too soon to get a reply
    disconnect_conn(q, conn, stream)

    # fd.o #31412 was that while the request pipeline was shutting down,
    # it would give the PEP query an error; the aliasing code would
    # respond by falling back to vCard via the request pipeline, which
    # was no longer there, *crash*.

    # check that Gabble hasn't crashed
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(test)
