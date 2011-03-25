"""
Test gabble trying alternative nicknames when the nick you wanted is already in
use in a MUC you try to join.
"""

import dbus

from gabbletest import (
    exec_test, make_muc_presence, sync_stream, elem,
    )
from servicetest import (
    call_async, unwrap, sync_dbus, assertEquals, assertSameSets, wrap_channel,
    )
import constants as cs
import ns

def test(q, bus, conn, stream):
    test_join(q, bus, conn, stream, 'chat@conf.localhost', False)
    test_join(q, bus, conn, stream, 'chien@conf.localhost', True)

def test_join(q, bus, conn, stream, room_jid, transient_conflict):
    """
    Tells Gabble to join a MUC, but make the first nick it tries conflict with
    an existing member of the MUC.  If transient_conflict is True, then when
    Gabble successfully joins with a different nick the originally conflicting
    user turns out not actually to be in the room (they left while we were
    retrying).
    """
    # Implementation detail: Gabble uses the first part of your jid (if you
    # don't have an alias) as your room nickname, and appends an underscore a
    # few times before giving up.
    member, member_ = [room_jid + '/' + x for x in ['test', 'test_']]

    call_async(q, conn.Requests, 'CreateChannel',
        dbus.Dictionary({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
                          cs.TARGET_ID: room_jid,
                        }, signature='sv'))

    # Gabble first tries to join as test
    q.expect('stream-presence', to=member)

    # MUC says no: there's already someone called test in room_jid
    presence = elem('presence', from_=member, type='error')(
        elem(ns.MUC, 'x'),
        elem('error', type='cancel')(
          elem(ns.STANZA, 'conflict'),
        ))
    stream.send(presence)

    # Gabble tries again as test_
    q.expect('stream-presence', to=member_)

    # MUC says yes!

    if not transient_conflict:
        # Send the other member of the room's presence. This is the nick we
        # originally wanted.
        stream.send(make_muc_presence('owner', 'moderator', room_jid, 'test'))

    # If gabble erroneously thinks the other user's presence is our own, it'll
    # think that it's got the whole userlist now. If so, syncing here will make
    # CreateChannel incorrectly return here.
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'test_'))

    # Only now should we have finished joining the room.
    event = q.expect('dbus-return', method='CreateChannel')
    path, props = event.value
    text_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')
    group_props = unwrap(text_chan.Properties.GetAll(cs.CHANNEL_IFACE_GROUP))

    t, t_ = conn.RequestHandles(cs.HT_CONTACT, [member, member_])

    # Check that Gabble think our nickname in the room is test_, not test
    muc_self_handle = group_props['SelfHandle']
    assert muc_self_handle == t_, (muc_self_handle, t_, t)

    members = group_props['Members']

    if transient_conflict:
        # The user we originally conflicted with isn't actually here; check
        # there's exactly one member (test_).
        assert members == [t_], (members, t_, t)
    else:
        # Check there are exactly two members (test and test_)
        assertSameSets([t, t_], members)

    # In either case, there should be no pending members.
    assert len(group_props['LocalPendingMembers']) == 0, group_props
    assert len(group_props['RemotePendingMembers']) == 0, group_props

    # Check that test_'s handle owner is us, and that test (if it's there) has
    # no owner.
    handle_owners = group_props['HandleOwners']
    assertEquals (conn.GetSelfHandle(), handle_owners[t_])
    if not transient_conflict:
        assertEquals (0, handle_owners[t])

    # test that closing the channel results in an unavailable message to the
    # right jid
    text_chan.Close()

    event = q.expect('stream-presence', to=member_)
    assertEquals('unavailable', event.stanza['type'])

if __name__ == '__main__':
    exec_test(test)
