
"""
Test basic roster functionality.
"""

import dbus

from gabbletest import exec_test

def _expect_contact_list_channel(q, bus, conn, name, contacts):
    event = q.expect('dbus-signal', signal='NewChannel')
    path, type, handle_type, handle, suppress_handler = event.args

    assert type == u'org.freedesktop.Telepathy.Channel.Type.ContactList'
    assert conn.InspectHandles(handle_type, [handle])[0] == name
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Group')
    members = group_iface.GetMembers()
    assert conn.InspectHandles(1, members) == contacts

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == handle,\
            (channel_props.get('TargetHandle'), handle)
    assert channel_props.get('TargetHandleType') == 3,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.ContactList',\
            channel_props.get('ChannelType')
    assert 'org.freedesktop.Telepathy.Channel.Interface.Group' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = chan.GetAll(
            'org.freedesktop.Telepathy.Channel.Interface.Group',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert 'HandleOwners' in group_props, group_props
    assert 'Members' in group_props, group_props
    assert group_props['Members'] == members, group_props['Members']
    assert 'LocalPendingMembers' in group_props, group_props
    assert group_props['LocalPendingMembers'] == []
    assert 'RemotePendingMembers' in group_props, group_props
    assert group_props['RemotePendingMembers'] == []
    assert 'GroupFlags' in group_props, group_props

def test(q, bus, conn, stream):
    conn.Connect()
    # q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    event = q.expect('stream-iq', query_ns='jabber:iq:roster')
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

    _expect_contact_list_channel(q, bus, conn, 'publish',
        ['amy@foo.com', 'bob@foo.com'])
    _expect_contact_list_channel(q, bus, conn, 'subscribe',
        ['amy@foo.com', 'che@foo.com'])
    _expect_contact_list_channel(q, bus, conn, 'known',
        ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

