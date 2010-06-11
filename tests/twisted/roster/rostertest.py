from gabbletest import (wrap_channel,)
from servicetest import (assertEquals, assertLength, EventPattern,
        assertContains)

import constants as cs

def expect_list_channel(q, bus, conn, name, contacts, lp_contacts=[],
                        rp_contacts=[]):
    return expect_contact_list_channel(q, bus, conn, cs.HT_LIST, name,
        contacts, lp_contacts=lp_contacts, rp_contacts=rp_contacts)

def expect_group_channel(q, bus, conn, name, contacts, lp_contacts=[],
                         rp_contacts=[]):
    return expect_contact_list_channel(q, bus, conn, cs.HT_GROUP, name,
        contacts, lp_contacts=lp_contacts, rp_contacts=rp_contacts)

def expect_contact_list_channel(q, bus, conn, ht, name, contacts,
                                lp_contacts=[], rp_contacts=[]):
    """
    Expects NewChannel and NewChannels signals for the
    contact list with handle type 'ht' and ID 'name', and checks that its
    members, lp members and rp members are exactly 'contacts', 'lp_contacts'
    and 'rp_contacts'.
    Returns a proxy for the channel.
    """

    old_signal, new_signal = q.expect_many(
            EventPattern('dbus-signal', signal='NewChannel'),
            EventPattern('dbus-signal', signal='NewChannels'),
            )

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

