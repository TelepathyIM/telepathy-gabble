from twisted.words.protocols.jabber.client import IQ

from gabbletest import (wrap_channel,)
from servicetest import (assertEquals, assertLength, EventPattern,
        assertContains)

import constants as cs
import ns

def send_roster_push(stream, jid, subscription):
    iq = IQ(stream, "set")
    iq['id'] = 'push'
    query = iq.addElement('query')
    query['xmlns'] = ns.ROSTER
    item = query.addElement('item')
    item['jid'] = jid
    item['subscription'] = subscription
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

def expect_contact_list_signals(q, bus, conn, lists, groups=[]):
    assert lists or groups

    eps = []

    for name in lists:
        eps.extend(get_contact_list_event_patterns(q, bus, conn,
            cs.HT_LIST, name))

    for name in groups:
        eps.extend(get_contact_list_event_patterns(q, bus, conn,
            cs.HT_GROUP, name))

    events = q.expect_many(*eps)
    ret = []

    for name in lists:
        old_signal = events.pop(0)
        new_signal = events.pop(0)
        ret.append((old_signal, new_signal))

    for name in groups:
        old_signal = events.pop(0)
        new_signal = events.pop(0)
        ret.append((old_signal, new_signal))

    assert len(events) == 0
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
        sorted(conn.InspectHandles(cs.HT_CONTACT, members)))

    lp_handles = conn.RequestHandles(cs.HT_CONTACT, lp_contacts)
    rp_handles = conn.RequestHandles(cs.HT_CONTACT, rp_contacts)

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
