"""
Test basic roster group functionality.
"""

from gabbletest import exec_test, acknowledge_iq, sync_stream
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import (assertLength, EventPattern, assertEquals, call_async,
        sync_dbus, assertContains, assertDoesNotContain)
import constants as cs
import ns

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import xpath

def parse_roster_change_request(query, iq):
    item = query.firstChildElement()

    groups = set()

    for gn in xpath.queryForNodes('/iq/query/item/group', iq):
        groups.add(str(gn))

    return item['jid'], groups

def send_roster_push(stream, jid, groups):
    iq = IQ(stream, 'set')
    query = iq.addElement((ns.ROSTER, 'query'))
    item = query.addElement('item')
    item['jid'] = jid
    item['subscription'] = 'both'
    for group in groups:
        item.addElement('group', content=group)
    stream.send(iq)

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='women')

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'
    item.addElement('group', content='men')

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'
    item.addElement('group', content='men')

    stream.send(event.stanza)

    # Avoid relying on the implementation detail of exactly when
    # TpBaseContactList emits ContactsChanged, relative to when it
    # announces its channels. Prior to 0.20.3, 0.21.1 it would
    # announce the channels, emit GroupsChanged, then announce the channels
    # again... which was a bug, but it turned out this test relied on it.
    #
    # We do still rely on the implementation detail that we emit GroupsChanged
    # once per group with all of its members, not once per contact with all
    # of their groups. On a typical contact list, there are more contacts
    # than groups, so that'll work out smaller.

    pairs, groups_changed = expect_contact_list_signals(q, bus, conn, [],
            ['men', 'women'],
            [
                EventPattern('dbus-signal', signal='GroupsChanged',
                    interface=cs.CONN_IFACE_CONTACT_GROUPS,
                    path=conn.object_path,
                    predicate=lambda e: 'women' in e.args[1]),
                EventPattern('dbus-signal', signal='GroupsChanged',
                    interface=cs.CONN_IFACE_CONTACT_GROUPS,
                    path=conn.object_path,
                    predicate=lambda e: 'men' in e.args[1]),
            ])

    amy, bob, che = conn.get_contact_handles_sync(
            ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    assertEquals([[amy], ['women'], []], groups_changed[0].args)
    assertEquals([[bob, che], ['men'], []], groups_changed[1].args)

    q.expect('dbus-signal', signal='ContactListStateChanged',
            args=[cs.CONTACT_LIST_STATE_SUCCESS])

    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_GROUP,
            'men', ['bob@foo.com', 'che@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_GROUP,
            'women', ['amy@foo.com'])

    assertLength(0, pairs)      # i.e. we've checked all of them

    # change Amy's groups
    call_async(q, conn.ContactGroups, 'SetContactGroups', amy,
            ['ladies', 'people starting with A'])

    s, iq = q.expect_many(
        EventPattern('dbus-signal', signal='GroupsCreated'),
        EventPattern('stream-iq', iq_type='set',
            query_name='query', query_ns=ns.ROSTER),
        )

    assertEquals(set(('ladies', 'people starting with A')), set(s.args[0]))

    jid, groups = parse_roster_change_request(iq.query, iq.stanza)
    assertEquals('amy@foo.com', jid)
    assertEquals(set(('ladies', 'people starting with A')), groups)

    acknowledge_iq(stream, iq.stanza)
    q.expect('dbus-return', method='SetContactGroups')

    # Now the server sends us a roster push.
    send_roster_push(stream, 'amy@foo.com', ['people starting with A', 'ladies'])

    # We get a single signal corresponding to that roster push
    e = q.expect('dbus-signal', signal='GroupsChanged',
            predicate=lambda e: e.args[0] == [amy])
    assertEquals(set(['ladies', 'people starting with A']), set(e.args[1]))
    assertEquals(['women'], e.args[2])

    # check that Amy's state is what we expected
    attrs = conn.Contacts.GetContactAttributes([amy],
            [cs.CONN_IFACE_CONTACT_GROUPS], False)[amy]
    # make the group list order-independent
    attrs[cs.CONN_IFACE_CONTACT_GROUPS + '/groups'] = \
        set(attrs[cs.CONN_IFACE_CONTACT_GROUPS + '/groups'])

    assertEquals({ cs.CONN_IFACE_CONTACT_GROUPS + '/groups':
                set(['ladies', 'people starting with A']),
            cs.CONN + '/contact-id': 'amy@foo.com' }, attrs)

    for it_worked in (False, True):
        # remove a group with a member (the old API couldn't do this)
        call_async(q, conn.ContactGroups, 'RemoveGroup',
                'people starting with A')

        iq = q.expect('stream-iq', iq_type='set',
                query_name='query', query_ns=ns.ROSTER)

        jid, groups = parse_roster_change_request(iq.query, iq.stanza)
        assertEquals('amy@foo.com', jid)
        assertEquals(set(('ladies',)), groups)

        acknowledge_iq(stream, iq.stanza)

        # we emit these as soon as the IQ is ack'd, so that we can indicate
        # group removal...
        q.expect('dbus-signal', signal='GroupsRemoved',
                args=[['people starting with A']])
        q.expect('dbus-signal', signal='GroupsChanged',
                args=[[amy], [], ['people starting with A']])

        q.expect('dbus-return', method='RemoveGroup')

        if it_worked:
            # ... although in fact this is what *actually* removes Amy from the
            # group
            send_roster_push(stream, 'amy@foo.com', ['ladies'])
        else:
            # if the change didn't "stick", this message will revert it
            send_roster_push(stream, 'amy@foo.com', ['ladies', 'people starting with A'])

            q.expect('dbus-signal', signal='GroupsCreated',
                    args=[['people starting with A']])
            q.expect('dbus-signal', signal='GroupsChanged',
                    args=[[amy], ['people starting with A'], []])

            sync_dbus(bus, q, conn)
            sync_stream(q, stream)
            assertEquals({
                    cs.CONN_IFACE_CONTACT_GROUPS + '/groups':
                        ['ladies', 'people starting with A'],
                    cs.CONN + '/contact-id':
                        'amy@foo.com' },
                conn.Contacts.GetContactAttributes([amy],
                    [cs.CONN_IFACE_CONTACT_GROUPS], False)[amy])

    # sanity check: after all that, we expect Amy to be in group 'ladies' only
    sync_dbus(bus, q, conn)
    sync_stream(q, stream)
    assertEquals({ cs.CONN_IFACE_CONTACT_GROUPS + '/groups': ['ladies'],
            cs.CONN + '/contact-id': 'amy@foo.com' },
        conn.Contacts.GetContactAttributes([amy],
            [cs.CONN_IFACE_CONTACT_GROUPS], False)[amy])

    # Rename group 'ladies' to 'girls'
    call_async(q, conn.ContactGroups, 'RenameGroup', 'ladies', 'girls')

    # Amy is added to 'girls'
    e = q.expect('stream-iq', iq_type='set', query_name='query', query_ns=ns.ROSTER)
    jid, groups = parse_roster_change_request(e.query, e.stanza)
    assertEquals('amy@foo.com', jid)
    assertEquals(set(['girls', 'ladies']), groups)

    send_roster_push(stream, 'amy@foo.com', ['girls', 'ladies'])
    acknowledge_iq(stream, e.stanza)

    # Amy is removed from 'ladies'
    e = q.expect('stream-iq', iq_type='set', query_name='query', query_ns=ns.ROSTER)
    jid, groups = parse_roster_change_request(e.query, e.stanza)
    assertEquals('amy@foo.com', jid)
    assertEquals(set(['girls']), groups)

    send_roster_push(stream, 'amy@foo.com', ['girls'])
    acknowledge_iq(stream, e.stanza)

    q.expect('dbus-return', method='RenameGroup')

    # check everything has been updated
    groups = conn.Properties.Get(cs.CONN_IFACE_CONTACT_GROUPS, 'Groups')
    assertContains('girls', groups)
    assertDoesNotContain('ladies', groups)

    contacts = conn.ContactList.GetContactListAttributes([cs.CONN_IFACE_CONTACT_GROUPS], False)
    assertEquals(['girls'], contacts[amy][cs.CONN_IFACE_CONTACT_GROUPS + '/groups'])

if __name__ == '__main__':
    exec_test(test)
