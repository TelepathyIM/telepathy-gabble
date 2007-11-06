"""
Test OLPC extensions to MUC invitations.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

    data['bob_handle'] = data['conn_iface'].RequestHandles(1,
        ['bob@localhost'])[0]

    data['buddy_iface'] = dbus.Interface(data['conn'],
            'org.laptop.Telepathy.BuddyInfo')
    call_async(data['test'], data['buddy_iface'], 'GetActivities',
        data['bob_handle'])

    return True

@match('stream-iq', iq_type='get', to='bob@localhost')
def expect_get_bob_activities_iq_get(event, data):
    # Bob has no activities
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'bob@localhost'
    data['stream'].send(event.stanza)
    return True

@match('dbus-return', method='GetActivities')
def expect_get_bob_activities_return(event, data):
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

    data['stream'].send(message)

    message = domish.Element((None, 'message'))
    message['from'] = 'chat@conf.localhost'
    message['to'] = 'test@localhost'
    x = message.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    invite = x.addElement((None, 'invite'))
    invite['from'] = 'bob@localhost'
    reason = invite.addElement((None, 'reason'))
    reason.addContent('No good reason')

    data['stream'].send(message)

    return True

@match('dbus-signal', signal='NewChannel')
def expect_text_channel(event, data):
    if event.args[1] != 'org.freedesktop.Telepathy.Channel.Type.Text':
        return False

    assert event.args[2] == 2   # handle type
    assert event.args[3] == 1   # handle
    data['room_handle'] = 1

    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn'].bus_name, event.args[0])
    data['group_iface'] = dbus.Interface(data['text_chan'],
        'org.freedesktop.Telepathy.Channel.Interface.Group')

    members = data['group_iface'].GetAllMembers()[0]
    local_pending = data['group_iface'].GetAllMembers()[1]
    remote_pending = data['group_iface'].GetAllMembers()[2]

    assert len(members) == 1
    assert data['conn_iface'].InspectHandles(1, members)[0] == 'bob@localhost'
    data['bob_handle'] = members[0]
    assert len(local_pending) == 1
    # FIXME: the username-part-is-nickname assumption
    assert data['conn_iface'].InspectHandles(1, local_pending)[0] == \
            'chat@conf.localhost/test'
    assert len(remote_pending) == 0

    data['room_self_handle'] = data['group_iface'].GetSelfHandle()
    assert data['room_self_handle'] == local_pending[0]

    # by now, we should have picked up the extra activity properties
    data['buddy_iface'] = dbus.Interface(data['conn'],
            'org.laptop.Telepathy.BuddyInfo')
    call_async(data['test'], data['buddy_iface'], 'GetActivities',
        data['bob_handle'])

    return True

@match('stream-iq', iq_type='get', to='bob@localhost')
def expect_get_bob_activities_iq_get_again(event, data):
    # Bob still has no (public) activities
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'bob@localhost'
    data['stream'].send(event.stanza)
    return True

@lazy
@match('dbus-return', method='GetActivities')
def expect_get_bob_activities_return_again(event, data):
    assert event.value == ([('foo_id', data['room_handle'])],)

    # OK, now accept the invitation
    call_async(data['test'], data['group_iface'], 'AddMembers',
        [data['room_self_handle']], 'Oh, OK then')

    return True

@lazy
@match('dbus-signal', signal='MembersChanged')
def expect_add_myself_into_remote_pending(event, data):
    assert event.args == ['', [], [data['bob_handle']], [],
            [data['room_self_handle']], 0,
            data['room_self_handle']]
    return True

@lazy
@match('stream-presence', to='chat@conf.localhost/test')
def expect_presence(event, data):
    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    data['stream'].send(presence)
    return True

@match('dbus-return', method='AddMembers')
def expect_add_myself_success(event, data):
    return True

@match('dbus-signal', signal='MembersChanged')
def expect_members_changed2(event, data):
    assert event.args == ['', [data['room_self_handle']], [], [],
            [], 0, 0]

    call_async(data['test'], data['buddy_iface'], 'SetActivities',
        [('foo_id', data['room_handle'])])
    return True

@match('stream-iq', iq_type='set')
def expect_activities_publication(event, data):
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)
    return True

@match('dbus-return', method='SetActivities')
def expect_set_activities_success(event, data):
    data['act_prop_iface'] = dbus.Interface(data['conn'],
            'org.laptop.Telepathy.ActivityProperties')
    call_async(data['test'], data['act_prop_iface'], 'SetProperties',
        data['room_handle'], {'color': '#ffff00,#00ffff', 'private': True})
    return True

@match('dbus-return', method='SetProperties')
def expect_set_activity_props_success(event, data):

    # Test sending an invitation
    data['alice_handle'] = data['conn_iface'].RequestHandles(1,
        ['alice@localhost'])[0]
    call_async(data['test'], data['group_iface'], 'AddMembers',
        [data['alice_handle']], 'I want to test invitations')
    return True

@match('stream-message')
def expect_act_props_pseudo_invite(event, data):
    message = event.stanza
    if message['to'] != 'alice@localhost':
        return False

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

    return True

@match('stream-message')
def expect_invitation(event, data):
    message = event.stanza
    if message['to'] != 'chat@conf.localhost':
        return False

    x = xpath.queryForNodes('/message/x', message)
    assert (x is not None and len(x) == 1), repr(x)
    assert x[0].uri == 'http://jabber.org/protocol/muc#user'

    invites = xpath.queryForNodes('/x/invite', x[0])
    assert (invites is not None and len(invites) == 1), repr(invites)
    assert invites[0]['to'] == 'alice@localhost'

    reasons = xpath.queryForNodes('/invite/reason', invites[0])
    assert (reasons is not None and len(reasons) == 1), repr(reasons)
    assert str(reasons[0]) == 'I want to test invitations'

    call_async(data['test'], data['act_prop_iface'], 'SetProperties',
        data['room_handle'], {'color': '#f00baa,#f00baa', 'private': True})
    return True

@match('stream-message')
def expect_act_props_refresh_pseudo_invite(event, data):
    message = event.stanza
    if message['to'] != 'alice@localhost':
        return False

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

    return True

@match('dbus-return', method='SetProperties')
def expect_set_activity_props_success2(event, data):

    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

