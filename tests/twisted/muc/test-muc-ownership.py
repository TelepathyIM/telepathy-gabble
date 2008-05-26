
"""
Test support for the HANDLE_OWNERS_NOT_AVAILABLE group flag, and calling
GetHandleOwners on MUC members.

By default, MUC channels should have the flag set. The flag should be unset
when presence is received that includes the MUC JID's owner JID.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go, make_result_iq, exec_test
from servicetest import call_async, lazy, match, tp_name_prefix, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

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

    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 2, room_handle, True)

    gfc, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupFlagsChanged'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))
    assert gfc.args[1] == 0

    event = q.expect('dbus-signal', signal='GroupFlagsChanged')
    assert event.args == [0, 1]

    # Send presence for anonymous other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    # Send presence for anonymous other member of room (2)
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/brian'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    # Send presence for nonymous other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/che'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    item['jid'] = 'che@foo.com'
    stream.send(presence)

    # Send presence for nonymous other member of room (2)
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/chris'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    item['jid'] = 'chris@foo.com'
    stream.send(presence)

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

    event = q.expect('dbus-signal', signal='GroupFlagsChanged')
    # Since we received MUC presence that contains an owner JID, the
    # OWNERS_NOT_AVAILABLE flag should be removed.
    assert event.args == [0, 1024]

    event = q.expect('dbus-signal', signal='HandleOwnersChanged',
        args=[{2: 0, 3: 0, 4: 0, 5: 6, 7: 8}, []])

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [2, 3, 4, 5, 7], [], [], [], 0, 0])
    assert conn.InspectHandles(1, [2]) == [
        'chat@conf.localhost/test']
    assert conn.InspectHandles(1, [3]) == [
        'chat@conf.localhost/bob']
    assert conn.InspectHandles(1, [4]) == [
        'chat@conf.localhost/brian']
    assert conn.InspectHandles(1, [5]) == [
        'chat@conf.localhost/che']
    assert conn.InspectHandles(1, [6]) == [
        'che@foo.com']
    assert conn.InspectHandles(1, [7]) == [
        'chat@conf.localhost/chris']
    assert conn.InspectHandles(1, [8]) == [
        'chris@foo.com']

    event = q.expect('dbus-return', method='RequestChannel')
    # Check that GetHandleOwners works.
    # FIXME: using non-API!
    bus = conn._bus
    chan = bus.get_object(conn._named_service, event.value[0])
    group = dbus.Interface(chan,
        'org.freedesktop.Telepathy.Channel.Interface.Group')
    assert group.GetHandleOwners([5, 7]) == [6, 8]

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

