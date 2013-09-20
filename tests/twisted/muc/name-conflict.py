# vim: fileencoding=utf-8 :
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
    EventPattern,
    )
import constants as cs
import ns

def test(q, bus, conn, stream):
    test_join(q, bus, conn, stream, 'chat@conf.localhost', False)
    test_join(q, bus, conn, stream, 'chien@conf.localhost', True)

    test_gtalk_weirdness(q, bus, conn, stream,
        'private-chat-massive-uuid@groupchat.google.com')

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
    assertEquals (conn.Properties.Get(cs.CONN, "SelfHandle"), handle_owners[t_])
    if not transient_conflict:
        assertEquals (0, handle_owners[t])

    # test that closing the channel results in an unavailable message to the
    # right jid
    text_chan.Close()

    event = q.expect('stream-presence', to=member_)
    assertEquals('unavailable', event.stanza['type'])

def test_gtalk_weirdness(q, bus, conn, stream, room_jid):
    """
    There's a strange bug in the Google Talk MUC server where it sends the
    <conflict/> stanza twice. This has been reported to their server team; but
    in any case it triggered a crazy bug in Gabble, so here's a regression test.
    """

    # Implementation detail: Gabble uses the first part of your jid (if you
    # don't have an alias) as your room nickname, and appends an underscore a
    # few times before giving up.
    jids = ['%s/test%s' % (room_jid, x) for x in ['', '_', '__']]
    member, member_, member__ = jids

    # Gabble should never get as far as trying to join as 'test__' since
    # joining as 'test_' will succeed.
    q.forbid_events([ EventPattern('stream-presence', to=member__) ])

    call_async(q, conn.Requests, 'CreateChannel',
        dbus.Dictionary({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
                          cs.TARGET_ID: room_jid,
                        }, signature='sv'))

    # Gabble first tries to join as test
    q.expect('stream-presence', to=member)

    # Google Talk says no from 'test', twice.
    presence = elem('presence', from_=member, type='error')(
        elem(ns.MUC, 'x'),
        elem('error', type='cancel')(
          elem(ns.STANZA, 'conflict'),
        ))
    stream.send(presence)
    stream.send(presence)

    # Gabble should try to join again as test_
    q.expect('stream-presence', to=member_)

    # Since 'test_' is not in use in the MUC, joining should succeed. According
    # to XEP-0045 §7.1.3 <http://xmpp.org/extensions/xep-0045.html#enter-pres>:
    #  The service MUST first send the complete list of the existing occupants
    #  to the new occupant and only then send the new occupant's own presence
    #  to the new occupant
    # but groupchat.google.com cheerfully violates this.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'test_'))

    # Here's some other random person, who owns the MUC.
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'foobar_gmail.com'))
    # And here's our hypothetical other self.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'test'))

    # The Gabble bug makes this time out: because Gabble thinks it's joining as
    # test__ it ignores the presence for test_, since it's not flagged with
    # code='210' to say “this is you”. (This is acceptable behaviour by the
    # server: it only needs to include code='210' if it's assigned the client a
    # name other than the one it asked for.
    #
    # The forbidden stream-presence event above doesn't blow up here because
    # servicetest doesn't process events on the 'stream-*' queue at all when
    # we're not waiting for one. But during disconnection in the test clean-up,
    # the forbidden event is encountered and correctly flagged up.
    event = q.expect('dbus-return', method='CreateChannel')
    path, _ = event.value
    text_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')

    # As far as Gabble's concerned, the two other participants joined
    # immediately after we did.  We can't request handles for them before we
    # try to join the MUC, because until we do so, Gabble doesn't know that
    # room_jid is a MUC, and so considers these three JIDs to be different
    # resources of the same contact. There is no race between this method
    # returning and MembersChangedDetailed firing, because libdbus reorders
    # messages when you make blocking calls.
    handle, handle_, handle__, foobar_handle = conn.RequestHandles(
        cs.HT_CONTACT, jids + ['%s/foobar_gmail.com' % room_jid])

    q.expect('dbus-signal', signal='MembersChangedDetailed',
        predicate=lambda e: e.args[0:4] == [[foobar_handle], [], [], []])
    q.expect('dbus-signal', signal='MembersChangedDetailed',
        predicate=lambda e: e.args[0:4] == [[handle], [], [], []])

    group_props = text_chan.Properties.GetAll(cs.CHANNEL_IFACE_GROUP)
    assertEquals(handle_, group_props['SelfHandle'])
    assertSameSets([handle, handle_, foobar_handle], group_props['Members'])

if __name__ == '__main__':
    exec_test(test)
