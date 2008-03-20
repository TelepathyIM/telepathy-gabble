"""
Test OLPC MUC properties.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

    data['buddy_iface'] = dbus.Interface(data['conn'],
            'org.laptop.Telepathy.BuddyInfo')
    data['act_prop_iface'] = dbus.Interface(data['conn'],
            'org.laptop.Telepathy.ActivityProperties')
    data['bob_handle'] = data['conn_iface'].RequestHandles(1,
        ['bob@localhost'])[0]

    # Bob invites us to a chatroom, pre-seeding properties
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'bob@localhost'
    message['to'] = 'test@localhost'
    properties = message.addElement(
        ('http://laptop.org/xmpp/activity-properties', 'properties'))
    properties['room'] = 'chat@conf.localhost'
    properties['activity'] = 'foo_id'
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'title'
    property.addContent('From the invitation')
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
    data['chan_iface'] = dbus.Interface(data['text_chan'],
        'org.freedesktop.Telepathy.Channel')
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
    call_async(data['test'], data['act_prop_iface'], 'GetProperties',
        data['room_handle'])

    return True

@lazy
@match('dbus-return', method='GetProperties')
def expect_get_properties_return(event, data):
    assert event.value == ({'title': 'From the invitation', 'private': True},)

    # Now Bob changes the properties

    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'bob@localhost'
    message['to'] = 'test@localhost'
    properties = message.addElement(
        ('http://laptop.org/xmpp/activity-properties', 'properties'))
    properties['room'] = 'chat@conf.localhost'
    properties['activity'] = 'foo_id'
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'title'
    property.addContent('Mushroom, mushroom')
    property = properties.addElement((None, 'property'))
    property['type'] = 'bool'
    property['name'] = 'private'
    property.addContent('0')

    data['stream'].send(message)

    return True

@lazy
@match('dbus-signal', signal='ActivityPropertiesChanged')
def expect_activity_properties_changed(event, data):
    assert event.args == [data['room_handle'], {'title': 'Mushroom, mushroom',
        'private': False }]
    assert data['act_prop_iface'].GetProperties(data['room_handle']) == \
            event.args[1]

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
    # Now that it's not private, it'll go in my PEP
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)
    return True

@match('dbus-return', method='SetActivities')
def expect_set_activities_success(event, data):

    # Bob changes the properties and tells the room he's done so
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'chat@conf.localhost/bob'
    message['to'] = 'chat@conf.localhost'
    properties = message.addElement(
        ('http://laptop.org/xmpp/activity-properties', 'properties'))
    properties['activity'] = 'foo_id'
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'title'
    property.addContent('Badger badger badger')
    property = properties.addElement((None, 'property'))
    property['type'] = 'bool'
    property['name'] = 'private'
    property.addContent('0')

    data['stream'].send(message)

    return True

@match('stream-iq', iq_type='set')
def expect_bob_activity_properties_copied_to_mine(event, data):

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == 'http://laptop.org/xmpp/activity-properties'

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == 'http://laptop.org/xmpp/activity-properties'
    assert properties[0]['room'] == 'chat@conf.localhost'
    assert properties[0]['activity'] == 'foo_id'

    property = xpath.queryForNodes('/properties/property', properties[0])
    assert (property is not None and len(property) == 2), repr(property)
    seen = set()
    for p in property:
        seen.add(p['name'])
        if p['name'] == 'title':
            assert p['type'] == 'str'
            assert str(p) == 'Badger badger badger'
        elif p['name'] == 'private':
            assert p['type'] == 'bool'
            assert str(p) == '0'
        else:
            assert False, 'Unexpected property %s' % p['name']
    assert 'title' in seen, seen
    assert 'private' in seen, seen

    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)

    data['act_prop_iface'] = dbus.Interface(data['conn'],
            'org.laptop.Telepathy.ActivityProperties')

    # test sets the title and sets private back to True
    call_async(data['test'], data['act_prop_iface'], 'SetProperties',
        data['room_handle'], {'title': 'I can set the properties too',
                              'private': True})
    return True

@match('stream-message')
def expect_properties_changed_broadcast(event, data):
    message = event.stanza
    if message['to'] != 'chat@conf.localhost':
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
        if p['name'] == 'title':
            assert p['type'] == 'str'
            assert str(p) == 'I can set the properties too'
        elif p['name'] == 'private':
            assert p['type'] == 'bool'
            assert str(p) == '1'
        else:
            assert False, 'Unexpected property %s' % p['name']
    assert 'title' in seen, seen
    assert 'private' in seen, seen

    return True

@match('stream-iq', iq_type='set')
def expect_activity_properties_unpublication(event, data):
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == 'http://laptop.org/xmpp/activity-properties'

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert properties is None, repr(properties)

    return True

@match('stream-iq', iq_type='set')
def expect_activity_unpublication(event, data):
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == 'http://laptop.org/xmpp/activities'

    activity = xpath.queryForNodes('/activities/activity', activities[0])
    assert activity is None, repr(activity)

    return True

@match('dbus-return', method='SetProperties')
def expect_set_activity_props_success(event, data):

    # test sets the title and sets private back to True
    call_async(data['test'], data['act_prop_iface'], 'SetProperties',
        data['room_handle'], {'title': 'I can set the properties too',
                              'private': False})
    return True

@match('stream-message')
def expect_properties_changed_broadcast2(event, data):
    message = event.stanza
    if message['to'] != 'chat@conf.localhost':
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
        if p['name'] == 'title':
            assert p['type'] == 'str'
            assert str(p) == 'I can set the properties too'
        elif p['name'] == 'private':
            assert p['type'] == 'bool'
            assert str(p) == '0'
        else:
            assert False, 'Unexpected property %s' % p['name']
    assert 'title' in seen, seen
    assert 'private' in seen, seen

    return True

@match('stream-iq', iq_type='set')
def expect_activity_properties_republication(event, data):
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == 'http://laptop.org/xmpp/activity-properties'

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == 'http://laptop.org/xmpp/activity-properties'
    assert properties[0]['room'] == 'chat@conf.localhost'
    assert properties[0]['activity'] == 'foo_id'

    property = xpath.queryForNodes('/properties/property', properties[0])
    assert (property is not None and len(property) == 2), repr(property)
    seen = set()
    for p in property:
        seen.add(p['name'])
        if p['name'] == 'title':
            assert p['type'] == 'str'
            assert str(p) == 'I can set the properties too'
        elif p['name'] == 'private':
            assert p['type'] == 'bool'
            assert str(p) == '0'
        else:
            assert False, 'Unexpected property %s' % p['name']
    assert 'title' in seen, seen
    assert 'private' in seen, seen

    return True

@match('stream-iq', iq_type='set')
def expect_activity_republication(event, data):
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == 'http://laptop.org/xmpp/activities'

    activity = xpath.queryForNodes('/activities/activity', activities[0])
    assert (activity is not None and len(activity) == 1), repr(activity)
    assert activity[0]['room'] == 'chat@conf.localhost'
    assert activity[0]['type'] == 'foo_id'                  # sic

    return True

@match('dbus-return', method='SetProperties')
def expect_set_activity_props_success2(event, data):

    data['chan_iface'].Close()
    return True

@match('stream-iq', iq_type='set')
def expect_activity_unpublication2(event, data):
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == 'http://laptop.org/xmpp/activities'

    activity = xpath.queryForNodes('/activities/activity', activities[0])
    assert activity is None, repr(activity)

    return True

@match('stream-iq', iq_type='set')
def expect_activity_properties_unpublication2(event, data):
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    data['stream'].send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == 'http://laptop.org/xmpp/activity-properties'

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert properties is None, repr(properties)

    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

