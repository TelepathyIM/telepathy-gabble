"""
Test basic roster functionality.
"""

import dbus

from twisted.words.protocols.jabber.client import IQ
from gabbletest import exec_test, acknowledge_iq
from servicetest import EventPattern, tp_name_prefix

def _expect_contact_list_channel(q, bus, conn, name, contacts):
    old_signal, new_signal = q.expect_many(
            EventPattern('dbus-signal', signal='NewChannel'),
            EventPattern('dbus-signal', signal='NewChannels'),
            )

    path, type, handle_type, handle, suppress_handler = old_signal.args

    assert type == u'org.freedesktop.Telepathy.Channel.Type.ContactList'
    assert conn.InspectHandles(handle_type, [handle])[0] == name
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Group')
    members = group_iface.GetMembers()
    assert conn.InspectHandles(1, members) == contacts

    return chan, group_iface

def test(q, bus, conn, stream):
    conn.Connect()

    def send_roster_iq(stream, jid, subscription):
        iq = IQ(stream, "set")
        iq['id'] = 'push'
        query = iq.addElement('query')
        query['xmlns'] = 'jabber:iq:roster'
        item = query.addElement('item')
        item['jid'] = jid
        item['subscription'] = subscription
        stream.send(iq)

    event = q.expect('stream-iq', query_ns='jabber:iq:roster')
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'quux@foo.com'
    item['subscription'] = 'none'

    stream.send(event.stanza)

    k0, i0 = _expect_contact_list_channel(q, bus, conn, 'publish',
        [])
    k1, i1 = _expect_contact_list_channel(q, bus, conn, 'subscribe',
        [])
    k, i = _expect_contact_list_channel(q, bus, conn, 'known',
        ['quux@foo.com'])

    i.RemoveMembers([dbus.UInt32(2)], '')
    send_roster_iq(stream, 'quux@foo.com', 'remove')

    acknowledge_iq(stream, q.expect('stream-iq').stanza)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

