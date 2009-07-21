
"""
Test subscribing to a contact's presence.
"""

import dbus

from twisted.words.xish import domish

from servicetest import EventPattern, wrap_channel, assertLength, assertEquals
from gabbletest import acknowledge_iq, exec_test
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    # send back empty roster
    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    while True:
        event = q.expect('dbus-signal', signal='NewChannel')
        path, type, handle_type, handle, suppress_handler = event.args

        if type != cs.CHANNEL_TYPE_CONTACT_LIST:
            continue

        chan_name = conn.InspectHandles(handle_type, [handle])[0]

        if chan_name == 'subscribe':
            break

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'ContactList')
    assertLength(0, chan.Group.GetMembers())

    # request subscription
    handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@foo.com'])[0]
    chan.Group.AddMembers([handle], '')

    event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()
    assertEquals('bob@foo.com', item["jid"])

    acknowledge_iq(stream, event.stanza)

    event = q.expect('stream-presence', presence_type='subscribe')

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='MembersChanged',
                args=['', [handle], [], [], [], 0, 0]),
            EventPattern('stream-presence'),
            )

if __name__ == '__main__':
    exec_test(test)
