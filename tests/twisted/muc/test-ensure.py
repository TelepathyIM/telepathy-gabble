"""
Test that EnsureChannel works for MUCs, particularly in the case when there
are several pending requests for the same MUC.
"""

from gabbletest import make_result_iq, exec_test, make_muc_presence
from servicetest import call_async, EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
    jids = ['chat@conf.localhost', 'chien@conf.localhost']
    call_async(q, conn, 'RequestHandles', 2, jids)

    # Gabble is stupid and discos the alleged conf server twice.
    for i in [0,1]:
        event = q.expect('stream-iq', to='conf.localhost',
            query_ns='http://jabber.org/protocol/disco#info')
        result = make_result_iq(stream, event.stanza)
        feature = result.firstChildElement().addElement('feature')
        feature['var'] = 'http://jabber.org/protocol/muc'
        stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    room_handles = event.value[0]

    test_create_ensure(q, conn, bus, stream, jids[0], room_handles[0])
    test_ensure_ensure(q, conn, bus, stream, jids[1], room_handles[1])

def test_create_ensure(q, conn, bus, stream, room_jid, room_handle):
    # Call both Create and Ensure for the same channel.
    call_async(q, conn.Requests, 'CreateChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_HANDLE: room_handle,
           })
    call_async(q, conn.Requests, 'EnsureChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_HANDLE: room_handle,
           })

    mc, _ = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged'),
        EventPattern('stream-presence', to=('%s/test' % room_jid)))
    msg, added, removed, local_pending, remote_pending, actor, reason = mc.args

    assert added == [], mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert len(remote_pending) == 1, mc.args

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'test'))

    mc = q.expect('dbus-signal', signal='MembersChanged')
    msg, added, removed, local_pending, remote_pending, actor, reason = mc.args

    assert len(added) == 2, mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert remote_pending == [], mc.args

    members = conn.InspectHandles(1, added)
    members.sort()
    assert members == ['%s/bob' % room_jid, '%s/test' % room_jid], members

    create_event, ensure_event = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-return', method='EnsureChannel'))

    assert len(create_event.value) == 2
    c_path, c_props = create_event.value

    assert len(ensure_event.value) == 3
    yours, e_path, e_props = ensure_event.value

    assert c_path == e_path, (c_path, e_path)
    assert c_props == e_props, (c_props, e_props)

    assert not yours


def test_ensure_ensure(q, conn, bus, stream, room_jid, room_handle):
    # Call Ensure twice for the same channel.
    call_async(q, conn.Requests, 'EnsureChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_HANDLE: room_handle,
           })
    call_async(q, conn.Requests, 'EnsureChannel',
           { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             cs.TARGET_HANDLE: room_handle,
           })

    mc, _ = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged'),
        EventPattern('stream-presence', to=('%s/test' % room_jid)))
    msg, added, removed, local_pending, remote_pending, actor, reason = mc.args

    assert added == [], mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert len(remote_pending) == 1, mc.args

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', room_jid, 'test'))

    mc = q.expect('dbus-signal', signal='MembersChanged')
    msg, added, removed, local_pending, remote_pending, actor, reason = mc.args

    assert len(added) == 2, mc.args
    assert removed == [], mc.args
    assert local_pending == [], mc.args
    assert remote_pending == [], mc.args

    members = conn.InspectHandles(1, added)
    members.sort()
    assert members == ['%s/bob' % room_jid, '%s/test' % room_jid], members

    # We should get two EnsureChannel returns
    es = []
    while len(es) < 2:
        e = q.expect('dbus-return', method='EnsureChannel')
        es.append(e)

    e1, e2 = es

    assert len(e1.value) == 3
    yours1, path1, props1 = e1.value

    assert len(e2.value) == 3
    yours2, path2, props2 = e2.value

    # Exactly one Ensure should get Yours=True.
    assert (yours1 == (not yours2))

    assert path1 == path2, (path1, path2)
    assert props1 == props2, (props1, props2)


if __name__ == '__main__':
    exec_test(test)

