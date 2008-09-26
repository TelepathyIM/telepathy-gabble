"""
Test that EnsureChannel works for MUCs, particularly in the case when there
are several pending requests for the same MUC.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go, make_result_iq, exec_test
from servicetest import call_async, lazy, match, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

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

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True

def test_create_ensure(q, conn, bus, stream, room_jid, room_handle):
    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')

    # Call both Create and Ensure for the same channel.
    call_async(q, requestotron, 'CreateChannel',
           { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 2,
             'org.freedesktop.Telepathy.Channel.TargetHandle': room_handle,
           })
    call_async(q, requestotron, 'EnsureChannel',
           { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 2,
             'org.freedesktop.Telepathy.Channel.TargetHandle': room_handle,
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
    presence = domish.Element((None, 'presence'))
    presence['from'] = '%s/bob' % room_jid
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = '%s/test' % room_jid
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

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
    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')

    # Call Ensure twice for the same channel.
    call_async(q, requestotron, 'EnsureChannel',
           { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 2,
             'org.freedesktop.Telepathy.Channel.TargetHandle': room_handle,
           })
    call_async(q, requestotron, 'EnsureChannel',
           { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 2,
             'org.freedesktop.Telepathy.Channel.TargetHandle': room_handle,
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
    presence = domish.Element((None, 'presence'))
    presence['from'] = '%s/bob' % room_jid
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = '%s/test' % room_jid
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

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

