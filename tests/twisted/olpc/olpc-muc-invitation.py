"""
Test OLPC extensions to MUC invitations.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import exec_test, make_muc_presence
from servicetest import call_async, EventPattern
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    handles = {}
    handles['bob'] = conn.RequestHandles(1, ['bob@localhost'])[0]

    buddy_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    act_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.ActivityProperties')
    call_async(q, buddy_iface, 'GetActivities', handles['bob'])

    event = q.expect('stream-iq', iq_type='get', to='bob@localhost')
    # Bob has no activities
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'bob@localhost'
    stream.send(event.stanza)

    event = q.expect('dbus-return', method='GetActivities')
    # initially, Bob has no activities
    assert event.value == ([],)

    # Bob sends an activity properties message
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'bob@localhost'
    message['to'] = 'test@localhost'
    properties = message.addElement((ns.OLPC_ACTIVITY_PROPS, 'properties'))
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

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    handles['chat'], props = event.args
    assert props == {'color': '#ffff00,#00ffff', 'private' : True}

    event = q.expect('dbus-signal', signal='ActivitiesChanged')
    assert event.args[0] == handles['bob']
    acts = event.args[1]
    assert len(acts) == 1
    assert acts[0] == ('foo_id', handles['chat'])

    props = act_prop_iface.GetProperties(handles['chat'])
    assert props == {'color': '#ffff00,#00ffff', 'private' : True}

    # Bobs invites us to the activity
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
    assert event.args[3] == handles['chat']   # handle

    text_chan = bus.get_object(conn.bus_name, event.args[0])
    group_iface = dbus.Interface(text_chan, cs.CHANNEL_IFACE_GROUP)

    members = group_iface.GetAllMembers()[0]
    local_pending = group_iface.GetAllMembers()[1]
    remote_pending = group_iface.GetAllMembers()[2]

    assert len(members) == 1
    assert conn.InspectHandles(1, members)[0] == 'bob@localhost'
    assert len(local_pending) == 1
    # FIXME: the username-part-is-nickname assumption
    assert conn.InspectHandles(1, local_pending)[0] == \
            'chat@conf.localhost/test'
    assert len(remote_pending) == 0

    handles['chat_self'] = group_iface.GetSelfHandle()
    assert handles['chat_self'] == local_pending[0]

    # by now, we should have picked up the extra activity properties
    call_async(q, buddy_iface, 'GetActivities', handles['bob'])

    event = q.expect('stream-iq', iq_type='get', to='bob@localhost')
    # Bob still has no (public) activities
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'bob@localhost'
    stream.send(event.stanza)

    event = q.expect('dbus-return', method='GetActivities')

    assert event.value == ([('foo_id', handles['chat'])],)

    # OK, now accept the invitation
    call_async(q, group_iface, 'AddMembers', [handles['chat_self']], 'Oh, OK then')

    _, event, _ = q.expect_many(
        EventPattern('stream-presence', to='chat@conf.localhost/test'),
        EventPattern('dbus-signal', signal='MembersChanged'),
        EventPattern('dbus-return', method='AddMembers')
        )

    assert event.args == ['', [], [handles['bob']], [],
            [handles['chat_self']], 0, cs.GC_REASON_INVITED]

    # Send presence for own membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    event = q.expect('dbus-signal', signal='MembersChanged')
    assert event.args == ['', [handles['chat_self']], [], [], [], 0, 0]

    call_async(q, buddy_iface, 'SetActivities', [('foo_id', handles['chat'])])

    event = q.expect('stream-iq', iq_type='set')
    event.stanza['type'] = 'result'
    event.stanza['to'] = 'test@localhost'
    event.stanza['from'] = 'test@localhost'
    stream.send(event.stanza)

    q.expect('dbus-return', method='SetActivities')
    call_async(q, act_prop_iface, 'SetProperties',
        handles['chat'], {'color': '#ffff00,#00ffff', 'private': True})

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    chat_handle, props = event.args
    assert chat_handle == handles['chat']
    assert props == {'color': '#ffff00,#00ffff', 'private' : True}

    q.expect('dbus-return', method='SetProperties')
    # Test sending an invitation
    handles['alice'] = conn.RequestHandles(1,
        ['alice@localhost'])[0]
    call_async(q, group_iface, 'AddMembers', [handles['alice']],
            'I want to test invitations')

    event = q.expect('stream-message', to='alice@localhost')
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
    assert x[0].uri == ns.MUC_USER

    invites = xpath.queryForNodes('/x/invite', x[0])
    assert (invites is not None and len(invites) == 1), repr(invites)
    assert invites[0]['to'] == 'alice@localhost'

    reasons = xpath.queryForNodes('/invite/reason', invites[0])
    assert (reasons is not None and len(reasons) == 1), repr(reasons)
    assert str(reasons[0]) == 'I want to test invitations'

    call_async(q, act_prop_iface, 'SetProperties',
        handles['chat'], {'color': '#f00baa,#f00baa', 'private': True})

    event, apc_event, _ = q.expect_many(
        EventPattern('stream-message', to='alice@localhost'),
        EventPattern('dbus-signal', signal='ActivityPropertiesChanged'),
        EventPattern('dbus-return', method='SetProperties'),
        )
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

    chat_handle, props = apc_event.args
    assert chat_handle == handles['chat']
    assert props == {'color': '#f00baa,#f00baa', 'private' : True}

if __name__ == '__main__':
    exec_test(test)
