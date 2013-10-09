"""
Test basic roster functionality.
"""

from gabbletest import exec_test
from rostertest import check_contact_roster, contacts_changed_predicate
from servicetest import (assertEquals, call_async)
import constants as cs
import ns

def test(q, bus, conn, stream):

    call_async(q, conn.ContactList, 'GetContactListAttributes', [])
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

    # Regression test for <https://bugs.freedesktop.org/show_bug.cgi?id=42186>:
    # some super-buggy XMPP server running on vk.com sends its reply to our
    # roster query twice. This used to crash Gabble.
    stream.send(event.stanza)

    contacts = [
        ('amy@foo.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        ('bob@foo.com', cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_YES, ''),
        ('che@foo.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
        ]

    # slight implementation detail: TpBaseContactList emits ContactsChanged
    # before it announces its channels
    q.expect('dbus-signal', signal='ContactsChanged',
            interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path,
            predicate=lambda e: contacts_changed_predicate(e, conn, contacts))

    amy, bob, che = conn.get_contact_handles_sync(
            ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    # this is emitted last, so clients can tell when the initial state dump
    # has finished
    q.expect('dbus-signal', signal='ContactListStateChanged',
            args=[cs.CONTACT_LIST_STATE_SUCCESS])

    call_async(q, conn.ContactList, 'GetContactListAttributes', [])
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

if __name__ == '__main__':
    exec_test(test)
