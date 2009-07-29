
"""
Regression test for a bug where receiving an empty vCard could make
RequesAliases fail to return.
"""

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
    acknowledge_iq(stream, event.stanza)

    q.expect('dbus-return', method='RequestAliases',
        value=([u'bob@foo.com'],))

    # A second request should be satisfied from the cache.
    assert conn.Aliasing.RequestAliases([handle]) == ['bob@foo.com']

if __name__ == '__main__':
    exec_test(test)
