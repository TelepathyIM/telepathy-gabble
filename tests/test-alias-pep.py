
"""
Test PEP alias support.
"""

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq

def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, event.stanza)

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Aliasing, 'RequestAliases', [handle])

    event = q.expect('stream-iq', to='bob@foo.com', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub', query_name='pubsub')
    items = event.query.firstChildElement()
    assert items.name == 'items'
    assert items['node'] == "http://jabber.org/protocol/nick"
    result = make_result_iq(stream, event.stanza)
    pubsub = result.firstChildElement()
    items = pubsub.addElement('items')
    items['node'] = 'http://jabber.org/protocol/nick'
    item = items.addElement('item')
    item.addElement('nick', 'http://jabber.org/protocol/nick',
        content='Bobby')
    stream.send(result)

    q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(handle, u'Bobby')]])
    q.expect('dbus-return', method='RequestAliases',
        value=(['Bobby'],))

    # A second request should be satisfied from the cache.
    assert conn.Aliasing.RequestAliases([handle]) == ['Bobby']

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

