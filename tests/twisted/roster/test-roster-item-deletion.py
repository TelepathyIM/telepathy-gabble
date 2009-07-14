"""
Regression test for http://bugs.freedesktop.org/show_bug.cgi?id=19524
"""

import dbus

from twisted.words.protocols.jabber.client import IQ

from gabbletest import exec_test, acknowledge_iq, expect_list_channel
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    def send_roster_iq(stream, jid, subscription):
        iq = IQ(stream, "set")
        iq['id'] = 'push'
        query = iq.addElement('query')
        query['xmlns'] = ns.ROSTER
        item = query.addElement('item')
        item['jid'] = jid
        item['subscription'] = subscription
        stream.send(iq)

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'quux@foo.com'
    item['subscription'] = 'none'

    stream.send(event.stanza)

    # FIXME: this is somewhat fragile - it's asserting the exact order that
    # things currently happen in roster.c. In reality the order is not
    # significant
    publish = expect_list_channel(q, bus, conn, 'publish', [])
    subscribe = expect_list_channel(q, bus, conn, 'subscribe', [])
    stored = expect_list_channel(q, bus, conn, 'stored', ['quux@foo.com'])

    stored.Group.RemoveMembers([dbus.UInt32(2)], '')
    send_roster_iq(stream, 'quux@foo.com', 'remove')

    acknowledge_iq(stream, q.expect('stream-iq').stanza)

if __name__ == '__main__':
    exec_test(test)
