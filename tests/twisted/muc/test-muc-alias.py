"""
Test that our alias is used to create MUC JIDs.
Mash-up of vcard/test-set-alias.py and muc/test-muc.py.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go, make_result_iq, acknowledge_iq, exec_test
from servicetest import call_async, lazy, match, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    conn.Aliasing.SetAliases({1: 'lala'})

    iq_event = q.expect('stream-iq', iq_type='set', query_ns='vcard-temp',
        query_name='vCard')
    acknowledge_iq(stream, iq_event.stanza)

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(1, u'lala')]])

    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
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
        EventPattern('stream-presence', to='chat@conf.localhost/lala'))
    assert gfc.args[1] == 0

    # Send presence for other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/lala'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [2, 3], [], [], [], 0, 0])
    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/lala']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']

    event = q.expect('dbus-return', method='RequestChannel')

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

