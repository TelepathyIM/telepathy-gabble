"""
Test basic roster group functionality.
"""

import dbus

from gabbletest import exec_test, acknowledge_iq
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import assertLength, EventPattern, assertEquals, call_async
import constants as cs
import ns

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import xpath

def test(q, bus, conn, stream):
    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    group = item.addElement('group', content='women')

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'
    group = item.addElement('group', content='men')

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'
    group = item.addElement('group', content='men')

    stream.send(event.stanza)

    # slight implementation detail: TpBaseContactList emits ContactsChanged
    # etc. before it announces its channels, and it emits one CGC per group.
    s1, s2 = q.expect_many(
        EventPattern('dbus-signal', signal='GroupsChanged',
            interface=cs.CONN_IFACE_CONTACT_GROUPS, path=conn.object_path,
            predicate=lambda e: 'women' in e.args[1]),
        EventPattern('dbus-signal', signal='GroupsChanged',
            interface=cs.CONN_IFACE_CONTACT_GROUPS, path=conn.object_path,
            predicate=lambda e: 'men' in e.args[1]),
        )

    amy, bob, che = conn.RequestHandles(cs.HT_CONTACT,
            ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    assertEquals([[amy], ['women'], []], s1.args)
    assertEquals([[bob, che], ['men'], []], s2.args)

    pairs = expect_contact_list_signals(q, bus, conn, [], ['men', 'women'])

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

    item = iq.query.firstChildElement()
    assertEquals('amy@foo.com', item['jid'])

    groups = set()

    for gn in xpath.queryForNodes('/iq/query/item/group', iq.stanza):
        groups.add(str(gn))

    assertEquals(set(('ladies', 'people starting with A')), groups)

    acknowledge_iq(stream, iq.stanza)
    q.expect('dbus-return', method='SetContactGroups')

    # Now the server sends us a roster push.
    iq = IQ(stream, 'set')
    query = iq.addElement((ns.ROSTER, 'query'))
    item = query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='people starting with A')
    item.addElement('group', content='ladies')
    stream.send(iq)

    # FIXME: ideally, this would emit a single GroupsChanged, but as a relic
    # of the Chan.T.ContactList code, it splits up updates by group
    q.expect_many(
        EventPattern('dbus-signal', signal='GroupsChanged',
            args=[[amy], [], ['women']]),
        EventPattern('dbus-signal', signal='GroupsChanged',
            args=[[amy], ['people starting with A'], []]),
        EventPattern('dbus-signal', signal='GroupsChanged',
            args=[[amy], ['ladies'], []]),
        )

if __name__ == '__main__':
    exec_test(test)
