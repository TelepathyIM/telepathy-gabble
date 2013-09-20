"""
Test broken groups on the roster (regression test for fd.o #12791)
"""

import dbus

from gabbletest import exec_test
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import assertLength
import constants as cs
import ns

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='women')
    item.addElement('group', content='affected-by-fdo-12791')

    # This is a broken roster - Amy appears twice. This should only happen
    # if the server is somehow buggy. This was my initial attempt at
    # reproducing fd.o #12791 - I doubt it's very realistic, but we shouldn't
    # assert, regardless of what input we get!
    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='women')

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'
    item.addElement('group', content='men')

    # This is what was *actually* strange about the #12791 submitter's roster -
    # Bob appears, fully subscribed, but also there's an attempt to subscribe
    # to one of Bob's resources. We now ignore such items
    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com/Resource'
    item['subscription'] = 'none'
    item['ask'] = 'subscribe'

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'
    item.addElement('group', content='men')

    stream.send(event.stanza)

    pairs = expect_contact_list_signals(q, bus, conn,
            ['publish', 'subscribe', 'stored'],
            ['men', 'women', 'affected-by-fdo-12791'])

    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'publish', ['amy@foo.com', 'bob@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'subscribe', ['amy@foo.com', 'che@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'stored', ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_GROUP,
            'men', ['bob@foo.com', 'che@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_GROUP,
            'women', ['amy@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_GROUP,
            'affected-by-fdo-12791', [])

    assertLength(0, pairs)      # i.e. we've checked all of them

if __name__ == '__main__':
    exec_test(test)
