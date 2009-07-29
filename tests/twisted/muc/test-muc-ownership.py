
"""
Test support for the HANDLE_OWNERS_NOT_AVAILABLE group flag, and calling
GetHandleOwners on MUC members.

By default, MUC channels should have the flag set. The flag should be unset
when presence is received that includes the MUC JID's owner JID.
"""

import dbus

from gabbletest import make_result_iq, exec_test, make_muc_presence
from servicetest import call_async, EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Need to call this asynchronously as it involves Gabble sending us a
    # query
    call_async(q, conn, 'RequestHandles', 2, ['chat@conf.localhost'])

    event = q.expect('stream-iq', to='conf.localhost',
        query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    room_handle = event.value[0][0]

    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TEXT, cs.HT_ROOM,
        room_handle, True)

    gfc, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupFlagsChanged'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))
    assert gfc.args[1] == 0

    event = q.expect('dbus-signal', signal='GroupFlagsChanged')
    assert event.args == [0, 1]

    # Send presence for anonymous other member of room.
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for anonymous other member of room (2)
    stream.send(make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'brian'))

    # Send presence for nonymous other member of room.
    stream.send(make_muc_presence('none', 'participant', 'chat@conf.localhost',
        'che', 'che@foo.com'))

    # Send presence for nonymous other member of room (2)
    stream.send(make_muc_presence('none', 'participant', 'chat@conf.localhost',
        'chris', 'chris@foo.com'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', 'chat@conf.localhost', 'test'))

    event = q.expect('dbus-signal', signal='GroupFlagsChanged')
    # Since we received MUC presence that contains an owner JID, the
    # OWNERS_NOT_AVAILABLE flag should be removed.
    assert event.args == [0, 1024]

    event = q.expect('dbus-signal', signal='HandleOwnersChanged',
        args=[{2: 1, 3: 0, 4: 0, 5: 6, 7: 8}, []])

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [2, 3, 4, 5, 7], [], [], [], 0, 0])
    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/test']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']
    assert conn.InspectHandles(1, [4]) == ['chat@conf.localhost/brian']
    assert conn.InspectHandles(1, [5]) == ['chat@conf.localhost/che']
    assert conn.InspectHandles(1, [6]) == ['che@foo.com']
    assert conn.InspectHandles(1, [7]) == ['chat@conf.localhost/chris']
    assert conn.InspectHandles(1, [8]) == ['chris@foo.com']

    event = q.expect('dbus-return', method='RequestChannel')

    bus = dbus.SessionBus()
    chan = bus.get_object(conn.bus_name, event.value[0])
    group = dbus.Interface(chan, cs.CHANNEL_IFACE_GROUP)
    props = dbus.Interface(chan, dbus.PROPERTIES_IFACE)

    # Exercise GetHandleOwners
    assert group.GetHandleOwners([5, 7]) == [6, 8]

    # Exercise D-Bus properties
    all = props.GetAll(cs.CHANNEL_IFACE_GROUP)

    assert all[u'LocalPendingMembers'] == [], all
    assert all[u'Members'] == [2, 3, 4, 5, 7], all
    assert all[u'RemotePendingMembers'] == [], all
    assert all[u'SelfHandle'] == 2, all
    assert all[u'HandleOwners'] == { 2: 1, 3: 0, 4: 0, 5: 6, 7: 8 }, all
    assert (all[u'GroupFlags'] & 2048) == 2048, all.get('GroupFlags')

if __name__ == '__main__':
    exec_test(test)
