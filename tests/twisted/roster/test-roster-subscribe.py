# -*- encoding:utf-8 -*-
"""
Test subscribing to a contact's presence.
"""

from twisted.words.xish import domish

from servicetest import (EventPattern, assertEquals, call_async, sync_dbus)
from gabbletest import (acknowledge_iq, exec_test, sync_stream)
from rostertest import send_roster_push
import constants as cs
import ns

def test_modern(q, bus, conn, stream):
    test(q, bus, conn, stream)

def test_modern_remove(q, bus, conn, stream):
    test(q, bus, conn, stream, True)

def test_modern_reject(q, bus, conn, stream):
    test(q, bus, conn, stream, False, 'reject')

def test_modern_reject_remove(q, bus, conn, stream):
    test(q, bus, conn, stream, True, 'reject')

def test_modern_revoke(q, bus, conn, stream):
    test(q, bus, conn, stream, 'revoke')

def test_modern_revoke_remove(q, bus, conn, stream):
    test(q, bus, conn, stream, True, 'revoke')

def test(q, bus, conn, stream, remove=False, remote='accept'):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    # send back empty roster
    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    q.expect('dbus-signal', signal='ContactListStateChanged', args=[cs.CONTACT_LIST_STATE_SUCCESS])

    # request subscription
    alice, bob = conn.get_contact_handles_sync(
            ['alice@foo.com', 'bob@foo.com'])

    # Repeated subscription requests are *not* idempotent: the second request
    # should nag the contact again.
    for first_time in True, False, False:
        call_async(q, conn.ContactList, 'RequestSubscription', [bob],
                'plz add kthx')

        if first_time:
            event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
            item = event.query.firstChildElement()
            assertEquals('bob@foo.com', item["jid"])
            acknowledge_iq(stream, event.stanza)

        expectations = [
                EventPattern('stream-presence', presence_type='subscribe'),
                ]

        expectations.append(EventPattern('dbus-return',
            method='RequestSubscription'))

        event = q.expect_many(*expectations)[0]
        assertEquals('plz add kthx', event.presence_status)

        if first_time:
            # Our server sends a roster push indicating that yes, we added him
            send_roster_push(stream, 'bob@foo.com', 'none')
            q.expect('stream-iq', iq_type='result', iq_id='push')

            # Our server will also send a roster push with the ask=subscribe
            # sub-state, in response to our <presence type=subscribe>.
            # (RFC 3921 §8.2.4)
            send_roster_push(stream, 'bob@foo.com', 'none', True)
            q.expect('stream-iq', iq_type='result', iq_id='push')

    if remote == 'reject':
        # Bob rejects our request.
        presence = domish.Element(('jabber:client', 'presence'))
        presence['from'] = 'bob@foo.com'
        presence['type'] = 'unsubscribed'
        stream.send(presence)

        q.expect_many(
                EventPattern('dbus-signal', signal='MembersChanged',
                    predicate=lambda e: e.args[0] == [] and e.args[1] == [bob] and
                        e.args[2] == [] and e.args[3] == [] and
                        e.args[4]['change-reason'] == cs.GC_REASON_PERMISSION_DENIED),
                #EventPattern('stream-presence'),
                EventPattern('dbus-signal', signal='ContactsChanged',
                    args=[{bob:
                        (cs.SUBSCRIPTION_STATE_REMOVED_REMOTELY,
                            cs.SUBSCRIPTION_STATE_NO, ''),
                        }, {bob: 'bob@foo.com'}, {}]),
                )

        send_roster_push(stream, 'bob@foo.com', 'to')
        q.expect('stream-iq', iq_type='result', iq_id='push')
    else:
        # Bob accepts
        presence = domish.Element(('jabber:client', 'presence'))
        presence['from'] = 'bob@foo.com'
        presence['type'] = 'subscribed'
        stream.send(presence)

        q.expect_many(
                EventPattern('dbus-signal', signal='MembersChanged',
                    predicate=lambda e: e.args[0] == [bob] and e.args[1] == [] and
                        e.args[2] == [] and e.args[3] == []),
                EventPattern('stream-presence'),
                EventPattern('dbus-signal', signal='ContactsChanged',
                    args=[{bob:
                        (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
                        }, {bob: 'bob@foo.com'}, {}]),
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

        if remote == 'revoke':
            # After accepting us, Bob then removes us.
            presence = domish.Element(('jabber:client', 'presence'))
            presence['from'] = 'bob@foo.com'
            presence['type'] = 'unsubscribed'
            stream.send(presence)

            q.expect_many(
                    EventPattern('dbus-signal', signal='MembersChanged',
                        predicate=lambda e: e.args[0] == [] and e.args[1] == [bob] and
                            e.args[2] == [] and e.args[3] == [] and
                            e.args[4]['change-reason'] == cs.GC_REASON_PERMISSION_DENIED),
                    EventPattern('stream-presence'),
                    EventPattern('dbus-signal', signal='ContactsChanged',
                        args=[{bob:
                            (cs.SUBSCRIPTION_STATE_REMOVED_REMOTELY,
                                cs.SUBSCRIPTION_STATE_NO, ''),
                            }, {bob: 'bob@foo.com'}, {}]),
                    )

        # Else, Bob isn't actually as interesting as we thought. Never mind,
        # we can unsubscribe or remove him (below), with the same APIs we'd
        # use to acknowledge remote removal.

        # (Unsubscribing from pending-subscribe is tested in
        # roster/removed-from-rp-subscribe.py so we don't test it here.)

    if remove:
        returning_method = 'RemoveContacts'
        call_async(q, conn.ContactList, 'RemoveContacts', [bob])
    else:
        returning_method = 'Unsubscribe'
        call_async(q, conn.ContactList, 'Unsubscribe', [bob])

    if remove:
        iq = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER,
                query_name='query')

        acknowledge_iq(stream, iq.stanza)

        q.expect('dbus-return', method='RemoveContacts')
        # FIXME: when we depend on a new enough tp-glib, expect RemoveMembers
        # to return here too

        send_roster_push(stream, 'bob@foo.com', 'remove')
        q.expect_many(
                EventPattern('stream-iq', iq_type='result', iq_id='push'),
                EventPattern('dbus-signal', signal='ContactsChanged',
                    args=[{}, {}, {bob: 'bob@foo.com'}]),
                )
    else:
        q.expect_many(
                EventPattern('dbus-return', method=returning_method),
                EventPattern('stream-presence', presence_type='unsubscribe',
                    to='bob@foo.com'),
                )

        send_roster_push(stream, 'bob@foo.com', 'none')
        q.expect_many(
                EventPattern('stream-iq', iq_type='result', iq_id='push'),
                EventPattern('dbus-signal', signal='ContactsChanged',
                    args=[{bob:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_NO, ''),
                        }, {bob: 'bob@foo.com'}, {}]),
                )

if __name__ == '__main__':
    exec_test(test_modern)
    exec_test(test_modern_remove)
    exec_test(test_modern_revoke)
    exec_test(test_modern_revoke_remove)
    exec_test(test_modern_reject)
    exec_test(test_modern_reject_remove)
