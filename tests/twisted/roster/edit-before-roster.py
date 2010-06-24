"""
Test editing the roster before we've received it.
"""

import dbus

from gabbletest import exec_test, acknowledge_iq, sync_stream
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import (assertLength, EventPattern, assertEquals, call_async,
        sync_dbus)
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

    # Before we get the roster, try to change something. It won't work.
    amy, bob, che = conn.RequestHandles(cs.HT_CONTACT,
            ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    call_async(q, conn.ContactGroups, 'AddToGroup', 'Amy & Bob', [amy, bob])
    q.expect('dbus-error', method='AddToGroup', name=cs.NOT_YET)

    # Now send the roster, and things will happen.
    stream.send(event.stanza)

    def mentions_amy(e):
        return bool(xpath.queryForNodes('/iq/query/item[@jid = "amy@foo.com"]',
            e.stanza))

    def mentions_bob(e):
        return bool(xpath.queryForNodes('/iq/query/item[@jid = "bob@foo.com"]',
            e.stanza))

    s1, s2, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupsChanged',
            interface=cs.CONN_IFACE_CONTACT_GROUPS, path=conn.object_path,
            predicate=lambda e: 'women' in e.args[1]),
        EventPattern('dbus-signal', signal='GroupsChanged',
            interface=cs.CONN_IFACE_CONTACT_GROUPS, path=conn.object_path,
            predicate=lambda e: 'men' in e.args[1]),
        EventPattern('dbus-signal', signal='ContactListStateChanged',
            args=[cs.CONTACT_LIST_STATE_SUCCESS]),
        )

    call_async(q, conn.ContactGroups, 'AddToGroup', 'Amy & Bob', [amy, bob])

    set1, set2 = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.ROSTER, query_name='query',
            iq_type='set', predicate=mentions_amy),
        EventPattern('stream-iq', query_ns=ns.ROSTER, query_name='query',
            iq_type='set', predicate=mentions_bob),
        )

    assertEquals([[amy], ['women'], []], s1.args)
    assertEquals([[bob, che], ['men'], []], s2.args)

    item = set1.query.firstChildElement()
    assertEquals('amy@foo.com', item['jid'])

    groups = set()

    for gn in xpath.queryForNodes('/iq/query/item/group', set1.stanza):
        groups.add(str(gn))

    assertEquals(set(('Amy & Bob', 'women')), groups)

    item = set2.query.firstChildElement()
    assertEquals('bob@foo.com', item['jid'])

    groups = set()

    for gn in xpath.queryForNodes('/iq/query/item/group', set2.stanza):
        groups.add(str(gn))

    assertEquals(set(('Amy & Bob', 'men')), groups)

    # Send a couple of roster pushes to reflect Amy and Bob's new states.

    iq = IQ(stream, 'set')
    query = iq.addElement((ns.ROSTER, 'query'))
    item = query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='women')
    item.addElement('group', content='Amy & Bob')
    stream.send(iq)

    q.expect('dbus-signal', signal='GroupsChanged',
            args=[[amy], ['Amy & Bob'], []])

    iq = IQ(stream, 'set')
    query = iq.addElement((ns.ROSTER, 'query'))
    item = query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='men')
    item.addElement('group', content='Amy & Bob')
    stream.send(iq)

    q.expect('dbus-signal', signal='GroupsChanged',
            args=[[bob], ['Amy & Bob'], []])

    # Acknowledge Amy's IQ, but make sure we don't see the acknowledgement
    # until Bob's IQ has replied too
    acknowledge_iq(stream, set1.stanza)
    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

    # When we get the reply for Bob, it actually happens.
    acknowledge_iq(stream, set2.stanza)

    q.expect('dbus-return', method='AddToGroup')

if __name__ == '__main__':
    exec_test(test)
