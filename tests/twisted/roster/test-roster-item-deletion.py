"""
Regression test for http://bugs.freedesktop.org/show_bug.cgi?id=19524
"""

import dbus

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish

from gabbletest import exec_test, acknowledge_iq
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import (assertLength, assertEquals, EventPattern, call_async)
import constants as cs
import ns

def test_ancient(q, bus, conn, stream):
    test(q, bus, conn, stream, False)

def test_modern(q, bus, conn, stream):
    test(q, bus, conn, stream, True)

def test(q, bus, conn, stream, modern=True):
    conn.Connect()

    def send_roster_iq(stream, jid, subscription):
        iq = IQ(stream, "set")
        iq['id'] = 'push'
        query = iq.addElement('query')
        query['xmlns'] = ns.ROSTER
        item = query.addElement('item')
        item['jid'] = jid
        item['subscription'] = subscription
        stream.send(iq)

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'quux@foo.com'
    item['subscription'] = 'none'

    quux_handle = conn.RequestHandles(cs.HT_CONTACT, ['quux@foo.com'])[0]

    stream.send(event.stanza)

    # slight implementation detail: TpBaseContactList emits ContactsChanged
    # before it announces its channels
    q.expect('dbus-signal', signal='ContactsChanged',
            interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path,
            args=[{quux_handle:
                (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_NO, '')}, []])

    pairs = expect_contact_list_signals(q, bus, conn,
            ['publish', 'subscribe', 'stored'])

    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'publish', [])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'subscribe', [])
    stored = check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'stored', ['quux@foo.com'])

    assertLength(0, pairs)      # i.e. we've checked all of them

    expectations = [
            EventPattern('stream-iq',
            iq_type='set', query_ns=ns.ROSTER),
            ]

    if modern:
        call_async(q, conn.ContactList, 'RemoveContacts', [quux_handle])
    else:
        call_async(q, stored.Group, 'RemoveMembers', [quux_handle], '')
        expectations.append(EventPattern('dbus-return',
            method='RemoveMembers'))

    event = q.expect_many(*expectations)[0]
    item = event.query.firstChildElement()
    assertEquals('quux@foo.com', item['jid'])
    assertEquals('remove', item['subscription'])

    send_roster_iq(stream, 'quux@foo.com', 'remove')

    q.expect_many(
            EventPattern('dbus-signal', interface=cs.CHANNEL_IFACE_GROUP,
                path=stored.object_path, signal='MembersChanged',
                args=['', [], [quux_handle], [], [], 0, 0]),
            EventPattern('dbus-signal', interface=cs.CONN_IFACE_CONTACT_LIST,
                path=conn.object_path, signal='ContactsChanged',
                args=[{}, [quux_handle]]),
            EventPattern('stream-iq', iq_id='push', iq_type='result'),
            )

    acknowledge_iq(stream, event.stanza)

    if modern:
        q.expect('dbus-return', method='RemoveContacts')

if __name__ == '__main__':
    exec_test(test_ancient)
    exec_test(test_modern)
