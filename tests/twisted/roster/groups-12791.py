"""
Test broken groups on the roster (regression test for fd.o #12791)
"""

import dbus

from gabbletest import exec_test, expect_list_channel
import constants as cs
import ns

def _expect_group_channel(q, bus, conn, name, contacts):
    event = q.expect('dbus-signal', signal='NewChannel')
    path, type, handle_type, handle, suppress_handler = event.args
    assert type == cs.CHANNEL_TYPE_CONTACT_LIST, type
    assert handle_type == cs.HT_GROUP, handle_type
    inspected = conn.InspectHandles(handle_type, [handle])[0]
    assert inspected == name, (inspected, name)
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan, cs.CHANNEL_IFACE_GROUP)
    inspected = conn.InspectHandles(cs.HT_CONTACT, group_iface.GetMembers())
    assert inspected == contacts, (inspected, contacts)

def test(q, bus, conn, stream):
    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    group = item.addElement('group', content='women')
    group = item.addElement('group', content='affected-by-fdo-12791')

    # This is a broken roster - Amy appears twice. This should only happen
    # if the server is somehow buggy. This was my initial attempt at
    # reproducing fd.o #12791 - I doubt it's very realistic, but we shouldn't
    # assert, regardless of what input we get!
    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    group = item.addElement('group', content='women')

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'
    group = item.addElement('group', content='men')

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
    group = item.addElement('group', content='men')

    stream.send(event.stanza)

    # FIXME: this is somewhat fragile - it's asserting the exact order that
    # things currently happen in roster.c. In reality the order is not
    # significant
    expect_list_channel(q, bus, conn, 'publish',
        ['amy@foo.com', 'bob@foo.com'])
    expect_list_channel(q, bus, conn, 'subscribe',
        ['amy@foo.com', 'che@foo.com'])
    expect_list_channel(q, bus, conn, 'stored',
        ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])
    _expect_group_channel(q, bus, conn, 'women', ['amy@foo.com'])
    _expect_group_channel(q, bus, conn, 'affected-by-fdo-12791', [])
    _expect_group_channel(q, bus, conn, 'men', ['bob@foo.com', 'che@foo.com'])

if __name__ == '__main__':
    exec_test(test)
