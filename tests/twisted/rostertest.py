from twisted.words.protocols.jabber.client import IQ

from gabbletest import (wrap_channel,)
from servicetest import (assertEquals, assertLength, EventPattern,
        assertContains, assertSameSets)

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

def get_contact_list_event_patterns(q, bus, conn, expected_handle_type, name):
    expected_handle = conn.RequestHandles(expected_handle_type, [name])[0]

    def new_channel_predicate(e):
        path, type, handle_type, handle, suppress_handler = e.args
        if type != cs.CHANNEL_TYPE_CONTACT_LIST:
            return False
        if handle_type != expected_handle_type:
            return False
        if handle != expected_handle:
            return False
        return True
    new_channel_repr = ('NewChannel(., ContactList, %u, "%s", .)'
            % (expected_handle_type, name))
    new_channel_predicate.__repr__ = lambda: new_channel_repr

    def new_channels_predicate(e):
        info, = e.args
        if len(info) != 1:
            return False
        path, props = info[0]
        if props.get(cs.CHANNEL_TYPE) != cs.CHANNEL_TYPE_CONTACT_LIST:
            return False
        if props.get(cs.TARGET_HANDLE_TYPE) != expected_handle_type:
            return False
        if props.get(cs.TARGET_HANDLE) != expected_handle:
            return False
        return True
    new_channels_repr = ('NewChannels(... ct=ContactList, ht=%u, name="%s"... )'
            % (expected_handle_type, name))
    new_channels_predicate.__repr__ = lambda: new_channels_repr

    return (
            EventPattern('dbus-signal', signal='NewChannel',
                predicate=new_channel_predicate),
            EventPattern('dbus-signal', signal='NewChannels',
                predicate=new_channels_predicate)
            )

def expect_contact_list_signals(q, bus, conn, lists, groups=[],
        expect_more=None):
    assert lists or groups

    if expect_more is None:
        eps = []
    else:
        eps = expect_more[:]

    for name in lists:
        eps.extend(get_contact_list_event_patterns(q, bus, conn,
            cs.HT_LIST, name))

    for name in groups:
        eps.extend(get_contact_list_event_patterns(q, bus, conn,
            cs.HT_GROUP, name))

    events = q.expect_many(*eps)
    ret = []
    more = []

    if expect_more is not None:
        for ep in expect_more:
            more.append(events.pop(0))

    for name in lists:
        old_signal = events.pop(0)
        new_signal = events.pop(0)
        ret.append((old_signal, new_signal))

    for name in groups:
        old_signal = events.pop(0)
        new_signal = events.pop(0)
        ret.append((old_signal, new_signal))

    assert len(events) == 0

    if expect_more is not None:
        return ret, more

    return ret

def check_contact_list_signals(q, bus, conn, signals,
        ht, name, contacts, lp_contacts=[], rp_contacts=[]):
    """
    Looks at NewChannel and NewChannels signals for the contact list with ID
    'name' and checks that its members, lp members and rp members are exactly
    'contacts', 'lp_contacts' and 'rp_contacts'.
    Returns a proxy for the channel.
    """
    old_signal, new_signal = signals

    path, type, handle_type, handle, suppress_handler = old_signal.args

    assertEquals(cs.CHANNEL_TYPE_CONTACT_LIST, type)
    assertEquals(name, conn.InspectHandles(handle_type, [handle])[0])

    chan = wrap_channel(bus.get_object(conn.bus_name, path),
        cs.CHANNEL_TYPE_CONTACT_LIST)
    members = chan.Group.GetMembers()

    assertEquals(sorted(contacts),
        sorted(conn.inspect_contacts_sync(members)))

    lp_handles = conn.get_contact_handles_sync(lp_contacts)
    rp_handles = conn.get_contact_handles_sync(rp_contacts)

    # NB. comma: we're unpacking args. Thython!
    info, = new_signal.args
    assertLength(1, info) # one channel
    path_, emitted_props = info[0]

    assertEquals(path_, path)

    assertEquals(cs.CHANNEL_TYPE_CONTACT_LIST, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(ht, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(handle, emitted_props[cs.TARGET_HANDLE])

    channel_props = chan.Properties.GetAll(cs.CHANNEL)
    assertEquals(handle, channel_props.get('TargetHandle'))
    assertEquals(ht, channel_props.get('TargetHandleType'))
    assertEquals(cs.CHANNEL_TYPE_CONTACT_LIST, channel_props.get('ChannelType'))
    assertContains(cs.CHANNEL_IFACE_GROUP, channel_props.get('Interfaces'))
    assertEquals(name, channel_props['TargetID'])
    assertEquals(False, channel_props['Requested'])
    assertEquals('', channel_props['InitiatorID'])
    assertEquals(0, channel_props['InitiatorHandle'])

    group_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_GROUP)
    assertContains('HandleOwners', group_props)
    assertContains('Members', group_props)
    assertEquals(members, group_props['Members'])
    assertContains('LocalPendingMembers', group_props)
    actual_lp_handles = [x[0] for x in group_props['LocalPendingMembers']]
    assertEquals(sorted(lp_handles), sorted(actual_lp_handles))
    assertContains('RemotePendingMembers', group_props)
    assertEquals(sorted(rp_handles), sorted(group_props['RemotePendingMembers']))
    assertContains('GroupFlags', group_props)

    return chan

# check that @contact on @conn is in groups @groups with @subscribe and
# @publish as subscription states
def check_contact_roster(conn, contact, groups=None, subscribe=None, publish=None):
    h = conn.get_contact_handle_sync(contact)
    attrs = conn.Contacts.GetContactAttributes([h],
            [cs.CONN_IFACE_CONTACT_LIST, cs.CONN_IFACE_CONTACT_GROUPS], True)[h]

    if groups is not None:
        assertSameSets(groups, attrs[cs.ATTR_GROUPS])
    if subscribe is not None:
        assertEquals(subscribe, attrs[cs.ATTR_SUBSCRIBE])
    if publish is not None:
        assertEquals(publish, attrs[cs.ATTR_PUBLISH])

# function to pass as 'ContactsChangedWithID' dbus-signal even predicate
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
