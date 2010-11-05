"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=31412
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()

    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, event.stanza)

    handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@foo.com'])[0]
    call_async(q, conn.Aliasing, 'RequestAliases', [handle])

    # First, Gabble sends a PEP query
    event = q.expect('stream-iq', to='bob@foo.com', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub', query_name='pubsub')

    # We disconnect too soon to get a reply
    call_async(q, conn, 'Disconnect')
    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-closed'),
        )
    stream.sendFooter()
    q.expect('dbus-return', method='Disconnect')

    # fd.o #31412 was that while the request pipeline was shutting down,
    # it would give the PEP query an error; the aliasing code would
    # respond by falling back to vCard via the request pipeline, which
    # was no longer there, *crash*.

    # check that Gabble hasn't crashed
    cm = bus.get_object(cs.CM + '.gabble',
            '/' + cs.CM.replace('.', '/') + '/gabble')
    call_async(q, dbus.Interface(cm, cs.CM), 'ListProtocols')
    q.expect('dbus-return', method='ListProtocols')

if __name__ == '__main__':
    exec_test(test)
