"""Test 1-1 tubes support."""

import dbus

from servicetest import call_async, EventPattern, sync_dbus, assertEquals
from gabbletest import acknowledge_iq, sync_stream
import constants as cs
import ns
import tubetestutil as t

from twisted.words.xish import domish, xpath

last_tube_id = 69

def contact_offer_dbus_tube(bytestream, tube_id):
    iq, si = bytestream.create_si_offer(ns.TUBES)

    tube = si.addElement((ns.TUBES, 'tube'))
    tube['type'] = 'dbus'
    tube['service'] = 'com.example.TestCase2'
    tube['id'] = str(tube_id)
    parameters = tube.addElement((None, 'parameters'))
    parameter = parameters.addElement((None, 'parameter'))
    parameter['type'] = 'str'
    parameter['name'] = 'login'
    parameter.addContent('TEST')

    bytestream.stream.send(iq)

def test(q, bus, conn, stream, bytestream_cls, access_control):
    global last_tube_id

    t.check_conn_properties(q, conn)

    vcard_event, roster_event = q.expect_many(
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns=ns.ROSTER))

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

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

    bob_handle = conn.get_contact_handle_sync('bob@localhost')

    # let's try to accept a D-Bus tube using the new API
    bytestream = bytestream_cls(stream, q, 'gamma', bob_full_jid,
        self_full_jid, True)

    last_tube_id += 1
    contact_offer_dbus_tube(bytestream, last_tube_id)

    def new_chan_predicate(e):
        path, props = e.args[0][0]
        return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE

    e = q.expect('dbus-signal', signal='NewChannels',
                 predicate=new_chan_predicate)
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
    except dbus.DBusException as e:
        assertEquals(e.get_dbus_name(), cs.INVALID_ARGUMENT)
    else:
        assert False

    # accept the tube (new API)
    call_async(q, dbus_tube_iface, 'Accept', access_control)

    events = q.expect_many (EventPattern('stream-iq', iq_type='result',
        query_ns=ns.SI), EventPattern('dbus-return', method='Accept'))

    iq_event = events[0]
    bytestream.check_si_reply(iq_event.stanza)
    tube = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]' % ns.TUBES, iq_event.stanza)
    assert len(tube) == 1

    return_event = events[1]
    addr = return_event.value[0]
    assert len(addr) > 0

    # Open the bytestream
    _, state_event = bytestream.open_bytestream([],
            [EventPattern('dbus-signal',
                signal='TubeChannelStateChanged',
                path=path)])

    state_event = state_event[0]
    assert state_event.args[0] == cs.TUBE_STATE_OPEN

    # close the tube
    tube_chan_iface.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

if __name__ == '__main__':
    t.exec_dbus_tube_test(test)
