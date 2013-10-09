"""
Regression test for http://bugs.freedesktop.org/show_bug.cgi?id=19524
"""

from gabbletest import exec_test, acknowledge_iq
from rostertest import (send_roster_push, check_contact_roster)
from servicetest import (assertEquals, EventPattern, call_async)
import constants as cs
import ns

def test_modern(q, bus, conn, stream):
    test(q, bus, conn, stream)

def test_modern_queued(q, bus, conn, stream):
    test(q, bus, conn, stream, True)

def test(q, bus, conn, stream, queued=False):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'quux@foo.com'
    item['subscription'] = 'none'

    quux_handle = conn.get_contact_handle_sync('quux@foo.com')

    stream.send(event.stanza)

    # slight implementation detail: TpBaseContactList emits ContactsChanged
    # before it announces its channels
    q.expect('dbus-signal', signal='ContactsChanged',
            interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path,
            args=[{quux_handle:
                (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_NO, '')}, {quux_handle: 'quux@foo.com'}, {}])

    check_contact_roster(conn, 'quux@foo.com', [], cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_NO)

    if queued:
        conn.Aliasing.SetAliases({quux_handle: 'Quux'})
        set_aliases = q.expect('stream-iq', query_ns=ns.ROSTER)
        item = set_aliases.query.firstChildElement()
        assertEquals('quux@foo.com', item['jid'])
        assertEquals('Quux', item['name'])

    expectations = [
            EventPattern('stream-iq',
            iq_type='set', query_ns=ns.ROSTER),
            ]

    call_async(q, conn.ContactList, 'RemoveContacts', [quux_handle])

    if queued:
        # finish off the previous thing we were doing, so removal can proceed
        acknowledge_iq(stream, set_aliases.stanza)

    event = q.expect_many(*expectations)[0]
    item = event.query.firstChildElement()
    assertEquals('quux@foo.com', item['jid'])
    assertEquals('remove', item['subscription'])

    send_roster_push(stream, 'quux@foo.com', 'remove')

    q.expect_many(
            EventPattern('dbus-signal', interface=cs.CONN_IFACE_CONTACT_LIST,
                path=conn.object_path, signal='ContactsChanged',
                args=[{}, {}, {quux_handle: 'quux@foo.com'}]),
            EventPattern('stream-iq', iq_id='push', iq_type='result'),
            )

    acknowledge_iq(stream, event.stanza)

    q.expect('dbus-return', method='RemoveContacts')
    # FIXME: when we depend on a new enough tp-glib, RemoveMembers should
    # return at this point too

if __name__ == '__main__':
    exec_test(test_modern)
    exec_test(test_modern_queued)
