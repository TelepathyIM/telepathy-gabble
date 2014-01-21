"""
Test MUC invitations.
"""

from twisted.words.xish import domish, xpath

from gabbletest import exec_test, make_muc_presence
from servicetest import call_async, EventPattern, wrap_channel, assertEquals
import constants as cs

def test(q, bus, conn, stream):
    # Bob has invited us to an activity.
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
    path, props = event.args
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(1, props[cs.TARGET_HANDLE])

    text_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')

    members = text_chan.Properties.Get(cs.CHANNEL_IFACE_GROUP, 'Members')
    local_pending = text_chan.Properties.Get(cs.CHANNEL_IFACE_GROUP, 'LocalPendingMembers')
    remote_pending = text_chan.Properties.Get(cs.CHANNEL_IFACE_GROUP, 'RemotePendingMembers')

    assert len(members) == 1
    assert conn.inspect_contact_sync(members[0]) == 'bob@localhost'
    bob_handle = members[0]
    assert len(local_pending) == 1
    # FIXME: the username-part-is-nickname assumption
    assert conn.inspect_contact_sync(local_pending[0][0]) == \
            'chat@conf.localhost/test'
    assert len(remote_pending) == 0

    room_self_handle = text_chan.Properties.Get(cs.CHANNEL_IFACE_GROUP,
            "SelfHandle")
    assert room_self_handle == local_pending[0][0]

    channel_props = text_chan.Properties.GetAll(cs.CHANNEL)
    assert channel_props['TargetID'] == 'chat@conf.localhost', channel_props
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == 'bob@localhost'
    assert channel_props['InitiatorHandle'] == bob_handle

    # set ourselves to away and back again, to check that we don't send any
    # presence to the MUC before the invite has been accepted
    conn.Presence.SetPresence('away', 'failure')
    conn.Presence.SetPresence('available', 'success')

    # accept the invitation
    call_async(q, text_chan.Group, 'AddMembers', [room_self_handle], 'Oh, OK then')

    event, event2, _ = q.expect_many(
            EventPattern('stream-presence', to='chat@conf.localhost/test'),
            EventPattern('dbus-signal', signal='MembersChanged'),
            EventPattern('dbus-return', method='AddMembers')
            )

    # check that the status we joined with was available / success
    elem = event.stanza
    show = [e for e in elem.elements() if e.name == 'show']
    assert not show
    status = [e for e in elem.elements() if e.name == 'status'][0]
    assert status
    assert status.children[0] == u'success'

    # We are added as remote pending while joining the room. The inviter (Bob)
    # is removed for now. It will be re-added with his channel specific handle
    # once we have joined.
    added, removed, local_pending, remote_pending, details = event2.args
    assertEquals([], added)
    assertEquals([bob_handle], removed)
    assertEquals([], local_pending)
    assertEquals([room_self_handle], remote_pending)
    assertEquals(cs.GC_REASON_INVITED, details['change-reason'])

    # Send presence for Bob's membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    event = q.expect('dbus-signal', signal='MembersChanged')

    room_bob_handle = conn.get_contact_handle_sync('chat@conf.localhost/bob')

    added, removed, local_pending, remote_pending, details = event.args
    assertEquals([room_self_handle, room_bob_handle], added)
    assertEquals([], removed)
    assertEquals([], local_pending)
    assertEquals([], remote_pending)

    # Test sending an invitation
    alice_handle = conn.get_contact_handle_sync('alice@localhost')
    call_async(q, text_chan.Group, 'AddMembers', [alice_handle],
            'I want to test invitations')

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

if __name__ == '__main__':
    exec_test(test)
