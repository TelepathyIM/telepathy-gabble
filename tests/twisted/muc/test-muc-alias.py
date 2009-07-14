"""
Test that our alias is used to create MUC JIDs.
"""

from gabbletest import exec_test, make_muc_presence, request_muc_handle, \
    expect_and_handle_get_vcard, expect_and_handle_set_vcard
from servicetest import call_async, EventPattern

import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()

    expect_and_handle_get_vcard(q, stream)

    self_handle = conn.GetSelfHandle()
    conn.Aliasing.SetAliases({self_handle: 'lala'})

    expect_and_handle_set_vcard(q, stream)

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(self_handle, u'lala')]])

    room_jid = 'chat@conf.localhost'
    room_handle = request_muc_handle(q, conn, stream, room_jid)

    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TEXT, cs.HT_ROOM,
        room_handle, True)

    gfc, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupFlagsChanged'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='%s/lala' % room_jid))
    assert gfc.args[1] == 0

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'lala'))

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [2, 3], [], [], [], 0, 0])
    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/lala']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']

    event = q.expect('dbus-return', method='RequestChannel')

if __name__ == '__main__':
    exec_test(test)
