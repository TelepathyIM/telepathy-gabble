# vim: set fileencoding=utf-8 :
"""
Test workarounds for gtalk
"""

from gabbletest import (
    acknowledge_iq, exec_test, sync_stream, make_result_iq, GoogleXmlStream,
    )
from rostertest import (
    expect_contact_list_signals, check_contact_list_signals,
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

def is_stored(event):
    return event.path.endswith('/stored')

def is_subscribe(event):
    return event.path.endswith('/subscribe')

def is_publish(event):
    return event.path.endswith('/publish')

def is_deny(event):
    return event.path.endswith('/deny')

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

    mutually_subscribed_contacts = ['lp-bug-398293@gmail.com',
        'blocked-but-subscribed@boards.ca',
        'music-is-math@boards.ca']
    rp_contacts = ['this-is-a-jid@badger.com']
    blocked_contacts = ['blocked-but-subscribed@boards.ca',
        'blocked-and-no-sub@boards.ca',
        'music-is-math@boards.ca']

    pairs = expect_contact_list_signals(q, bus, conn,
            ['publish', 'subscribe', 'stored', 'deny'])

    publish = check_contact_list_signals(q, bus, conn, pairs.pop(0),
            cs.HT_LIST, 'publish', mutually_subscribed_contacts)
    subscribe = check_contact_list_signals(q, bus, conn, pairs.pop(0),
            cs.HT_LIST, 'subscribe', mutually_subscribed_contacts,
            rp_contacts=rp_contacts)
    stored = check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'stored', mutually_subscribed_contacts + rp_contacts)
    deny = check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'deny', blocked_contacts)

    assertLength(0, pairs)      # i.e. we've checked all of them

    return (publish, subscribe, stored, deny)

def test_flickering(q, bus, conn, stream, subscribe):
    """
    Google's server is buggy, and subscription state transitions "flicker"
    sometimes. Here, we test that Gabble is suppressing the flickers.
    """

    self_handle = conn.GetSelfHandle()
    contact = 'bob@foo.com'
    handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@foo.com'])[0]

    # request subscription
    subscribe.Group.AddMembers([handle], '')

    event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()
    assertEquals(contact, item['jid'])

    acknowledge_iq(stream, event.stanza)

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
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [handle], [], [], [], 0, cs.GC_REASON_NONE],
            predicate=is_stored),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [], [], [], [handle], self_handle, cs.GC_REASON_NONE],
            predicate=is_subscribe),
        EventPattern('dbus-signal', signal='ContactsChanged',
            args=[{handle:
                (cs.SUBSCRIPTION_STATE_ASK, cs.SUBSCRIPTION_STATE_NO, ''),
                }, []]),
        )

    # Gabble shouldn't report any changes to subscribe or stored's members in
    # response to the next two roster updates.
    change_events = [
        EventPattern('dbus-signal', signal='MembersChanged',
            predicate=is_subscribe),
        EventPattern('dbus-signal', signal='MembersChanged',
            predicate=is_stored),
        EventPattern('dbus-signal', signal='ContactsChanged'),
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
    q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [handle], [], [], [], handle, cs.GC_REASON_NONE],
            predicate=is_subscribe),
        EventPattern('dbus-signal', signal='ContactsChanged',
            args=[{handle:
                (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
                }, []]),
        )

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

# This event is forbidden in all of the deny tests!
remove_events = [
    EventPattern('stream-iq', query_ns=ns.ROSTER,
        predicate=(lambda event:
            event.query.firstChildElement()['subscription'] == 'remove'),
        )
    ]

def test_deny_simple(q, bus, conn, stream, stored, deny):
    """
    If we remove a blocked contact from 'stored', they shouldn't actually be
    removed from the roster: rather, we should cancel both subscription
    directions, at which point they will vanish from 'stored', while
    remaining on 'deny'.
    """
    contact = 'blocked-but-subscribed@boards.ca'
    handle = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]
    assertContains(handle,
        stored.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))
    stored.Group.RemoveMembers([handle], "")

    q.forbid_events(remove_events)

    q.expect_many(
        EventPattern('stream-presence', to=contact,
            presence_type='unsubscribe'),
        EventPattern('stream-presence', to=contact,
            presence_type='unsubscribed'),
        )

    # Our server sends roster pushes in response to our unsubscribe and
    # unsubscribed commands.
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "from", False, attrs={'gr:t': 'B'}))
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False, attrs={'gr:t': 'B'}))

    # As a result they should drop off all three non-deny lists, but not fall
    # off deny:
    q.expect_many(
        EventPattern('dbus-signal', signal='ContactsChanged',
            args=[{}, [handle]]),
        *[ EventPattern('dbus-signal', signal='MembersChanged',
                        args=['', [], [handle], [], [], 0, cs.GC_REASON_NONE],
                        predicate=p)
           for p in [is_stored, is_subscribe, is_publish]
         ])

    assertContains(handle,
        deny.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))

    q.unforbid_events(remove_events)

def test_deny_overlap_one(q, bus, conn, stream, subscribe, stored, deny):
    """
    Here's a tricker case: blocking a contact, and then removing them before
    the server's responded to the block request.
    """
    self_handle = conn.GetSelfHandle()

    # As we saw in test_flickering(), we have a subscription to Bob,
    # everything's peachy.
    contact = 'bob@foo.com'
    handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@foo.com'])[0]

    assertContains(handle,
        stored.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))
    assertContains(handle,
        subscribe.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))

    q.forbid_events(remove_events)

    # But then we have a falling out. In a blind rage, I block Bob:
    call_async(q, deny.Group, 'AddMembers', [handle], "")
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()
    assertEquals(contact, item['jid'])
    assertEquals('B', item[(ns.GOOGLE_ROSTER, 't')])

    # Then — *before the server has replied* — I remove him from stored.
    call_async(q, stored.Group, 'RemoveMembers', [handle], "")

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
        EventPattern('dbus-signal', signal='MembersChanged', predicate=is_deny,
            args=["", [handle], [], [], [], self_handle, 0]),
        EventPattern('stream-presence', to=contact,
            presence_type='unsubscribe'),
        )

    # And our server sends us a roster push in response to unsubscribe:
    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
            "none", False, attrs={'gr:t': 'B'}))

    # As a result, Gabble makes Bob fall off subscribe and stored.
    q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            predicate=is_subscribe,
            args=["", [], [handle], [], [], 0, 0]),
        EventPattern('dbus-signal', signal='MembersChanged',
            predicate=is_stored,
            args=["", [], [handle], [], [], 0, 0]),
        EventPattern('dbus-signal', signal='ContactsChanged',
            args=[{}, [handle]]),
        )

    # And he should definitely still be on deny. That rascal.
    assertContains(handle,
        deny.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))

    q.unforbid_events(unsubscribed_events)
    q.unforbid_events(remove_events)

def test_deny_overlap_two(q, bus, conn, stream,
                          subscribe, publish, stored, deny):
    """
    Here's another tricky case: editing a contact (setting an alias, say), and
    then while that edit's in flight, blocking and remove the contact.
    """

    # This contact was on our roster when we started.
    contact = 'lp-bug-398293@gmail.com'
    handle = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]

    assertContains(handle,
        stored.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))
    assertContains(handle,
        subscribe.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))
    assertContains(handle,
        publish.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))

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

    deny.Group.AddMembers([handle], '')
    stored.Group.RemoveMembers([handle], '')

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

def test_deny_unblock_remove(q, bus, conn, stream, stored, deny):
    """
    Test unblocking a contact, and, while that request is pending, deleting
    them.
    """
    self_handle = conn.GetSelfHandle()

    # This contact was on our roster, blocked and subscribed, when we started.
    contact = 'music-is-math@boards.ca'
    handle = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]

    # They're blocked, and we have a bidi subscription, so they should be on
    # deny and stored. (We already checked this earlier, but we've been messing
    # with the roster so let's be sure the preconditions are okay...)
    assertContains(handle,
        deny.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))
    assertContains(handle,
        stored.Properties.Get(cs.CHANNEL_IFACE_GROUP, "Members"))

    # Unblock them.
    deny.Group.RemoveMembers([handle], '')

    roster_event = q.expect('stream-iq', query_ns=ns.ROSTER)
    item = roster_event.query.firstChildElement()
    assertEquals(contact, item['jid'])
    assertDoesNotContain((ns.GOOGLE_ROSTER, 't'), item.attributes)

    # If we now remove them from stored, the edit shouldn't be sent until the
    # unblock event has had a reply.
    q.forbid_events(remove_events)
    stored.Group.RemoveMembers([handle], '')

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
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [], [handle], [], [], self_handle, cs.GC_REASON_NONE],
            predicate=is_deny),
        remove_events[0],
        )
    item = roster_event.query.firstChildElement()
    assertEquals(contact, item['jid'])

    stream.send(make_set_roster_iq(stream, 'test@localhost/Resource', contact,
        'remove', False, attrs={}))
    acknowledge_iq(stream, roster_event.stanza)

    q.expect('dbus-signal', signal='MembersChanged',
        args=['', [], [handle], [], [], 0, cs.GC_REASON_NONE],
        predicate=is_stored)


def test(q, bus, conn, stream):
    conn.Connect()

    publish, subscribe, stored, deny = test_inital_roster(q, bus, conn, stream)

    test_flickering(q, bus, conn, stream, subscribe)
    test_deny_simple(q, bus, conn, stream, stored, deny)
    test_deny_overlap_one(q, bus, conn, stream, subscribe, stored, deny)
    test_deny_overlap_two(q, bus, conn, stream,
        subscribe, publish, stored, deny)
    test_deny_unblock_remove(q, bus, conn, stream, stored, deny)

if __name__ == '__main__':
    exec_test(test, protocol=GoogleXmlStream)
