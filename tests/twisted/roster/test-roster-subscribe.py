
"""
Test subscribing to a contact's presence.
"""

import dbus

from twisted.words.xish import domish

from servicetest import (EventPattern, assertLength, assertEquals,
        call_async, wrap_channel)
from gabbletest import acknowledge_iq, exec_test
import constants as cs
import ns

def test_ancient(q, bus, conn, stream):
    test(q, bus, conn, stream, False)

def test_modern(q, bus, conn, stream):
    test(q, bus, conn, stream, True)

def test(q, bus, conn, stream, modern=True):
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

    expectations = [
            EventPattern('stream-iq', iq_type='set', query_ns=ns.ROSTER),
            ]

    if modern:
        call_async(q, conn.ContactList, 'RequestSubscription', [handle], '')
    else:
        call_async(q, chan.Group, 'AddMembers', [handle], '')
        expectations.append(EventPattern('dbus-return', method='AddMembers'))

    event = q.expect_many(*expectations)[0]

    item = event.query.firstChildElement()
    assertEquals('bob@foo.com', item["jid"])

    acknowledge_iq(stream, event.stanza)

    # FIXME: also expect RequestSubscription to finish; in principle, it should
    # only finish after we ack that IQ, although at the moment it finishes
    # sooner

    event = q.expect('stream-presence', presence_type='subscribe')

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='MembersChanged',
                args=['', [handle], [], [], [], 0, 0]),
            EventPattern('stream-presence'),
            EventPattern('dbus-signal', signal='ContactsChanged',
                args=[{handle:
                    (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
                    }, []]),
            )

if __name__ == '__main__':
    exec_test(test_ancient)
    exec_test(test_modern)
