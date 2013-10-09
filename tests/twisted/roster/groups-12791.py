"""
Test broken groups on the roster (regression test for fd.o #12791)
"""

from gabbletest import exec_test
from servicetest import assertLength, assertSameSets, EventPattern
from rostertest import check_contact_roster, contacts_changed_predicate, groups_created_predicate
import constants as cs
import ns

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='women')
    item.addElement('group', content='affected-by-fdo-12791')

    # This is a broken roster - Amy appears twice. This should only happen
    # if the server is somehow buggy. This was my initial attempt at
    # reproducing fd.o #12791 - I doubt it's very realistic, but we shouldn't
    # assert, regardless of what input we get!
    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    item.addElement('group', content='women')

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'
    item.addElement('group', content='men')

    # This is what was *actually* strange about the #12791 submitter's roster -
    # Bob appears, fully subscribed, but also there's an attempt to subscribe
    # to one of Bob's resources. We now ignore such items
    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com/Resource'
    item['subscription'] = 'none'
    item['ask'] = 'subscribe'

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'
    item.addElement('group', content='men')

    stream.send(event.stanza)

    contacts = [
        ('amy@foo.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES, ''),
        ('bob@foo.com', cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_YES, ''),
        ('che@foo.com', cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_NO, ''),
        ]

    q.expect_many(
        EventPattern('dbus-signal', signal='ContactsChanged',
            predicate=lambda e: contacts_changed_predicate(e, conn, contacts)),
        EventPattern('dbus-signal', signal='GroupsCreated',
            predicate=lambda e: groups_created_predicate(e, ['women', 'men', 'affected-by-fdo-12791'])),
        )

    contacts = conn.ContactList.GetContactListAttributes([cs.CONN_IFACE_CONTACT_GROUPS])
    assertLength(3, contacts)

    check_contact_roster(conn, 'amy@foo.com', ['women'])
    check_contact_roster(conn, 'bob@foo.com', ['men'])
    check_contact_roster(conn, 'che@foo.com', ['men'])

    groups = conn.Properties.Get(cs.CONN_IFACE_CONTACT_GROUPS, 'Groups')
    assertSameSets(['men', 'women', 'affected-by-fdo-12791'], groups)

if __name__ == '__main__':
    exec_test(test)
