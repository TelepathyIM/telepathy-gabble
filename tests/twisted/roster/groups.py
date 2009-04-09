"""
Test basic roster functionality.
"""

import dbus

from gabbletest import exec_test
import constants as cs

def _expect_contact_list_channel(q, bus, conn, name, contacts):
    event = q.expect('dbus-signal', signal='NewChannel')
    path, type, handle_type, handle, suppress_handler = event.args
    assert type == cs.CHANNEL_TYPE_CONTACT_LIST, type
    assert handle_type == cs.HT_CONTACT_LIST, handle_type
    inspected = conn.InspectHandles(handle_type, [handle])[0]
    assert inspected == name, (inspected, name)
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan, cs.CHANNEL_IFACE_GROUP)
    inspected = conn.InspectHandles(cs.HT_CONTACT, group_iface.GetMembers())
    assert inspected == contacts, (inspected, contacts)

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

    channel_props = chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == name, channel_props
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == ''
    assert channel_props['InitiatorHandle'] == 0

def test(q, bus, conn, stream):
    conn.Connect()
    # q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    event = q.expect('stream-iq', query_ns='jabber:iq:roster')
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
    _expect_contact_list_channel(q, bus, conn, 'publish',
        ['amy@foo.com', 'bob@foo.com'])
    _expect_contact_list_channel(q, bus, conn, 'subscribe',
        ['amy@foo.com', 'che@foo.com'])
    _expect_contact_list_channel(q, bus, conn, 'known',
        ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])
    _expect_group_channel(q, bus, conn, 'women', ['amy@foo.com'])
    _expect_group_channel(q, bus, conn, 'men', ['bob@foo.com', 'che@foo.com'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

