"""
Test receiving and authorizing publish requests, including "pre-authorization"
(authorizing publication before someone asks for it).
"""

from gabbletest import (exec_test, sync_stream, acknowledge_iq)
from rostertest import send_roster_push
from servicetest import (assertEquals, call_async, EventPattern,
        sync_dbus)
import constants as cs
import ns

from twisted.words.xish import domish

def test(q, bus, conn, stream, remove=False):

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    q.expect('dbus-error', method='GetContactListAttributes',
            name=cs.NOT_YET)

    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    item = event.query.addElement('item')
    item['jid'] = 'holly@example.com'
    item['subscription'] = 'both'
    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    holly, dave, arnold, kristine, cat = conn.get_contact_handles_sync(
            ['holly@example.com', 'dave@example.com', 'arnold@example.com',
                'kristine@example.com', 'cat@example.com'])

    # slight implementation detail: TpBaseContactList emits ContactsChangedWithID
    # before it announces its channels
    s = q.expect('dbus-signal', signal='ContactsChangedWithID',
            interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path)
    assertEquals([{
        holly: (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        }, { holly: 'holly@example.com' }, {}], s.args)

    # this is emitted last, so clients can tell when the initial state dump
    # has finished
    q.expect('dbus-signal', signal='ContactListStateChanged',
            args=[cs.CONTACT_LIST_STATE_SUCCESS])

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertEquals(({
        holly: {
            cs.CONN_IFACE_CONTACT_LIST + '/publish':
                cs.SUBSCRIPTION_STATE_YES,
            cs.CONN_IFACE_CONTACT_LIST + '/subscribe':
                cs.SUBSCRIPTION_STATE_YES,
            cs.CONN + '/contact-id': 'holly@example.com',
            }
        },), r.value)

    # publication authorized for Dave, Holly (the former is pre-authorization,
    # the latter is a no-op)
    call_async(q, conn.ContactList, 'AuthorizePublication', [dave, holly])
    event = q.expect('dbus-return', method='AuthorizePublication')

    # Receive authorization requests from the contacts

    # We pre-authorized Dave, so this is automatically approved
    presence = domish.Element(('jabber:client', 'presence'))
    presence['type'] = 'subscribe'
    presence['from'] = 'dave@example.com'
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{dave: (cs.SUBSCRIPTION_STATE_NO,
                    cs.SUBSCRIPTION_STATE_ASK,
                    '')}, { dave: 'dave@example.com' }, {}]),
            EventPattern('stream-presence', presence_type='subscribed',
                to='dave@example.com'),
            )

    # Our server responds to Dave being authorized
    send_roster_push(stream, 'dave@example.com', 'from')
    q.expect_many(
            EventPattern('stream-iq', iq_type='result', iq_id='push'),
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{dave: (cs.SUBSCRIPTION_STATE_NO,
                    cs.SUBSCRIPTION_STATE_YES, '')}, { dave: 'dave@example.com' }, {}]),
            )

    # The request from Kristine needs authorization (below)
    presence['from'] = 'kristine@example.com'
    stream.send(presence)

    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{kristine: (cs.SUBSCRIPTION_STATE_NO,
                cs.SUBSCRIPTION_STATE_ASK, '')}, { kristine: 'kristine@example.com' }, {}])

    # This request from Arnold is dealt with below
    presence['from'] = 'arnold@example.com'
    stream.send(presence)

    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{arnold: (cs.SUBSCRIPTION_STATE_NO,
                cs.SUBSCRIPTION_STATE_ASK, '')}, { arnold: 'arnold@example.com' }, {}])

    returning_method = 'AuthorizePublication'
    call_async(q, conn.ContactList, 'AuthorizePublication',
            [kristine, holly])

    q.expect_many(
            EventPattern('dbus-return', method=returning_method),
            EventPattern('stream-presence', presence_type='subscribed',
                to='kristine@example.com'),
            )

    # Our server acknowledges that we authorized Kristine. Holly's state
    # does not change.
    send_roster_push(stream, 'kristine@example.com', 'from')
    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{kristine: (cs.SUBSCRIPTION_STATE_NO,
                    cs.SUBSCRIPTION_STATE_YES,
                    '')}, { kristine: 'kristine@example.com' }, {}]),
            EventPattern('stream-iq', iq_type='result', iq_id='push'),
            )

    # Arnold gives up waiting for us, and cancels his request
    presence['from'] = 'arnold@example.com'
    presence['type'] = 'unsubscribe'
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{arnold: (cs.SUBSCRIPTION_STATE_NO,
                    cs.SUBSCRIPTION_STATE_REMOVED_REMOTELY, '')}, { arnold: 'arnold@example.com' }, {}]),
            EventPattern('stream-presence', presence_type='unsubscribed',
                to='arnold@example.com'),
            )

    # We can acknowledge that with RemoveContacts or with Unpublish.
    # The old Chan.T.ContactList API can't acknowledge RemovedRemotely,
    # because it sees it as "not there at all" and the group logic drops
    # the "redundant" request.

    if remove:
        returning_method = 'RemoveContacts'
        call_async(q, conn.ContactList, 'RemoveContacts', [arnold])
    else:
        returning_method = 'Unpublish'
        call_async(q, conn.ContactList, 'Unpublish', [arnold])

    # Even if we Unpublish() here, Arnold was never on our XMPP roster,
    # so setting his publish state to SUBSCRIPTION_STATE_NO should result
    # in his removal.
    q.expect_many(
            EventPattern('dbus-return', method=returning_method),
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{}, {}, {arnold: 'arnold@example.com' }]),
            )

    # Rejecting an authorization request also works
    presence = domish.Element(('jabber:client', 'presence'))
    presence['type'] = 'subscribe'
    presence['from'] = 'cat@example.com'
    stream.send(presence)

    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{cat: (cs.SUBSCRIPTION_STATE_NO,
                cs.SUBSCRIPTION_STATE_ASK,
                '')}, { cat: 'cat@example.com' }, {}])

    if remove:
        returning_method = 'RemoveContacts'
        call_async(q, conn.ContactList, 'RemoveContacts', [cat])
    else:
        returning_method = 'Unpublish'
        call_async(q, conn.ContactList, 'Unpublish', [cat])

    # As above, the only reason the Cat is on our contact list is the pending
    # publish request, so Unpublish really results in removal.
    q.expect_many(
            EventPattern('dbus-return', method=returning_method),
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{}, {}, { cat: 'cat@example.com' }]),
            )

    # Redundant API calls (removing an absent contact, etc.) cause no network
    # traffic, and succeed.
    forbidden = [EventPattern('stream-iq', query_ns=ns.ROSTER),
            EventPattern('stream-presence')]
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.forbid_events(forbidden)

    call_async(q, conn.ContactList, 'AuthorizePublication',
            [kristine, holly, dave])
    call_async(q, conn.ContactList, 'Unpublish', [arnold, cat])
    call_async(q, conn.ContactList, 'RemoveContacts', [arnold, cat])
    q.expect_many(
            EventPattern('dbus-return', method='AuthorizePublication'),
            EventPattern('dbus-return', method='Unpublish'),
            EventPattern('dbus-return', method='RemoveContacts'),
            )

    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

    # There's one more case: revoking the publish permission of someone who is
    # genuinely on the roster.

    if remove:
        returning_method = 'RemoveContacts'
        call_async(q, conn.ContactList, 'RemoveContacts', [holly])
    else:
        returning_method = 'Unpublish'
        call_async(q, conn.ContactList, 'Unpublish', [holly])

    if remove:
        iq = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER,
                query_name='query')

        acknowledge_iq(stream, iq.stanza)

        q.expect('dbus-return', method='RemoveContacts')
        # FIXME: when we depend on a new enough tp-glib, expect RemoveMembers
        # to return here too

        send_roster_push(stream, 'holly@example.com', 'remove')
        q.expect_many(
                EventPattern('stream-iq', iq_type='result', iq_id='push'),
                EventPattern('dbus-signal', signal='ContactsChangedWithID',
                    args=[{}, {}, { holly: 'holly@example.com' }]),
                )
    else:
        q.expect_many(
                EventPattern('dbus-return', method=returning_method),
                EventPattern('stream-presence', presence_type='unsubscribed',
                    to='holly@example.com'),
                )

        send_roster_push(stream, 'holly@example.com', 'to')
        q.expect_many(
                EventPattern('stream-iq', iq_type='result', iq_id='push'),
                EventPattern('dbus-signal', signal='ContactsChangedWithID',
                    args=[{holly:
                        (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
                        }, { holly: 'holly@example.com' }, {}]),
                )

def test_modern(q, bus, conn, stream):
    test(q, bus, conn, stream)

def test_modern_remove(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=True)

if __name__ == '__main__':
    exec_test(test_modern)
    exec_test(test_modern_remove)
