"""
Regression test for a bug where calling ListChannels with an open old-skool
DBus tube asserted.
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, sync_stream

import ns
import constants

from twisted.words.xish import domish

jid = 'explosions@in.the.sky'

def test(q, bus, conn, stream):
    conn.Connect()
    _, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', query_ns='jabber:iq:roster'))

    roster = roster_event.stanza
    roster['type'] = 'result'
    item = roster_event.query.addElement('item')
    item['jid'] = jid
    item['subscription'] = 'both'
    stream.send(roster)

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = '%s/Bob' % jid
    presence['to'] = 'test@localhost/Resource'
    c = presence.addElement('c')
    c['xmlns'] = ns.CAPS
    c['node'] = 'http://example.com/ICantBelieveItsNotTelepathy'
    c['ver'] = '1.2.3'
    stream.send(presence)

    event = q.expect('stream-iq', iq_type='get',
        query_ns=ns.DISCO_INFO,
        to=('%s/Bob' % jid))
    result = event.stanza
    result['type'] = 'result'
    assert event.query['node'] == \
        'http://example.com/ICantBelieveItsNotTelepathy#1.2.3'
    feature = event.query.addElement('feature')
    feature['var'] = ns.TUBES
    stream.send(result)

    sync_stream(q, stream)

    h = conn.RequestHandles(1, [jid])[0]
    tubes_path = conn.RequestChannel(constants.CHANNEL_TYPE_TUBES, 1, h, True)

    tubes_chan = bus.get_object(conn.bus_name, tubes_path)
    tubes_iface = dbus.Interface(tubes_chan, constants.CHANNEL_TYPE_TUBES)

    tubes_iface.OfferDBusTube('bong.hits', dbus.Dictionary({}, signature='sv'))

    conn.ListChannels()

    call_async(q, conn, 'Disconnect')
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
