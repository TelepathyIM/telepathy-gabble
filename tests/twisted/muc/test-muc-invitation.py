"""
Test MUC invitations.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import exec_test, make_muc_presence
from servicetest import call_async, EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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
    assert event.args[1] == cs.CHANNEL_TYPE_TEXT

    assert event.args[2] == 2   # handle type
    assert event.args[3] == 1   # handle
    room_handle = 1

    text_chan = bus.get_object(conn.bus_name, event.args[0])
    group_iface = dbus.Interface(text_chan, cs.CHANNEL_IFACE_GROUP)

    members = group_iface.GetMembers()
    local_pending = group_iface.GetLocalPendingMembers()
    remote_pending = group_iface.GetRemotePendingMembers()

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

    channel_props = text_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == 'chat@conf.localhost', channel_props
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == 'bob@localhost'
    assert channel_props['InitiatorHandle'] == bob_handle

    # set ourselves to away and back again, to check that we don't send any
    # presence to the MUC before the invite has been accepted
    conn.Presence.SetStatus({'away':{'message':'failure'}})
    conn.Presence.SetStatus({'available':{'message':'success'}})

    # accept the invitation
    call_async(q, group_iface, 'AddMembers', [room_self_handle], 'Oh, OK then')

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
    assert event2.args == ['', [], [bob_handle], [],
            [room_self_handle], 0, cs.GC_REASON_INVITED]

    # Send presence for Bob's membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    event = q.expect('dbus-signal', signal='MembersChanged')

    room_bob_handle = conn.RequestHandles(cs.HT_CONTACT, ['chat@conf.localhost/bob'])[0]
    assert event.args == ['', [room_self_handle, room_bob_handle], [], [], [], 0, 0]

    # Test sending an invitation
    alice_handle = conn.RequestHandles(1, ['alice@localhost'])[0]
    call_async(q, group_iface, 'AddMembers', [alice_handle],
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
