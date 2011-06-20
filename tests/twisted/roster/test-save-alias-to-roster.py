
"""
Test that updating an alias saves it to the roster.
"""

import dbus

from servicetest import EventPattern, call_async
from gabbletest import acknowledge_iq, exec_test, make_result_iq
import constants as cs
import ns
from rostertest import expect_contact_list_signals

def test(q, bus, conn, stream):
    event, event2 = q.expect_many(
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns=ns.ROSTER))

    acknowledge_iq(stream, event.stanza)
    acknowledge_iq(stream, event2.stanza)

    signals = expect_contact_list_signals(q, bus, conn, lists=['subscribe'])
    old_signal, new_signal = signals[0]
    path = old_signal.args[0]

    # request subscription
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan, cs.CHANNEL_IFACE_GROUP)
    assert group_iface.GetMembers() == []
    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, group_iface, 'AddMembers', [handle], '')

    event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()

    acknowledge_iq(stream, event.stanza)
    q.expect('dbus-return', method='AddMembers')

    call_async(q, conn.Aliasing, 'RequestAliases', [handle])

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub',
        to='bob@foo.com')

    result = make_result_iq(stream, event.stanza)
    pubsub = result.firstChildElement()
    items = pubsub.addElement('items')
    items['node'] = 'http://jabber.org/protocol/nick'
    item = items.addElement('item')
    item.addElement('nick', 'http://jabber.org/protocol/nick',
        content='Bobby')
    stream.send(result)

    event, _ = q.expect_many(
        EventPattern('stream-iq', iq_type='set', query_ns=ns.ROSTER),
        EventPattern('dbus-return', method='RequestAliases',
        value=(['Bobby'],)))

    item = event.query.firstChildElement()
    assert item['jid'] == 'bob@foo.com'
    assert item['name'] == 'Bobby'

if __name__ == '__main__':
    exec_test(test)
