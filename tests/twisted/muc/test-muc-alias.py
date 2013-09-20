"""
Test that our alias is used to create MUC JIDs.
"""

from gabbletest import exec_test, make_muc_presence, request_muc_handle, \
    expect_and_handle_get_vcard, expect_and_handle_set_vcard
from servicetest import call_async, EventPattern

import constants as cs

def test(q, bus, conn, stream):
    expect_and_handle_get_vcard(q, stream)

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    conn.Aliasing.SetAliases({self_handle: 'lala'})

    expect_and_handle_set_vcard(q, stream)

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(self_handle, u'lala')]])

    room_jid = 'chat@conf.localhost'

    # muc stream tube
    call_async(q, conn.Requests, 'CreateChannel', {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: room_jid})

    gfc, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupFlagsChanged'),
        EventPattern('dbus-signal', signal='MembersChangedDetailed',
            predicate=lambda e:
                e.args[0] == [] and         # added
                e.args[1] == [] and         # removed
                e.args[2] == [] and         # local pending
                len(e.args[3]) == 1 and     # remote pending
                e.args[4].get('actor', 0) == 0 and
                e.args[4].get('change-reason', 0) == 0 and
                e.args[4]['contact-ids'][e.args[3][0]] == 'chat@conf.localhost/lala'),
        EventPattern('stream-presence', to='%s/lala' % room_jid))
    assert gfc.args[1] == 0

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'lala'))

    event = q.expect('dbus-signal', signal='MembersChangedDetailed',
            predicate=lambda e:
                len(e.args[0]) == 2 and     # added
                e.args[1] == [] and         # removed
                e.args[2] == [] and         # local pending
                e.args[3] == [] and         # remote pending
                e.args[4].get('actor', 0) == 0 and
                e.args[4].get('change-reason', 0) == 0 and
                set([e.args[4]['contact-ids'][h] for h in e.args[0]]) ==
                set(['chat@conf.localhost/lala', 'chat@conf.localhost/bob']))

    event = q.expect('dbus-return', method='CreateChannel')

if __name__ == '__main__':
    exec_test(test)
