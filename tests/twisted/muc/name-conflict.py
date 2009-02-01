"""
Test gabble trying alternative nicknames when the nick you wanted is already in
use in a MUC you try to join.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import (
    exec_test, make_muc_presence, request_muc_handle, sync_stream
    )
from servicetest import call_async, EventPattern, unwrap, sync_dbus
from constants import (
    HT_CONTACT, HT_ROOM,
    CONN_IFACE_REQUESTS, CHANNEL_TYPE_TEXT, CHANNEL_IFACE_GROUP,
    CHANNEL_TYPE, TARGET_HANDLE_TYPE, TARGET_HANDLE,
    )
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    requests = dbus.Interface(conn, CONN_IFACE_REQUESTS)

    room_jid = 'chat@conf.localhost'
    room_handle = request_muc_handle(q, conn, stream, room_jid)
    # Implementation detail: Gabble uses the first part of your jid (if you
    # don't have an alias) as your room nickname, and appends an underscore a
    # few times before giving up.
    member, member_ = [room_jid + '/' + x for x in ['test', 'test_']]

    call_async(q, requests, 'CreateChannel',
        dbus.Dictionary({ CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
                          TARGET_HANDLE_TYPE: HT_ROOM,
                          TARGET_HANDLE: room_handle,
                        }, signature='sv'))

    # Gabble first tries to join as test
    q.expect('stream-presence', to=member)

    # MUC says no: there's already someone called test in chat@conf.localhost
    presence = domish.Element((None, 'presence'))
    presence['from'] = member
    presence['type'] = 'error'
    x = presence.addElement((ns.MUC, 'x'))
    error = presence.addElement((None, 'error'))
    error['type'] = 'cancel'
    error.addElement((ns.STANZA, 'conflict'))
    stream.send(presence)

    # Gabble tries again as test_
    q.expect('stream-presence', to=member_)

    # MUC says yes!

    # Send the other member of the room's presence. This is the nick we
    # originally wanted.
    stream.send(make_muc_presence(
        'owner', 'moderator', 'chat@conf.localhost', 'test'))

    # If gabble erroneously thinks the other user's presence is our own, it'll
    # think that it's got the whole userlist now. If so, syncing here will make
    # CreateChannel incorrectly return here.
    sync_stream(q, stream)
    sync_dbus(stream, q, conn)

    # Send presence for own membership of room.
    stream.send(make_muc_presence(
        'none', 'participant', 'chat@conf.localhost', 'test_'))

    # Only now should we have finished joining the room.
    event = q.expect('dbus-return', method='CreateChannel')
    path, props = event.value
    text_chan = bus.get_object(conn.bus_name, path)
    group_props = unwrap(text_chan.GetAll(CHANNEL_IFACE_GROUP,
        dbus_interface=dbus.PROPERTIES_IFACE))

    t, t_ = conn.RequestHandles(HT_CONTACT, [member, member_])

    # Check that Gabble think our nickname in the room is test_, not test
    muc_self_handle = group_props['SelfHandle']
    assert muc_self_handle == t_, (muc_self_handle, t_, t)

    # Check there are exactly two members (test and test_) and that there are
    # no pending members
    members = group_props['Members']
    assert sorted(members) == sorted([t, t_]), (members, [t, t_])
    assert len(group_props['LocalPendingMembers']) == 0, group_props
    assert len(group_props['RemotePendingMembers']) == 0, group_props

    # Check that test_'s handle owner is us, and that test has no owner.
    handle_owners = group_props['HandleOwners']
    assert handle_owners[t_] == self_handle, \
        (handle_owners, t_, handle_owners[t_], self_handle)
    assert handle_owners[t] == 0, (handle_owners, t)

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True

if __name__ == '__main__':
    exec_test(test)
