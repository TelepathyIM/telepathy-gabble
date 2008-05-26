"""
Test OLPC extensions to MUC invitations.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go, make_result_iq, exec_test
from servicetest import call_async, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    buddy_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    call_async(q, buddy_iface, 'GetActivities', bob_handle)

    event = q.expect('stream-iq', iq_type='get', to='bob@localhost')
    # Bob has no activities
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'bob@localhost'
    stream.send(event.stanza)

    event = q.expect('dbus-return', method='GetActivities')
    # initially, Bob has no activities
    assert event.value == ([],)

    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'bob@localhost'
    message['to'] = 'test@localhost'
    properties = message.addElement(
        ('http://laptop.org/xmpp/activity-properties', 'properties'))
    properties['room'] = 'chat@conf.localhost'
    properties['activity'] = 'foo_id'
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#ffff00,#00ffff')
    property = properties.addElement((None, 'property'))
    property['type'] = 'bool'
    property['name'] = 'private'
    property.addContent('1')

    stream.send(message)

    message = domish.Element((None, 'message'))
    message['from'] = 'chat@conf.localhost'
    message['to'] = 'test@localhost'
    x = message.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    invite = x.addElement((None, 'invite'))
    invite['from'] = 'bob@localhost'
    reason = invite.addElement((None, 'reason'))
    reason.addContent('No good reason')

    stream.send(message)

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == 'org.freedesktop.Telepathy.Channel.Type.Text'

    assert event.args[2] == 2   # handle type
    assert event.args[3] == 1   # handle
    room_handle = 1

    text_chan = bus.get_object(conn.bus_name, event.args[0])
    group_iface = dbus.Interface(text_chan,
            'org.freedesktop.Telepathy.Channel.Interface.Group')

    members = group_iface.GetAllMembers()[0]
    local_pending = group_iface.GetAllMembers()[1]
    remote_pending = group_iface.GetAllMembers()[2]

    assert len(members) == 1
    assert conn.InspectHandles(1, members)[0] == 'bob@localhost'
    bob_handle = members[0]
    assert len(local_pending) == 1
    # FIXME: the username-part-is-nickname assumption
    assert conn.InspectHandles(1, local_pending)[0] == \
            'chat@conf.localhost/test'
    assert len(remote_pending) == 0

    room_self_handle = group_iface.GetSelfHandle()
    assert room_self_handle == local_pending[0]

    # by now, we should have picked up the extra activity properties
    buddy_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    call_async(q, buddy_iface, 'GetActivities', bob_handle)

    event = q.expect('stream-iq', iq_type='get', to='bob@localhost')
    # Bob still has no (public) activities
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'bob@localhost'
    stream.send(event.stanza)

    event = q.expect('dbus-return', method='GetActivities')

    assert event.value == ([('foo_id', room_handle)],)

    # OK, now accept the invitation
    call_async(q, group_iface, 'AddMembers', [room_self_handle], 'Oh, OK then')

    _, event = q.expect_many(
        EventPattern('stream-presence', to='chat@conf.localhost/test'),
        EventPattern('dbus-signal', signal='MembersChanged')
        )

    assert event.args == ['', [], [bob_handle], [],
            [room_self_handle], 0, room_self_handle]

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    q.expect('dbus-return', method='AddMembers')

    event = q.expect('dbus-signal', signal='MembersChanged')
    assert event.args == ['', [room_self_handle], [], [], [], 0, 0]

    call_async(q, buddy_iface, 'SetActivities', [('foo_id', room_handle)])

    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    q.expect('dbus-return', method='SetActivities')
    act_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.ActivityProperties')
    call_async(q, act_prop_iface, 'SetProperties',
        room_handle, {'color': '#ffff00,#00ffff', 'private': True})

    q.expect('dbus-return', method='SetProperties')
    # Test sending an invitation
    alice_handle = conn.RequestHandles(1,
        ['alice@localhost'])[0]
    call_async(q, group_iface, 'AddMembers', [alice_handle],
            'I want to test invitations')

    event = q.expect('stream-message', to='alice@localhost')
    message = event.stanza

    properties = xpath.queryForNodes('/message/properties', message)
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == 'http://laptop.org/xmpp/activity-properties'
    assert properties[0]['room'] == 'chat@conf.localhost'
    assert properties[0]['activity'] == 'foo_id'

    property = xpath.queryForNodes('/properties/property', properties[0])
    assert (property is not None and len(property) == 2), repr(property)
    seen = set()
    for p in property:
        seen.add(p['name'])
        if p['name'] == 'color':
            assert p['type'] == 'str'
            assert str(p) == '#ffff00,#00ffff'
        elif p['name'] == 'private':
            assert p['type'] == 'bool'
            assert str(p) == '1'
        else:
            assert False, 'Unexpected property %s' % p['name']
    assert 'color' in seen, seen
    assert 'private' in seen, seen

    event = q.expect('stream-message', to='chat@conf.localhost')
    message = event.stanza

    x = xpath.queryForNodes('/message/x', message)
    assert (x is not None and len(x) == 1), repr(x)
    assert x[0].uri == 'http://jabber.org/protocol/muc#user'

    invites = xpath.queryForNodes('/x/invite', x[0])
    assert (invites is not None and len(invites) == 1), repr(invites)
    assert invites[0]['to'] == 'alice@localhost'

    reasons = xpath.queryForNodes('/invite/reason', invites[0])
    assert (reasons is not None and len(reasons) == 1), repr(reasons)
    assert str(reasons[0]) == 'I want to test invitations'

    call_async(q, act_prop_iface, 'SetProperties',
        room_handle, {'color': '#f00baa,#f00baa', 'private': True})

    event = q.expect('stream-message', to='alice@localhost')
    message = event.stanza

    properties = xpath.queryForNodes('/message/properties', message)
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == 'http://laptop.org/xmpp/activity-properties'
    assert properties[0]['room'] == 'chat@conf.localhost'
    assert properties[0]['activity'] == 'foo_id'

    property = xpath.queryForNodes('/properties/property', properties[0])
    assert (property is not None and len(property) == 2), repr(property)
    seen = set()
    for p in property:
        seen.add(p['name'])
        if p['name'] == 'color':
            assert p['type'] == 'str'
            assert str(p) == '#f00baa,#f00baa'
        elif p['name'] == 'private':
            assert p['type'] == 'bool'
            assert str(p) == '1'
        else:
            assert False, 'Unexpected property %s' % p['name']
    assert 'color' in seen, seen
    assert 'private' in seen, seen

    q.expect('dbus-return', method='SetProperties')

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
