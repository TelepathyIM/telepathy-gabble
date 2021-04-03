"""
Test basic roster functionality.
"""

from gabbletest import exec_test, make_result_iq, XmppAuthenticator
from rostertest import check_contact_roster, contacts_changed_predicate, make_roster_push
from servicetest import (assertEquals, call_async)
import constants as cs
import ns

contacts = [
        ('amy@foo.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        ('bob@foo.com', cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_YES, ''),
        ('che@foo.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
        ]

def verify(q, conn, groups=set()):
    # slight implementation detail: TpBaseContactList emits ContactsChanged
    # before it announces its channels
    q.expect('dbus-signal', signal='ContactsChangedWithID',
            interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path,
            predicate=lambda e: contacts_changed_predicate(e, conn, contacts))

    if groups:
        s = q.expect('dbus-signal', signal='GroupsCreated')
        assertEquals(set(groups), set(s.args[0]))


    handles = conn.get_contact_handles_sync([ c[0] for c in contacts])

    # this is emitted last, so clients can tell when the initial state dump
    # has finished
    q.expect('dbus-signal', signal='ContactListStateChanged',
            args=[cs.CONTACT_LIST_STATE_SUCCESS])

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    attribs = dict( 
            (handles[i],{
                cs.CONN_IFACE_CONTACT_LIST + '/subscribe': contacts[i][1],
                cs.CONN_IFACE_CONTACT_LIST + '/publish': contacts[i][2],
                cs.CONN + '/contact-id': contacts[i][0]
            }) for i in range(0,len(contacts)) )
    assertEquals((attribs,), r.value)

def test_create(q, bus, conn, stream):

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    q.expect('dbus-error', method='GetContactListAttributes',
            name=cs.NOT_YET)

    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    event.stanza['type'] = 'result'
    event.stanza.query['ver']='zero'

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

    verify(q, conn)

def test_cache1(q, bus, conn, stream):
    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    q.expect('dbus-error', method='GetContactListAttributes',
            name=cs.NOT_YET)

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    assert event.stanza.query['ver'] == 'zero', event.stanza.query

    stream.send(make_result_iq(stream, event.stanza, False))

    verify(q, conn)

    amy, bob, che = conn.get_contact_handles_sync(
            ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

    ## Technically roster is only cached on server push, as server needs to
    ## push version tag. Below is just to test end to end flow, but server
    ## push alone (as if from the other client) is sufficient.

    # Remove che from this client
    call_async(q, conn.ContactList, 'RemoveContacts', [che])

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    # Server needs to confirm removal
    push = make_roster_push(stream, 'che@foo.com', 'remove')
    push.query['ver'] = 'one'
    stream.send(push)
    # And ack the push
    stream.send(make_result_iq(stream, event.stanza, False))

    # Now wait for contact to be removed from the roster
    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{},{},{che: 'che@foo.com'}])

    # And bob was removed from another client, here's server's confirmation
    push = make_roster_push(stream, 'bob@foo.com', 'remove')
    push.query['ver'] = 'two'
    stream.send(push)

    # Now again wait for contact to be removed from the roster
    q.expect('dbus-signal', signal='ContactsChangedWithID',
            args=[{},{},{bob: 'bob@foo.com'}])

    call_async(q, conn.ContactList, 'GetContactListAttributes', [], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertEquals(({
        amy: {
            cs.CONN_IFACE_CONTACT_LIST + '/subscribe': cs.SUBSCRIPTION_STATE_YES,
            cs.CONN_IFACE_CONTACT_LIST + '/publish': cs.SUBSCRIPTION_STATE_YES,
            cs.CONN + '/contact-id': 'amy@foo.com',
            },
        },), r.value)
    # Roster should be at version two by now - commit changes to the server
    contacts.pop()
    contacts.pop()

def test_cache2(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    assert event.stanza.query['ver'] == 'two', event.stanza.query

    stream.send(make_result_iq(stream, event.stanza, False))

    verify(q, conn)

    amy = conn.get_contact_handle_sync(contacts[0][0])
    # Add Amy to some groups
    push = make_roster_push(stream, contacts[0][0], 'both')
    push.query['ver'] = 'two-and-half'
    push.query.item.addElement('group', content='ladies')
    push.query.item.addElement('group', content='people starting with A')
    stream.send(push)

    # but reaction should be the same
    s = q.expect('dbus-signal', signal='GroupsCreated')
    assertEquals(set(('ladies', 'people starting with A')), set(s.args[0]))

    q.expect('stream-iq', iq_type='result', iq_id='push')

    # Also let request subscription from bob again, for simplicity just by
    # doing roster-push - i.e. from some other client
    push = make_roster_push(stream, 'bob@foo.com', 'none', True, 'Rob')
    push.query['ver'] = 'three'
    stream.send(push)
    q.expect('stream-iq', iq_id='push')
    # Commit the change to the server roster
    contacts.append(('bob@foo.com', cs.SUBSCRIPTION_STATE_ASK, cs.SUBSCRIPTION_STATE_NO, ''))

def test_cache3(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    assert event.stanza.query['ver'] == 'three', event.stanza.query

    stream.send(make_result_iq(stream, event.stanza, False))

    verify(q, conn, ('ladies', 'people starting with A'))

    # First let's check the name (Alias) was properly recovered from the cache
    bob = conn.get_contact_handle_sync(contacts[1][0])
    assert conn.Aliasing.RequestAliases([bob]) == ['Rob']

    # Now bob approves our request and server pushes new roster to us
    push = make_roster_push(stream, 'bob@foo.com', 'to', False, 'Rob')
    push.query['ver'] = 'three-and-half'
    stream.send(push)
    contacts.pop()
    contacts.append(('bob@foo.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''))
    q.expect('dbus-signal', signal='ContactsChangedWithID', args=[{bob: contacts[1][1::]}, {bob: contacts[1][0]}, {}])
    q.expect('stream-iq', iq_id='push')

    # and che requests to be added again - and we approve it (which triggers roster push)
    push = make_roster_push(stream, 'che@foo.com', 'from')
    push.query['ver'] = 'four'
    stream.send(push)
    contacts.append(('che@foo.com', cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_YES, ''))
    che = conn.get_contact_handle_sync(contacts[2][0])
    q.expect('dbus-signal', signal='ContactsChangedWithID', args=[{che: contacts[2][1::]}, {che: contacts[2][0]}, {}])
    q.expect('stream-iq', iq_id='push')


def test_cache4(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    assert event.stanza.query['ver'] == 'four', event.stanza.query

    stream.send(make_result_iq(stream, event.stanza, False))

    verify(q, conn, ('ladies', 'people starting with A'))


if __name__ == '__main__':
    XmppAuthenticator.features[ns.NS_XMPP_ROSTERVER]='ver'
    exec_test(test_create) # populate initial roster
    exec_test(test_cache1) # recover initial roster, check, remove some items
    exec_test(test_cache2) # ensure removed is gone, add groups and request
    exec_test(test_cache3) # ensure groups and requested item are there, add moar
    exec_test(test_cache4) # ensure old request is processed and new is added
