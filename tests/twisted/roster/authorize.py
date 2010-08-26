"""
Test receiving and authorizing publish requests, including "pre-authorization"
(authorizing publication before someone asks for it).
"""

from gabbletest import exec_test
from rostertest import (expect_contact_list_signals,
        check_contact_list_signals)
from servicetest import (assertEquals, assertLength, call_async, EventPattern)
import constants as cs
import ns

from twisted.words.xish import domish

def test(q, bus, conn, stream):

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    q.expect('dbus-error', method='GetContactListAttributes',
            name=cs.NOT_YET)

    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    item = event.query.addElement('item')
    item['jid'] = 'holly@example.com'
    item['subscription'] = 'both'
    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    holly, dave, arnold, kristine = conn.RequestHandles(cs.HT_CONTACT,
            ['holly@example.com', 'dave@example.com', 'arnold@example.com',
                'kristine@example.com'])

    # slight implementation detail: TpBaseContactList emits ContactsChanged
    # before it announces its channels
    s = q.expect('dbus-signal', signal='ContactsChanged',
            interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path)
    assertEquals([{
        holly: (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
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
        holly: {
            cs.CONN_IFACE_CONTACT_LIST + '/publish':
                cs.SUBSCRIPTION_STATE_YES,
            cs.CONN_IFACE_CONTACT_LIST + '/subscribe':
                cs.SUBSCRIPTION_STATE_YES,
            cs.CONN + '/contact-id': 'holly@example.com',
            }
        },), r.value)

    # check that the channels were as we expected too
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'publish', ['holly@example.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'subscribe', ['holly@example.com'])
    check_contact_list_signals(q, bus, conn, pairs.pop(0), cs.HT_LIST,
            'stored', ['holly@example.com'])
    assertLength(0, pairs)      # i.e. we've checked all of them

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
            EventPattern('dbus-signal', signal='ContactsChanged',
                args=[{dave: (cs.SUBSCRIPTION_STATE_NO,
                    cs.SUBSCRIPTION_STATE_ASK,
                    '')}, []]),
            EventPattern('stream-presence', presence_type='subscribed',
                to='dave@example.com'),
            )

    presence['from'] = 'kristine@example.com'
    stream.send(presence)

    q.expect('dbus-signal', signal='ContactsChanged',
            args=[{kristine: (cs.SUBSCRIPTION_STATE_NO,
                cs.SUBSCRIPTION_STATE_ASK, '')}, []])

    presence['from'] = 'arnold@example.com'
    stream.send(presence)

    q.expect('dbus-signal', signal='ContactsChanged',
            args=[{arnold: (cs.SUBSCRIPTION_STATE_NO,
                cs.SUBSCRIPTION_STATE_ASK, '')}, []])

    call_async(q, conn.ContactList, 'AuthorizePublication', [kristine, holly])

    q.expect_many(
            EventPattern('dbus-return', method='AuthorizePublication'),
            EventPattern('stream-presence', presence_type='subscribed',
                to='kristine@example.com'),
            )

    # Arnold gives up waiting for us, and cancels his request
    presence['from'] = 'arnold@example.com'
    presence['type'] = 'unsubscribe'
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChanged',
                args=[{arnold: (cs.SUBSCRIPTION_STATE_NO,
                    cs.SUBSCRIPTION_STATE_REMOVED_REMOTELY, '')}, []]),
            EventPattern('stream-presence', presence_type='unsubscribed',
                to='arnold@example.com'),
            )

    # We can acknowledge that with RemoveContacts or with Unpublish
    # FIXME: test RemoveContacts here

    call_async(q, conn.ContactList, 'Unpublish', [arnold])

    # FIXME: strictly speaking, Arnold was never on our XMPP roster,
    # so setting his publish state to SUBSCRIPTION_STATE_NO should result
    # in his removal (as seen in the telepathy-glib contactlist example)
    q.expect_many(
            EventPattern('dbus-return', method='Unpublish'),
            EventPattern('dbus-signal', signal='ContactsChanged',
                args=[{
                    arnold:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_NO,
                            ''),
                    }, []]),
            )

if __name__ == '__main__':
    exec_test(test)
