"""Test 1-1 tubes support."""

import dbus

from servicetest import call_async, EventPattern, sync_dbus, assertEquals
from gabbletest import acknowledge_iq, sync_stream
import constants as cs
import ns
import tubetestutil as t

from twisted.words.xish import domish, xpath

def contact_offer_dbus_tube(bytestream, tube_id):
    iq, si = bytestream.create_si_offer(ns.TUBES)

    tube = si.addElement((ns.TUBES, 'tube'))
    tube['type'] = 'dbus'
    tube['service'] = 'com.example.TestCase2'
    tube['id'] = tube_id
    parameters = tube.addElement((None, 'parameters'))
    parameter = parameters.addElement((None, 'parameter'))
    parameter['type'] = 'str'
    parameter['name'] = 'login'
    parameter.addContent('TEST')

    bytestream.stream.send(iq)

def test(q, bus, conn, stream, bytestream_cls, access_control):
    t.check_conn_properties(q, conn)

    conn.Connect()

    _, vcard_event, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns=ns.ROSTER))

    self_handle = conn.GetSelfHandle()

    acknowledge_iq(stream, vcard_event.stanza)

    roster = roster_event.stanza
    roster['type'] = 'result'
    item = roster_event.query.addElement('item')
    item['jid'] = 'bob@localhost' # Bob can do tubes
    item['subscription'] = 'both'
    stream.send(roster)

    bob_full_jid = 'bob@localhost/Bob'
    self_full_jid = 'test@localhost/Resource'

    # Send Bob presence and his tube caps
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = bob_full_jid
    presence['to'] = self_full_jid
    c = presence.addElement('c')
    c['xmlns'] = 'http://jabber.org/protocol/caps'
    c['node'] = 'http://example.com/ICantBelieveItsNotTelepathy'
    c['ver'] = '1.2.3'
    stream.send(presence)

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/disco#info',
        to=bob_full_jid)
    result = event.stanza
    result['type'] = 'result'
    assert event.query['node'] == \
        'http://example.com/ICantBelieveItsNotTelepathy#1.2.3'
    feature = event.query.addElement('feature')
    feature['var'] = ns.TUBES
    stream.send(result)

    # A tube request can be done only if the contact has tube capabilities
    # Ensure that Bob's caps have been received
    sync_stream(q, stream)

    # Also ensure that all the new contact list channels have been announced,
    # so that the NewChannel(s) signals we look for after calling
    # RequestChannel are the ones we wanted.
    sync_dbus(bus, q, conn)

    # let's try to accept a D-Bus tube using the old API
    bytestream = bytestream_cls(stream, q, 'beta', bob_full_jid,
        'test@localhost/Reource', True)

    contact_offer_dbus_tube(bytestream, '69')

    # tubes channel is created
    event = q.expect('dbus-signal', signal='NewChannel')

    bob_handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@localhost'])[0]

    t.check_NewChannel_signal(event.args, cs.CHANNEL_TYPE_TUBES, None,
        bob_handle, False)

    tubes_chan = bus.get_object(conn.bus_name, event.args[0])
    tubes_iface = dbus.Interface(tubes_chan, cs.CHANNEL_TYPE_TUBES)

    event = q.expect('dbus-signal', signal='NewTube')
    id = event.args[0]
    initiator = event.args[1]
    type = event.args[2]
    service = event.args[3]
    parameters = event.args[4]
    state = event.args[5]

    assert id == 69
    initiator_jid = conn.InspectHandles(1, [initiator])[0]
    assert initiator_jid == 'bob@localhost'
    assert type == cs.TUBE_TYPE_DBUS
    assert service == 'com.example.TestCase2'
    assert parameters == {'login': 'TEST'}
    assert state == cs.TUBE_STATE_LOCAL_PENDING

    # accept the tube (old API)
    call_async(q, tubes_iface, 'AcceptDBusTube', id)

    event = q.expect('stream-iq', iq_type='result')
    bytestream.check_si_reply(event.stanza)
    tube = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]' % ns.TUBES,
        event.stanza)
    assert len(tube) == 1

    # Init the bytestream
    events, _ = bytestream.open_bytestream([EventPattern('dbus-return', method='AcceptDBusTube')],
        [EventPattern('dbus-signal', signal='TubeStateChanged', args=[69, 2])])

    return_event = events[0]
    address = return_event.value[0]
    assert len(address) > 0

    # OK, now let's try to accept a D-Bus tube using the new API
    bytestream = bytestream_cls(stream, q, 'gamma', bob_full_jid,
        self_full_jid, True)

    contact_offer_dbus_tube(bytestream, '70')

    e = q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    assert len(channels) == 1
    path, props = channels[0]

    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE
    assert props[cs.INITIATOR_HANDLE] == bob_handle
    assert props[cs.INITIATOR_ID] == 'bob@localhost'
    assert props[cs.INTERFACES] == [cs.CHANNEL_IFACE_TUBE]
    assert props[cs.REQUESTED] == False
    assert props[cs.TARGET_HANDLE] == bob_handle
    assert props[cs.TARGET_ID] == 'bob@localhost'
    assert props[cs.DBUS_TUBE_SERVICE_NAME] == 'com.example.TestCase2'
    assert props[cs.TUBE_PARAMETERS] == {'login': 'TEST'}
    assert props[cs.DBUS_TUBE_SUPPORTED_ACCESS_CONTROLS] == [cs.SOCKET_ACCESS_CONTROL_CREDENTIALS,
        cs.SOCKET_ACCESS_CONTROL_LOCALHOST]
    assert cs.TUBE_STATE not in props

    tube_chan = bus.get_object(conn.bus_name, path)
    tube_chan_iface = dbus.Interface(tube_chan, cs.CHANNEL)
    dbus_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)

    status = tube_chan.Get(cs.CHANNEL_IFACE_TUBE, 'State', dbus_interface=cs.PROPERTIES_IFACE)
    assert status == cs.TUBE_STATE_LOCAL_PENDING

    # try to accept using a wrong access control
    try:
        dbus_tube_iface.Accept(cs.SOCKET_ACCESS_CONTROL_PORT)
    except dbus.DBusException, e:
        assertEquals(e.get_dbus_name(), cs.INVALID_ARGUMENT)
    else:
        assert False

    # accept the tube (new API)
    call_async(q, dbus_tube_iface, 'Accept', access_control)

    # Init the bytestream
    events, state_event = bytestream.open_bytestream(
            [EventPattern('stream-iq', iq_type='result', query_ns=ns.SI),
                EventPattern('dbus-return', method='Accept')],
            [EventPattern('dbus-signal', signal='TubeChannelStateChanged')])

    iq_event = events[0]
    bytestream.check_si_reply(iq_event.stanza)
    tube = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]' % ns.TUBES, iq_event.stanza)
    assert len(tube) == 1

    return_event = events[1]
    addr = return_event.value[0]
    assert len(addr) > 0

    state_event = state_event[0]
    assert state_event.args[0] == cs.TUBE_STATE_OPEN

    # close the tube
    tube_chan_iface.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

if __name__ == '__main__':
    t.exec_dbus_tube_test(test)
