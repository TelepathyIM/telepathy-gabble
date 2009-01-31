"""
Test basic roster functionality.
"""

import dbus

from gabbletest import exec_test
from servicetest import EventPattern, tp_name_prefix

def _expect_contact_list_channel(q, bus, conn, name, contacts):
    old_signal, new_signal = q.expect_many(
            EventPattern('dbus-signal', signal='NewChannel'),
            EventPattern('dbus-signal', signal='NewChannels'),
            )

    path, type, handle_type, handle, suppress_handler = old_signal.args

    assert type == u'org.freedesktop.Telepathy.Channel.Type.ContactList'
    assert conn.InspectHandles(handle_type, [handle])[0] == name
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Group')
    members = group_iface.GetMembers()
    assert conn.InspectHandles(1, members) == contacts

    assert len(new_signal.args) == 1
    assert len(new_signal.args[0]) == 1         # one channel
    assert len(new_signal.args[0][0]) == 2      # two struct members
    assert new_signal.args[0][0][0] == path

    emitted_props = new_signal.args[0][0][1]
    assert emitted_props[tp_name_prefix + '.Channel.ChannelType'] ==\
            tp_name_prefix + '.Channel.Type.ContactList'
    assert emitted_props[tp_name_prefix + '.Channel.TargetHandleType'] == 3
    assert emitted_props[tp_name_prefix + '.Channel.TargetHandle'] == handle

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
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
    assert channel_props['TargetID'] == name, channel_props
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == ''
    assert channel_props['InitiatorHandle'] == 0

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = chan.GetAll(
            'org.freedesktop.Telepathy.Channel.Interface.Group',
            dbus_interface=dbus.PROPERTIES_IFACE)
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

