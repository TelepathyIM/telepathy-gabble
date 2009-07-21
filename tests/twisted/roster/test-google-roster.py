"""
Test workarounds for gtalk
"""

import dbus

from gabbletest import (
    acknowledge_iq, exec_test, sync_stream, make_result_iq, GoogleXmlStream,
    expect_list_channel
    )
from servicetest import (
    sync_dbus, EventPattern, wrap_channel,
    assertLength, assertEquals, assertContains,
    )
import constants as cs
import ns

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish

def make_set_roster_iq(stream, user, contact, state, ask):
    iq = IQ(stream, 'set')
    query = iq.addElement((ns.ROSTER, 'query'))
    add_gr_attributes(query)
    add_roster_item(query, contact, state, ask)
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

def test(q, bus, conn, stream):
    conn.Connect()

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
    add_roster_item(query, 'lp-bug-298293@gmail.com', 'both', False,
        {'gr:autosub': 'true'})

    # Send back the roster
    stream.send(result)

    # This depends on the order in which roster.c creates the channels.
    # Since s-b-h had the "hidden" flag set, we don't expect them to be on any
    # lists. But we do want the "autosub" contact to be visible; see
    # <https://bugs.launchpad.net/ubuntu/+source/telepathy-gabble/+bug/398293>,
    # where Gabble was incorrectly hiding valid contacts.

    expected_contacts = ['lp-bug-298293@gmail.com']
    rp_contacts = ['this-is-a-jid@badger.com']

    publish = expect_list_channel(q, bus, conn, 'publish', expected_contacts)
    subscribe = expect_list_channel(q, bus, conn, 'subscribe',
        expected_contacts, rp_contacts=rp_contacts)
    stored = expect_list_channel(q, bus, conn, 'stored',
        expected_contacts+rp_contacts)

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

    def is_stored(event):
        return event.path.endswith('/stored')

    def is_subscribe(event):
        return event.path.endswith('/subscribe')

    # Gabble should report this update to the UI.
    q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [handle], [], [], [], 0, cs.GC_REASON_NONE],
            predicate=is_stored),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [], [], [], [handle], 0, cs.GC_REASON_NONE],
            predicate=is_subscribe),
        )

    # Gabble shouldn't report any changes to subscribe or stored's members in
    # response to the next two roster updates.
    change_events = [
        EventPattern('dbus-signal', signal='MembersChanged',
            predicate=is_subscribe),
        EventPattern('dbus-signal', signal='MembersChanged',
            predicate=is_stored),
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
    q.expect('dbus-signal', signal='MembersChanged',
        args=['', [handle], [], [], [], 0, cs.GC_REASON_NONE],
        predicate=is_subscribe)

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

if __name__ == '__main__':
    exec_test(test, protocol=GoogleXmlStream)
