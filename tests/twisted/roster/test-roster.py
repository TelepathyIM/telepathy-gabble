"""
Test basic roster functionality.
"""

from gabbletest import exec_test
from rostertest import expect_contact_list_signals, check_contact_list_signals
from servicetest import (assertEquals, assertLength, call_async)
import constants as cs
import ns

def test(q, bus, conn, stream):

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    q.expect('dbus-error', method='GetContactListAttributes',
            name=cs.NOT_YET)

    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'

    stream.send(event.stanza)

    # slight implementation detail: TpBaseContactList emits ContactsChanged
    # before it announces its channels
    s = q.expect('dbus-signal', signal='ContactsChanged',
            interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path)

    amy, bob, che = conn.RequestHandles(cs.HT_CONTACT,
            ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    assertEquals([{
        amy: (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        bob: (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_YES, ''),
        che: (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
        }, []], s.args)

    pairs = expect_contact_list_signals(q, bus, conn,
            ['publish', 'subscribe', 'stored'])

    # this is emitted last, so clients can tell when the initial state dump
    # has finished
    q.expect('dbus-signal', signal='ContactListStateChanged',
            args=[cs.CONTACT_LIST_STATE_SUCCESS])

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertEquals(({
        amy: {
            cs.CONN_IFACE_CONTACT_LIST + '/subscribe':
                cs.SUBSCRIPTION_STATE_YES,
            cs.CONN_IFACE_CONTACT_LIST + '/publish': cs.SUBSCRIPTION_STATE_YES,
            cs.CONN + '/contact-id': 'amy@foo.com',
            },
        bob: {
            cs.CONN_IFACE_CONTACT_LIST + '/subscribe':
                cs.SUBSCRIPTION_STATE_NO,
            cs.CONN_IFACE_CONTACT_LIST + '/publish': cs.SUBSCRIPTION_STATE_YES,
            cs.CONN + '/contact-id': 'bob@foo.com',
            },
        che: {
            cs.CONN_IFACE_CONTACT_LIST + '/subscribe':
                cs.SUBSCRIPTION_STATE_YES,
            cs.CONN_IFACE_CONTACT_LIST + '/publish': cs.SUBSCRIPTION_STATE_NO,
            cs.CONN + '/contact-id': 'che@foo.com',
            },
        },), r.value)

    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'publish', ['amy@foo.com', 'bob@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'subscribe', ['amy@foo.com', 'che@foo.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'stored', ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    assertLength(0, pairs)      # i.e. we've checked all of them

if __name__ == '__main__':
    exec_test(test)
