from twisted.words.protocols.jabber.client import IQ

from servicetest import (assertEquals, assertSameSets)

import constants as cs
import ns

def make_roster_push(stream, jid, subscription, ask_subscribe=False, name=None):
    iq = IQ(stream, "set")
    iq['id'] = 'push'
    query = iq.addElement('query')
    query['xmlns'] = ns.ROSTER
    item = query.addElement('item')
    item['jid'] = jid
    item['subscription'] = subscription

    if name is not None:
        item['name'] = name

    if ask_subscribe:
        item['ask'] = 'subscribe'

    return iq

def send_roster_push(stream, jid, subscription, ask_subscribe=False, name=None):
    iq = make_roster_push(stream, jid, subscription,
        ask_subscribe=ask_subscribe, name=name)
    stream.send(iq)

# check that @contact on @conn is in groups @groups with @subscribe and
# @publish as subscription states
def check_contact_roster(conn, contact, groups=None, subscribe=None, publish=None):
    h = conn.get_contact_handle_sync(contact)
    attrs = conn.Contacts.GetContactAttributes([h],
            [cs.CONN_IFACE_CONTACT_LIST, cs.CONN_IFACE_CONTACT_GROUPS])[h]

    if groups is not None:
        assertSameSets(groups, attrs[cs.ATTR_GROUPS])
    if subscribe is not None:
        assertEquals(subscribe, attrs[cs.ATTR_SUBSCRIBE])
    if publish is not None:
        assertEquals(publish, attrs[cs.ATTR_PUBLISH])

# function to pass as 'ContactsChanged' dbus-signal even predicate
# checking if the (contact-id, subscribe-state, publish-state, message) tuples
# from @contacts are the arguments of the signal.
def contacts_changed_predicate(e, conn, contacts):
    changes, ids, removals = e.args

    if len(changes) != len(contacts):
        return False

    for c in contacts:
        i, subscribe, publish, msg = c

        h = conn.get_contact_handle_sync(i)

        if changes[h] != (subscribe, publish, msg):
            return False

    return True

# function to pass as a 'BlockedContactsChanged' dbus-signal even predicate
# checking if the @blocked and @unblocked contacts match those from the
# signal
def blocked_contacts_changed_predicate(e, blocked, unblocked):
    b, u = e.args

    return set(b.values()) == set(blocked) and set(u.values()) == set(unblocked)

# function to pass as a 'GroupsCreated' dbus-signal even predicate
# checking if the created @groups match those from the signal
def groups_created_predicate(e, groups):
    return set(e.args[0]) == set(groups)

# function to pass as a 'GroupsChanged' dbus-signal even predicate
# checking if @contacts have been added/removed to/from the @added/@removed
# groups
def groups_changed_predicate(e, conn, contacts, added, removed):
    c, a, r = e.args
    handles = conn.get_contact_handles_sync(contacts)

    return set(handles) == set(c) and set(added) == set(a) and set(removed) == set(r)
