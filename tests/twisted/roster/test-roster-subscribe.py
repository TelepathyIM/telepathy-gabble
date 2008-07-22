
"""
Test subscribing to a contact's presence.
"""

import dbus

from twisted.words.xish import domish

from servicetest import EventPattern
from gabbletest import acknowledge_iq, exec_test

def test(q, bus, conn, stream):
    conn.Connect()

    event = q.expect('stream-iq', query_ns='jabber:iq:roster')
    # send back empty roster
    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    while 1:
        event = q.expect('dbus-signal', signal='NewChannel')
        path, type, handle_type, handle, suppress_handler = event.args

        if type != u'org.freedesktop.Telepathy.Channel.Type.ContactList':
            continue

        chan_name = conn.InspectHandles(handle_type, [handle])[0]

        if chan_name == 'subscribe':
            break

    # request subscription
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Group')
    assert group_iface.GetMembers() == []
    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    group_iface.AddMembers([handle], '')

    event = q.expect('stream-iq', iq_type='set', query_ns='jabber:iq:roster')
    item = event.query.firstChildElement()
    assert item["jid"] == 'bob@foo.com'

    acknowledge_iq(stream, event.stanza)

    while 1:
        event = q.expect('stream-presence')
        if event.stanza['type'] == 'subscribe':
            break

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='MembersChanged',
                args=['', [2], [], [], [], 0, 0]),
            EventPattern('stream-presence'),
            )

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

