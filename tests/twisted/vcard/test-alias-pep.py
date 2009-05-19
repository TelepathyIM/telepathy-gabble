
"""
Test PEP alias support.
"""

from servicetest import call_async, EventPattern, assertEquals
from gabbletest import exec_test, make_result_iq, acknowledge_iq

import constants as cs
import ns

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

    event = q.expect('stream-iq', to='bob@foo.com', iq_type='get',
        query_ns=ns.PUBSUB, query_name='pubsub')
    items = event.query.firstChildElement()
    assertEquals('items', items.name)
    assertEquals(ns.NICK, items['node'])
    result = make_result_iq(stream, event.stanza)
    pubsub = result.firstChildElement()
    items = pubsub.addElement('items')
    items['node'] = ns.NICK
    item = items.addElement('item')
    item.addElement('nick', ns.NICK, content='Bobby')
    stream.send(result)

    q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(handle, u'Bobby')]])
    q.expect('dbus-return', method='RequestAliases',
        value=(['Bobby'],))

    # A second request should be satisfied from the cache.
    assertEquals(['Bobby'], conn.Aliasing.RequestAliases([handle]))

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),

if __name__ == '__main__':
    exec_test(test)

