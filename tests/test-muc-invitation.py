"""
Test MUC invitations.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

    # Bob has invited us to an activity.
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

    # accept the invitation
    call_async(data['test'], data['group_iface'], 'AddMembers',
        [data['room_self_handle']], 'Oh, OK then')

    return True

@match('dbus-signal', signal='MembersChanged')
def expect_add_myself_into_remote_pending(event, data):
    assert event.args == ['', [], [data['bob_handle']], [],
            [data['room_self_handle']], 0,
            data['room_self_handle']]
    return True

@match('dbus-return', method='AddMembers')
def expect_add_myself_success(event, data):
    return True

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

@match('dbus-signal', signal='MembersChanged')
def expect_members_changed2(event, data):
    assert event.args == ['', [data['room_self_handle']], [], [],
            [], 0, 0]

    # Test sending an invitation
    data['alice_handle'] = data['conn_iface'].RequestHandles(1,
        ['alice@localhost'])[0]
    call_async(data['test'], data['group_iface'], 'AddMembers',
        [data['alice_handle']], 'I want to test invitations')
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

    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

