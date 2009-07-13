
"""
Test workarounds for gtalk
"""

import dbus

from gabbletest import acknowledge_iq, exec_test
from servicetest import EventPattern
import constants as cs
import ns

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish

def make_set_roster_iq(stream, user, contact, state, ask):
    iq = IQ(stream, 'set')
    query = iq.addElement((ns.ROSTER, 'query'))
    item = query.addElement('item')
    item['jid'] = contact
    item['subscription'] = state
    if ask:
        item['ask'] = 'subscribe'
    return iq


def test(q, bus, conn, stream):
    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    # send back empty roster
    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    while True:
        event = q.expect('dbus-signal', signal='NewChannel')
        path, type, handle_type, handle, suppress_handler = event.args

        if type != cs.CHANNEL_TYPE_CONTACT_LIST:
            continue

        chan_name = conn.InspectHandles(handle_type, [handle])[0]

        if chan_name == 'subscribe':
            break

    # request subscription
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan, cs.CHANNEL_IFACE_GROUP)
    assert group_iface.GetMembers() == []
    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    group_iface.AddMembers([handle], '')

    event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()
    assert item["jid"] == 'bob@foo.com'

    acknowledge_iq(stream, event.stanza)

    # send empty roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource',
            item["jid"], "none", False)
    stream.send(iq)

    event = q.expect('stream-presence', presence_type='subscribe')

    # send pending roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', event.to,
            "none", True)
    stream.send(iq)

    # First error point, resetting from none+ask:subscribe to none, and back
    # In the real world this triggers bogus 'subscribe authorization rejected'
    # messages

    # send pending roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', event.to,
            "none", False)
    stream.send(iq)

    # send pending roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', event.to,
            "none", True)
    stream.send(iq)

    # send accepted roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', event.to,
            "to", False)
    stream.send(iq)

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    stream.send(presence)

    # Second error point, demoting from to to none+ask:subscribe, and back
    # In the real world this triggers multiple bogus 'subscribe authorization
    # granted' messages instead of just one

    # send pending roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', event.to,
            "none", True)
    stream.send(iq)

    # send accepted roster item
    iq = make_set_roster_iq(stream, 'test@localhost/Resource', event.to,
            "to", False)
    stream.send(iq)

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=['', [2], [], [], [], 0, 0])
    assert(event.path.endswith('/stored'))

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=['', [], [], [], [2], 0, 0])
    assert(event.path.endswith('/subscribe'))

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=['', [2], [], [], [], 0, 0])
    assert(event.path.endswith('/subscribe'))

    # If there's an assertion here, that means we've got a few MembersChanged
    # signals too many (either from the first, or second point of error).
    q.forbid_events([EventPattern('dbus-signal', signal='MembersChanged')])

if __name__ == '__main__':
    exec_test(test)
