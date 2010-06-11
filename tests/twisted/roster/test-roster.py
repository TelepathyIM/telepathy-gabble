"""
Test basic roster functionality.
"""

import dbus

from gabbletest import exec_test
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import assertLength
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'

    stream.send(event.stanza)

    pairs = expect_contact_list_signals(q, bus, conn,
            ['publish', 'subscribe', 'stored'])

    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'publish', ['amy@foo.com', 'bob@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'subscribe', ['amy@foo.com', 'che@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'stored', ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    assertLength(0, pairs)      # i.e. we've checked all of them

if __name__ == '__main__':
    exec_test(test)
