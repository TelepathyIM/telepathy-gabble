
"""
Test alias support.
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

    # Nack PEP query.
    event = q.expect('stream-iq', to='bob@foo.com', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub', query_name='pubsub')
    items = event.query.firstChildElement()
    assert items.name == 'items'
    assert items['node'] == "http://jabber.org/protocol/nick"
    result = make_result_iq(stream, event.stanza)
    result['type'] = 'error'
    error = result.addElement('error')
    error['type'] = 'auth'
    error.addElement('forbidden', 'urn:ietf:params:xml:ns:xmpp-stanzas')
    stream.send(result)

    event = q.expect('stream-iq', to='bob@foo.com', query_ns='vcard-temp',
        query_name='vCard')
    result = make_result_iq(stream, event.stanza)
    vcard = result.firstChildElement()
    vcard.addElement('NICKNAME', content='Bobby')
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

