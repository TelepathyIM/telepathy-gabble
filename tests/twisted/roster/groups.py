"""
Test basic roster functionality.
"""

import dbus

from gabbletest import exec_test, expect_list_channel, expect_group_channel
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    # q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    group = item.addElement('group', content='women')

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'
    group = item.addElement('group', content='men')

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'
    group = item.addElement('group', content='men')

    stream.send(event.stanza)

    # FIXME: this is somewhat fragile - it's asserting the exact order that
    # things currently happen in roster.c. In reality the order is not
    # significant
    expect_list_channel(q, bus, conn, 'publish',
        ['amy@foo.com', 'bob@foo.com'])
    expect_list_channel(q, bus, conn, 'subscribe',
        ['amy@foo.com', 'che@foo.com'])
    expect_list_channel(q, bus, conn, 'known',
        ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])
    expect_group_channel(q, bus, conn, 'women', ['amy@foo.com'])
    expect_group_channel(q, bus, conn, 'men', ['bob@foo.com', 'che@foo.com'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

