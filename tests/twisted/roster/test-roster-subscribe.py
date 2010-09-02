
"""
Test subscribing to a contact's presence.
"""

from twisted.words.xish import domish

from servicetest import (EventPattern, assertLength, assertEquals,
        call_async, wrap_channel, sync_dbus)
from gabbletest import (acknowledge_iq, exec_test, sync_stream)
from rostertest import send_roster_push
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
    alice, bob = conn.RequestHandles(cs.HT_CONTACT,
            ['alice@foo.com', 'bob@foo.com'])

    # Repeated subscription requests are *not* idempotent: the second request
    # should nag the contact again.
    for first_time in True, False, False:
        if modern:
            call_async(q, conn.ContactList, 'RequestSubscription', [bob],
                    'plz add kthx')
        else:
            call_async(q, chan.Group, 'AddMembers', [bob],
                    'plz add kthx')

        if first_time:
            event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
            item = event.query.firstChildElement()
            assertEquals('bob@foo.com', item["jid"])
            acknowledge_iq(stream, event.stanza)

        expectations = [
                EventPattern('stream-presence', presence_type='subscribe'),
                ]

        if modern:
            expectations.append(EventPattern('dbus-return',
                method='RequestSubscription'))

        event = q.expect_many(*expectations)[0]
        assertEquals('plz add kthx', event.presence_status)

    # Bob accepts
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='MembersChanged',
                args=['', [bob], [], [], [], 0, 0]),
            EventPattern('stream-presence'),
            EventPattern('dbus-signal', signal='ContactsChanged',
                args=[{bob:
                    (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
                    }, []]),
            )

    send_roster_push(stream, 'bob@foo.com', 'to')
    q.expect('stream-iq', iq_type='result', iq_id='push')

    # Doing the same again is a successful no-op
    forbidden = [EventPattern('stream-iq', query_ns=ns.ROSTER),
            EventPattern('stream-presence')]
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.forbid_events(forbidden)

    call_async(q, conn.ContactList, 'RequestSubscription', [bob], 'moo')
    q.expect('dbus-return', method='RequestSubscription')

    # Alice is not on the list
    call_async(q, conn.ContactList, 'Unsubscribe', [alice])
    q.expect('dbus-return', method='Unsubscribe')
    call_async(q, conn.ContactList, 'RemoveContacts', [alice])
    q.expect('dbus-return', method='RemoveContacts')

    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

if __name__ == '__main__':
    exec_test(test_ancient)
    exec_test(test_modern)
