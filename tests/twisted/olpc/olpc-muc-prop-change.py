"""
Test OLPC MUC properties.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import exec_test, acknowledge_iq, make_muc_presence
from servicetest import call_async, EventPattern
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    buddy_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    act_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.ActivityProperties')
    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    # Bob invites us to a chatroom, pre-seeding properties
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'bob@localhost'
    message['to'] = 'test@localhost'
    properties = message.addElement(
        (ns.OLPC_ACTIVITY_PROPS, 'properties'))
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

    stream.send(message)

    message = domish.Element((None, 'message'))
    message['from'] = 'chat@conf.localhost'
    message['to'] = 'test@localhost'
    x = message.addElement((ns.MUC_USER, 'x'))
    invite = x.addElement((None, 'invite'))
    invite['from'] = 'bob@localhost'
    reason = invite.addElement((None, 'reason'))
    reason.addContent('No good reason')

    stream.send(message)

    event = q.expect('dbus-signal', signal='NewChannel')

    assert event.args[1] == cs.CHANNEL_TYPE_TEXT

    assert event.args[2] == 2   # handle type
    assert event.args[3] == 1   # handle
    room_handle = 1

    text_chan = bus.get_object(conn.bus_name, event.args[0])
    chan_iface = dbus.Interface(text_chan, cs.CHANNEL)
    group_iface = dbus.Interface(text_chan, cs.CHANNEL_IFACE_GROUP)

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

    props = act_prop_iface.GetProperties(room_handle)
    assert len(props) == 2
    assert props['title'] == 'From the invitation'
    assert props['private'] == True

    # Now Bob changes the properties
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'bob@localhost'
    message['to'] = 'test@localhost'
    properties = message.addElement(
        (ns.OLPC_ACTIVITY_PROPS, 'properties'))
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

    stream.send(message)

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')

    assert event.args == [room_handle, {'title': 'Mushroom, mushroom',
        'private': False }]
    assert act_prop_iface.GetProperties(room_handle) == \
            event.args[1]

    # OK, now accept the invitation
    call_async(q, group_iface, 'AddMembers', [room_self_handle], 'Oh, OK then')

    q.expect_many(
        EventPattern('stream-presence', to='chat@conf.localhost/test'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [], [bob_handle], [], [room_self_handle],
                0, cs.GC_REASON_INVITED])
            )

    # Send presence for own membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    q.expect('dbus-return', method='AddMembers')

    event = q.expect('dbus-signal', signal='MembersChanged')
    assert event.args == ['', [room_self_handle], [], [], [], 0, 0]

    call_async(q, buddy_iface, 'SetActivities', [('foo_id', room_handle)])

    event = q.expect('stream-iq', iq_type='set')
    # Now that it's not private, it'll go in my PEP
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    q.expect('dbus-return', method='SetActivities')

    # Bob changes the properties and tells the room he's done so
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'chat@conf.localhost/bob'
    message['to'] = 'chat@conf.localhost'
    properties = message.addElement(
        (ns.OLPC_ACTIVITY_PROPS, 'properties'))
    properties['activity'] = 'foo_id'
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'title'
    property.addContent('Badger badger badger')
    property = properties.addElement((None, 'property'))
    property['type'] = 'bool'
    property['name'] = 'private'
    property.addContent('0')

    stream.send(message)

    event = q.expect('stream-iq', iq_type='set')
    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == ns.OLPC_ACTIVITY_PROPS

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == ns.OLPC_ACTIVITY_PROPS
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
    stream.send(event.stanza)

    act_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.ActivityProperties')

    # test sets the title and sets private back to True
    call_async(q, act_prop_iface, 'SetProperties',
            room_handle, {'title': 'I can set the properties too', 'private': True})

    event = q.expect('stream-message', to='chat@conf.localhost')
    message = event.stanza

    properties = xpath.queryForNodes('/message/properties', message)
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == ns.OLPC_ACTIVITY_PROPS
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


    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == ns.OLPC_ACTIVITY_PROPS

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert properties is None, repr(properties)

    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == ns.OLPC_ACTIVITIES

    activity = xpath.queryForNodes('/activities/activity', activities[0])
    assert activity is None, repr(activity)

    q.expect('dbus-return', method='SetProperties')

    # test sets the title and sets private back to True
    call_async(q, act_prop_iface, 'SetProperties',
        room_handle, {'title': 'I can set the properties too',
                              'private': False})

    event = q.expect('stream-message', to='chat@conf.localhost')
    message = event.stanza

    properties = xpath.queryForNodes('/message/properties', message)
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == ns.OLPC_ACTIVITY_PROPS
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

    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == ns.OLPC_ACTIVITY_PROPS

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == ns.OLPC_ACTIVITY_PROPS
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

    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == ns.OLPC_ACTIVITIES

    activity = xpath.queryForNodes('/activities/activity', activities[0])
    assert (activity is not None and len(activity) == 1), repr(activity)
    assert activity[0]['room'] == 'chat@conf.localhost'
    assert activity[0]['type'] == 'foo_id'                  # sic

    q.expect('dbus-return', method='SetProperties')

    chan_iface.Close()

    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == ns.OLPC_ACTIVITIES

    activity = xpath.queryForNodes('/activities/activity', activities[0])
    assert activity is None, repr(activity)

    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    message = event.stanza

    activities = xpath.queryForNodes('/iq/pubsub/publish/item/activities',
            message)
    assert (activities is not None and len(activities) == 1), repr(activities)
    assert activities[0].uri == ns.OLPC_ACTIVITY_PROPS

    properties = xpath.queryForNodes('/activities/properties', activities[0])
    assert properties is None, repr(properties)

if __name__ == '__main__':
    exec_test(test)
