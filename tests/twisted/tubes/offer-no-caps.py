"""
Test that offering a tube to a contact without tube capabilities fails
appropriately.
"""

import dbus

from twisted.words.xish import domish

from servicetest import EventPattern, make_channel_proxy, call_async
from gabbletest import exec_test, acknowledge_iq, sync_stream

import constants as cs
import tubetestutil as t
import ns

def props(ct, extra=None):
    ret = { cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_ID: 'joe@localhost',
            cs.CHANNEL_TYPE: ct,
          }
    if extra is not None:
        ret.update(extra)
    return ret

def test(q, bus, conn, stream):
    conn.Connect()

    _, vcard_event, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns=ns.ROSTER))

    acknowledge_iq(stream, vcard_event.stanza)

    # Send a roster with one member, Joe.
    roster = roster_event.stanza
    roster['type'] = 'result'
    item = roster_event.query.addElement('item')
    item['jid'] = 'joe@localhost'
    item['subscription'] = 'both'
    stream.send(roster)

    # Send Joe's presence.
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'joe@localhost/Joe'
    presence['to'] = 'test@localhost/Resource'
    c = presence.addElement('c')
    c['xmlns'] = 'http://jabber.org/protocol/caps'
    c['node'] = 'http://example.com/IDontSupportTubes'
    c['ver'] = '1.0'
    stream.send(presence)

    # Gabble discoes Joe, because it doesn't know his client's caps
    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/disco#info',
        to='joe@localhost/Joe')
    assert event.query['node'] == 'http://example.com/IDontSupportTubes#1.0'

    # Send a "Joe doesn't have any caps" response.
    result = event.stanza
    result['type'] = 'result'
    stream.send(result)

    # Ensure Joe's caps have been received.
    # FIXME: we shouldn't need to do this, the tubes code should wait for the
    #        caps to appear, just like the StreamedMedia channel does.
    sync_stream(q, stream)

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    # First, we make an old-style Tubes channel, which should work; calling
    # OfferStreamTube or OfferDBusTube on it, however, should fail with
    # NotAvailable
    path, _ = requests.CreateChannel(props(cs.CHANNEL_TYPE_TUBES))
    tubes = make_channel_proxy(conn, path, 'Channel.Type.Tubes')

    call_async(q, tubes, 'OfferDBusTube', 'com.example.monkeys', {})
    e = q.expect('dbus-error', method='OfferDBusTube').error
    assert e.get_dbus_name() == cs.NOT_AVAILABLE, e.get_dbus_name()

    address = t.create_server(q, cs.SOCKET_ADDRESS_TYPE_IPV4)

    call_async(q, tubes, 'OfferStreamTube', 'echo', {},
        cs.SOCKET_ADDRESS_TYPE_IPV4, address,
        cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "")
    e = q.expect('dbus-error', method='OfferStreamTube').error
    assert e.get_dbus_name() == cs.NOT_AVAILABLE, e.get_dbus_name()

    # Now we try making new-style DBusTube and StreamTube channels, and calling
    # the relevant Offer method on them; this should fail with NotAvailable.
    # FIXME: I think this should be NotCapable
    st_path, _ = requests.CreateChannel(props(cs.CHANNEL_TYPE_STREAM_TUBE,
        {cs.STREAM_TUBE_SERVICE: "newecho"}))
    st_chan = bus.get_object(conn.bus_name, st_path)
    st = dbus.Interface(st_chan, cs.CHANNEL_TYPE_STREAM_TUBE)
    call_async(q, st, 'Offer', cs.SOCKET_ADDRESS_TYPE_IPV4,
        address, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, {})
    e = q.expect('dbus-error', method='Offer').error
    assert e.get_dbus_name() == cs.NOT_AVAILABLE, e.get_dbus_name()

    dt_path, _ = requests.CreateChannel(props(cs.CHANNEL_TYPE_DBUS_TUBE,
        { cs.DBUS_TUBE_SERVICE_NAME: "com.newecho" }))
    dt_chan = bus.get_object(conn.bus_name, dt_path)
    dt = dbus.Interface(dt_chan, cs.CHANNEL_TYPE_DBUS_TUBE)
    call_async(q, dt, 'Offer', {}, cs.SOCKET_ACCESS_CONTROL_CREDENTIALS)
    e = q.expect('dbus-error', method='Offer').error
    assert e.get_dbus_name() == cs.NOT_AVAILABLE, e.get_dbus_name()

if __name__ == '__main__':
    exec_test(test)
