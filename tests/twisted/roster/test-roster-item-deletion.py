"""
Regression test for http://bugs.freedesktop.org/show_bug.cgi?id=19524
"""

import dbus

from twisted.words.protocols.jabber.client import IQ

from gabbletest import exec_test, acknowledge_iq
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import assertLength
import constants as cs
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

    pairs = expect_contact_list_signals(q, bus, conn,
            ['publish', 'subscribe', 'stored'])

    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'publish', [])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'subscribe', [])
    stored = check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'stored', ['quux@foo.com'])

    assertLength(0, pairs)      # i.e. we've checked all of them

    stored.Group.RemoveMembers([dbus.UInt32(2)], '')
    send_roster_iq(stream, 'quux@foo.com', 'remove')

    acknowledge_iq(stream, q.expect('stream-iq').stanza)

if __name__ == '__main__':
    exec_test(test)
