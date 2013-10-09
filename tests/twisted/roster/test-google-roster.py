# vim: set fileencoding=utf-8 :
"""
Test workarounds for gtalk
"""

from gabbletest import (
    acknowledge_iq, exec_test, sync_stream, make_result_iq, GoogleXmlStream,
    )
from rostertest import (
    check_contact_roster, contacts_changed_predicate, blocked_contacts_changed_predicate,
    )
from servicetest import (
    call_async, sync_dbus, EventPattern,
    assertLength, assertEquals, assertContains, assertDoesNotContain,
    )
import constants as cs
import ns

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish

def make_set_roster_iq(stream, user, contact, state, ask, attrs={}):
    iq = IQ(stream, 'set')
    query = iq.addElement((ns.ROSTER, 'query'))
    add_gr_attributes(query)
    add_roster_item(query, contact, state, ask, attrs=attrs)
    return iq

def add_gr_attributes(query):
    query['xmlns:gr'] = ns.GOOGLE_ROSTER
    query['gr:ext'] = '2'

def add_roster_item(query, contact, state, ask, attrs={}):
    item = query.addElement('item')
    item['jid'] = contact
    item['subscription'] = state

    if ask:
        item['ask'] = 'subscribe'

    for k, v in attrs.iteritems():
        item[k] = v

    return item

def test_inital_roster(q, bus, conn, stream):
    """
    This part of the test checks that Gabble correctly alters on which lists
    contacts appear based on the google:roster attributes and special-cases.
    """

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    query = event.query
    assertContains('gr', query.localPrefixes)
    assertEquals(ns.GOOGLE_ROSTER, query.localPrefixes['gr'])
    # We support version 2 of Google's extensions.
    assertEquals('2', query[(ns.GOOGLE_ROSTER, 'ext')])

    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    add_gr_attributes(query)

    # Gabble suppresses contacts labelled as "hidden" from all roster channels.
    add_roster_item(query, 'should-be-hidden@example.com', 'both', False,
        {'gr:t': 'H'})
    # Gabble should hide contacts on the Google roster with subscription="none"
    # and ask!="subscribe", to hide contacts which are actually just email
    # addresses. (This is in line with Pidgin; the code there was added by Sean
    # Egan, who worked on Google Talk for Google at the time.)
    add_roster_item(query, 'probably-an-email-address@badger.com', 'none',
        False)
    # This contact is remote pending, so we shouldn't suppress it.
    add_roster_item(query, 'this-is-a-jid@badger.com', 'none', True)
    add_roster_item(query, 'lp-bug-398293@gmail.com', 'both', False,
        {'gr:autosub': 'true'})
    # These contacts are blocked but we're subscribed to them, so they should
    # show up in all of the lists.
    add_roster_item(query, 'blocked-but-subscribed@boards.ca', 'both', False,
        {'gr:t': 'B'})
    add_roster_item(query, 'music-is-math@boards.ca', 'both', False,
        {'gr:t': 'B'})
    # This contact is blocked, and we have no other subscription to them; so,
    # they should not show up in 'stored'.
    add_roster_item(query, 'blocked-and-no-sub@boards.ca', 'none', False,
        {'gr:t': 'B'})

    # Send back the roster
    stream.send(result)

    # Since s-b-h had the "hidden" flag set, we don't expect them to be on any
    # lists. But we do want the "autosub" contact to be visible; see
    # <https://bugs.launchpad.net/ubuntu/+source/telepathy-gabble/+bug/398293>,
    # where Gabble was incorrectly hiding valid contacts.

    contacts = [
        ('lp-bug-398293@gmail.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        ('blocked-but-subscribed@boards.ca', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        ('music-is-math@boards.ca', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        ('this-is-a-jid@badger.com', cs.SUBSCRIPTION_STATE_ASK, cs.SUBSCRIPTION_STATE_NO, '')
        ]

    blocked_contacts = ['blocked-but-subscribed@boards.ca',
        'blocked-and-no-sub@boards.ca',
        'music-is-math@boards.ca']

    q.expect_many(
        EventPattern('dbus-signal', signal='ContactsChangedWithID',
            predicate=lambda e: contacts_changed_predicate(e, conn, contacts)),
        EventPattern('dbus-signal', signal='BlockedContactsChanged',
            predicate=lambda e: blocked_contacts_changed_predicate(e, blocked_contacts, [])),
        )

def test_flickering(q, bus, conn, stream):
    """
    Google's server is buggy; when asking to subscribe to somebody, the
    subscription state transitions "flicker" sometimes. Here, we test that
    Gabble is suppressing the flickers.
    """

    contact = 'bob@foo.com'
    handle = conn.get_contact_handle_sync('bob@foo.com')

    # request subscription
    call_async(q, conn.ContactList, 'RequestSubscription', [handle], '')

    event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()
    assertEquals(contact, item['jid'])

    acknowledge_iq(stream, event.stanza)
    # FIXME: when we depend on a new enough tp-glib we could expect
    # AddMembers to return at this point

    # send empty roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False)
    stream.send(iq)

    # We don't expect the stored list to be updated here, because Gabble
    # ignores Google Talk roster items with subscription="none" and
    # ask!="subscribe" as described above.
    event = q.expect('stream-presence', presence_type='subscribe')

    # Google's server appears to be buggy. If you send
    #   <presence type='subscribe'/>
    # it sends:
    #  1. A roster update with ask="subscribe";
    #  2. Another roster update, without ask="subscribe";
    #  3. A third roster update, with ask="subscribe".
    # Gabble should work around this, to avoid spuriously informing the UI that
    # the subscription request was declined.

    # Send roster update 1: none, ask=subscribe
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", True)
    stream.send(iq)

    # Gabble should report this update to the UI.
    q.expect_many(
        EventPattern('dbus-signal', signal='ContactsChangedWithID',
            args=[{handle:
                (cs.SUBSCRIPTION_STATE_ASK, cs.SUBSCRIPTION_STATE_NO, ''),
                }, {handle: contact}, {}]),
        )

    # Gabble shouldn't report any changes to subscribe or stored's members in
    # response to the next two roster updates.
    change_events = [
        EventPattern('dbus-signal', signal='ContactsChangedWithID'),
        ]
    q.forbid_events(change_events)

    # Send roster update 2: none
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False)
    stream.send(iq)

    # Send roster update 3: none, ask=subscribe
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", True)
    stream.send(iq)

    # Neither of those should have been signalled as a change to the subscribe
    # list
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events(change_events)

    # Also, when the contact accepts the subscription request, they flicker
    # similarly:
    #  1. subscription='to'
    #  2. subscription='none' ask='subscribe'
    #  3. subscription='to'
    # Again, Gabble should work around this rather than informing the UI that a
    # subscription request was accepted twice.

    # Send roster update 1: subscription=to (accepted)
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "to", False)
    stream.send(iq)

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    stream.send(presence)

    # Gabble should report this update to the UI.
    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{handle:
                (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
                }, {handle: contact}, {}])

    # Gabble shouldn't report any changes to subscribe or stored's members in
    # response to the next two roster updates.
    q.forbid_events(change_events)

    # Send roster update 2: subscription=none, ask=subscribe (pending again)
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", True)
    stream.send(iq)

    # Send roster update 3: subscript=to (accepted again)
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "to", False)
    stream.send(iq)

    # Neither of those should have been signalled as a change to the subscribe
    # list
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events(change_events)

def test_local_pending(q, bus, conn, stream):
    """
    When somebody asks to subscribe to us, Google sends the subscription
    request and then a roster update saying there is no subscription.
    This causes the contact to appear in local pending and then disappear.
    Here, we test that Gabble is suppressing the flickers.
    """

    contact = 'alice@foo.com'
    handle = conn.get_contact_handle_sync(contact)

    # Alice asks to subscribes to us
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = contact
    presence['type'] = 'subscribe'
    stream.send(presence)

    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{handle: (cs.SUBSCRIPTION_STATE_NO,
                cs.SUBSCRIPTION_STATE_ASK, '')}, {handle: contact}, {}])

    def alice_state_changed(e):
        # check that Alice's publish and subscribe state isn't changed
        changes, ids, removal = e.args

        subscription = changes.get(handle)
        if subscription is None:
            return False

        subscribe, publish, request = subscription
        return subscribe != cs.SUBSCRIPTION_STATE_NO and publish != cs.SUBSCRIPTION_STATE_ASK

    # Now we send the spurious roster update with subscribe="none" and verify
    # that nothing happens to her publish state in reaction to that
    change_event = EventPattern('dbus-signal', signal='ContactsChangedWithID',
            predicate=alice_state_changed)
    q.forbid_events([change_event])

    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False)
    stream.send(iq)

    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events([change_event])

    # Now we cancel alice's subscription request and verify that if the
    # redundant IQ is sent again, it's safely handled
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = contact
    presence['type'] = 'unsubscribe'
    stream.send(presence)

    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{handle: (cs.SUBSCRIPTION_STATE_NO,
                cs.SUBSCRIPTION_STATE_REMOVED_REMOTELY, '')}, {handle: contact}, {}])

    # Now we send a roster roster update with subscribe="none" again (which
    # doesn't change anything, it just confirms what we already knew) and
    # verify that nothing happens to her publish state in reaction to that.
    q.forbid_events([change_event])

    iq = make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False)
    stream.send(iq)

    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events([change_event])

# This event is forbidden in all of the deny tests!
remove_events = [
    EventPattern('stream-iq', query_ns=ns.ROSTER,
        predicate=(lambda event:
            event.query.firstChildElement()['subscription'] == 'remove'),
        )
    ]

def test_deny_simple(q, bus, conn, stream):
    """
    If we remove a blocked contact from 'stored', they shouldn't actually be
    removed from the roster: rather, we should cancel both subscription
    directions, at which point they will vanish from 'stored', while
    remaining on 'deny'.
    """
    contact = 'blocked-but-subscribed@boards.ca'
    handle = conn.get_contact_handle_sync(contact)

    check_contact_roster(conn, contact, [],
            cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES)

    call_async(q, conn.ContactList, 'RemoveContacts', [handle])

    q.forbid_events(remove_events)

    q.expect_many(
        EventPattern('stream-presence', to=contact,
            presence_type='unsubscribe'),
        EventPattern('stream-presence', to=contact,
            presence_type='unsubscribed'),
        EventPattern('dbus-return', method='RemoveContacts'),
        )

    # Our server sends roster pushes in response to our unsubscribe and
    # unsubscribed commands.
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "from", False, attrs={'gr:t': 'B'}))
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False, attrs={'gr:t': 'B'}))

    # As a result they should drop off all three non-deny lists, but not fall
    # off deny:
    q.expect('dbus-signal', signal='ContactsChangedWithID', args=[{}, {}, {handle: contact}])

    assertContains(handle,
        conn.ContactBlocking.RequestBlockedContacts().keys())

    q.unforbid_events(remove_events)

def test_deny_overlap_one(q, bus, conn, stream):
    """
    Here's a tricker case: blocking a contact, and then removing them before
    the server's responded to the block request.
    """

    # As we saw in test_flickering(), we have a subscription to Bob,
    # everything's peachy.
    contact = 'bob@foo.com'
    handle = conn.get_contact_handle_sync(contact)

    check_contact_roster(conn, contact, [],
            cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO)

    q.forbid_events(remove_events)

    # But then we have a falling out. In a blind rage, I block Bob:
    call_async(q, conn.ContactBlocking, 'BlockContacts', [handle], "")
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()
    assertEquals(contact, item['jid'])
    assertEquals('B', item[(ns.GOOGLE_ROSTER, 't')])

    # Then — *before the server has replied* — I remove him from the contact
    # list.
    call_async(q, conn.ContactList, 'RemoveContacts', [handle])

    # subscription='remove' is still forbidden from above. So we sync to ensure
    # that Gabble's received RemoveMembers, and if it's going to send us a
    # remove (or premature <presence type='unsubscribe'/>) we catch it.
    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

    # So now we send a roster push and reply for the block request.
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
        'to', False, attrs={ 'gr:t': 'B' }))
    acknowledge_iq(stream, event.stanza)

    # At which point, Bob should appear on 'deny', and Gabble should send an
    # unsubscribe, but *not* an unsubscribe*d* because Bob wasn't subscribed to
    # us!
    unsubscribed_events = [
        EventPattern('stream-presence', presence_type='unsubscribed')
        ]
    q.forbid_events(unsubscribed_events)

    q.expect_many(
        EventPattern('dbus-signal', signal='BlockedContactsChanged',
            predicate=lambda e: blocked_contacts_changed_predicate(e, [contact], [])),
        EventPattern('stream-presence', to=contact,
            presence_type='unsubscribe'),
        )

    # And our server sends us a roster push in response to unsubscribe:
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False, attrs={'gr:t': 'B'}))

    # As a result, Gabble makes Bob fall off subscribe and stored.
    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{}, {}, {handle: contact}])

    # And he should definitely still be on deny. That rascal.
    assertContains(handle,
        conn.ContactBlocking.RequestBlockedContacts().keys())

    q.unforbid_events(unsubscribed_events)
    q.unforbid_events(remove_events)

def test_deny_overlap_two(q, bus, conn, stream):
    """
    Here's another tricky case: editing a contact (setting an alias, say), and
    then while that edit's in flight, blocking and remove the contact.
    """

    # This contact was on our roster when we started.
    contact = 'lp-bug-398293@gmail.com'
    handle = conn.get_contact_handle_sync(contact)

    check_contact_roster(conn, contact, [],
            cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES)

    # Once again, at no point in this test should anyone be removed outright.
    q.forbid_events(remove_events)

    # First up, we edit the contact's alias, triggering a roster update from
    # the client.
    conn.Aliasing.SetAliases({handle: 'oh! the huge manatee!'})
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()
    assertEquals(contact, item['jid'])
    assertEquals('oh! the huge manatee!', item['name'])

    # Before the server responds, we block and remove the contact. The edits
    # should be queued...
    patterns = [
        EventPattern('stream-iq', query_ns=ns.ROSTER),
        EventPattern('stream-presence', presence_type='unsubscribed'),
        EventPattern('stream-presence', presence_type='unsubscribe'),
        ]
    q.forbid_events(patterns)

    call_async(q, conn.ContactBlocking, 'BlockContacts', [handle], "")
    call_async(q, conn.ContactList, 'RemoveContacts', [handle])

    # Make sure if the edits are sent prematurely, we've got them.
    sync_stream(q, stream)
    q.unforbid_events(patterns)

    # Okay, now we respond to the alias update. At this point we expect an
    # update to gr:t=B, leaving subscription=both intact, and subscription
    # cancellations.
    acknowledge_iq(stream, event.stanza)
    roster_event, _, _ = q.expect_many(*patterns)

    item = roster_event.query.firstChildElement()
    assertEquals(contact, item['jid'])
    assertEquals('B', item[(ns.GOOGLE_ROSTER, 't')])

    # And we're done. Clean up.
    q.unforbid_events(remove_events)

def test_deny_unblock_remove(q, bus, conn, stream):
    """
    Test unblocking a contact, and, while that request is pending, deleting
    them.
    """

    # This contact was on our roster, blocked and subscribed, when we started.
    contact = 'music-is-math@boards.ca'
    handle = conn.get_contact_handle_sync(contact)

    check_contact_roster(conn, contact, [],
            cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES)

    # They're blocked, and we have a bidi subscription, so they should be on
    # deny and stored. (We already checked this earlier, but we've been messing
    # with the roster so let's be sure the preconditions are okay...)
    assertContains(handle,
        conn.ContactBlocking.RequestBlockedContacts().keys())

    # Unblock them.
    call_async(q, conn.ContactBlocking, 'UnblockContacts', [handle])

    roster_event = q.expect('stream-iq', query_ns=ns.ROSTER)
    item = roster_event.query.firstChildElement()
    assertEquals(contact, item['jid'])
    assertDoesNotContain((ns.GOOGLE_ROSTER, 't'), item.attributes)

    # If we now remove them from stored, the edit shouldn't be sent until the
    # unblock event has had a reply.
    q.forbid_events(remove_events)
    call_async(q, conn.ContactList, 'RemoveContacts', [handle])

    # Make sure if the remove is sent prematurely, we catch it.
    sync_stream(q, stream)
    q.unforbid_events(remove_events)

    # So now we send a roster push and reply for the unblock request.
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
        'both', False, attrs={}))
    acknowledge_iq(stream, roster_event.stanza)

    # And on receiving the push and reply, Gabble should show them being
    # removed from deny, and send a remove.

    _, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='BlockedContactsChanged',
            predicate=lambda e: blocked_contacts_changed_predicate(e, [], [contact])),
        remove_events[0],
        )
    item = roster_event.query.firstChildElement()
    assertEquals(contact, item['jid'])

    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
        'remove', False, attrs={}))
    acknowledge_iq(stream, roster_event.stanza)

def test_contact_blocking(q, bus, conn, stream):
    """test ContactBlocking API"""
    assertContains(cs.CONN_IFACE_CONTACT_BLOCKING,
        conn.Properties.Get(cs.CONN, "Interfaces"))

    # 3 contacts are blocked
    blocked = conn.RequestBlockedContacts(dbus_interface=cs.CONN_IFACE_CONTACT_BLOCKING)

    assertLength(3, blocked)

def test(q, bus, conn, stream):
    test_inital_roster(q, bus, conn, stream)

    test_flickering(q, bus, conn, stream)
    test_local_pending(q, bus, conn, stream)
    test_deny_simple(q, bus, conn, stream)
    test_deny_overlap_one(q, bus, conn, stream)
    test_deny_overlap_two(q, bus, conn, stream)
    test_deny_unblock_remove(q, bus, conn, stream)
    test_contact_blocking(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test, protocol=GoogleXmlStream)
