"""Test 1-1 tubes support."""

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, watch_tube_signals, sync_dbus
from gabbletest import acknowledge_iq, sync_stream
import constants as cs
import ns
import tubetestutil as t
from bytestream import parse_si_offer, create_si_reply, parse_si_reply

from dbus import PROPERTIES_IFACE

from twisted.words.xish import domish, xpath

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

new_sample_parameters = dbus.Dictionary({
    's': 'newhello',
    'ay': dbus.ByteArray('newhello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

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

def test(q, bus, conn, stream, bytestream_cls):
    t.set_up_echo("")
    t.set_up_echo("2")

    t.check_conn_properties(q, conn)

    conn.Connect()

    _, vcard_event, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns='jabber:iq:roster'))

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

    # Test tubes with Bob. Bob have tube capabilities.
    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    chan_path = conn.RequestChannel(cs.CHANNEL_TYPE_TUBES, cs.HT_CONTACT,
        bob_handle, True)

    tubes_chan = bus.get_object(conn.bus_name, chan_path)
    tubes_iface = dbus.Interface(tubes_chan, cs.CHANNEL_TYPE_TUBES)

    call_async(q, tubes_iface, 'OfferDBusTube',
        'com.example.TestCase', sample_parameters)

    event = q.expect('stream-iq', iq_type='set', to=bob_full_jid)
    profile, dbus_stream_id, bytestreams = parse_si_offer(event.stanza)

    assert profile == ns.TUBES
    assert bytestreams == [ns.BYTESTREAMS, ns.IBB]

    tube = xpath.queryForNodes('/iq/si/tube', event.stanza)[0]
    assert tube['initiator'] == 'test@localhost'
    assert tube['service'] == 'com.example.TestCase'
    assert tube['stream-id'] == dbus_stream_id
    assert not tube.hasAttribute('dbus-name')
    assert tube['type'] == 'dbus'
    dbus_tube_id = long(tube['id'])

    params = {}
    parameter_nodes = xpath.queryForNodes('/tube/parameters/parameter', tube)
    for node in parameter_nodes:
        assert node['name'] not in params
        params[node['name']] = (node['type'], str(node))
    assert params == {'ay': ('bytes', 'aGVsbG8='),
                      's': ('str', 'hello'),
                      'i': ('int', '-123'),
                      'u': ('uint', '123'),
                     }

    bytestream3 = bytestream_cls(stream, q, dbus_stream_id, self_full_jid,
        bob_full_jid, False)
    result, si = create_si_reply(stream, event.stanza, bytestream3.initiator, bytestream3.get_ns())
    stream.send(result)

    bytestream3.wait_bytestream_open()

    q.expect('dbus-signal', signal='TubeStateChanged',
        args=[dbus_tube_id, cs.TUBE_STATE_OPEN])

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    expected_dtube = (dbus_tube_id, self_handle, cs.TUBE_TYPE_DBUS,
        'com.example.TestCase', sample_parameters, cs.TUBE_STATE_OPEN)
    t.check_tube_in_tubes(expected_dtube, tubes)

    dbus_tube_adr = tubes_iface.GetDBusTubeAddress(dbus_tube_id)
    dbus_tube_conn = Connection(dbus_tube_adr)

    signal = SignalMessage('/', 'foo.bar', 'baz')
    my_bus_name = ':123.whatever.you.like'
    signal.set_sender(my_bus_name)
    signal.append(42, signature='u')
    dbus_tube_conn.send_message(signal)

    binary = bytestream3.get_data()

    # little and big endian versions of: SIGNAL, NO_REPLY, protocol v1,
    # 4-byte payload
    assert binary.startswith('l\x04\x01\x01' '\x04\x00\x00\x00') or \
           binary.startswith('B\x04\x01\x01' '\x00\x00\x00\x04')
    # little and big endian versions of the 4-byte payload, UInt32(42)
    assert (binary[0] == 'l' and binary.endswith('\x2a\x00\x00\x00')) or \
           (binary[0] == 'B' and binary.endswith('\x00\x00\x00\x2a'))
    # XXX: verify that it's actually in the "sender" slot, rather than just
    # being in the message somewhere
    assert my_bus_name in binary

    watch_tube_signals(q, dbus_tube_conn)

    dbus_message = binary
    seq = 0

    # Have the fake client send us a message all in one go...
    bytestream3.send_data(dbus_message)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)

    # ... and a message one byte at a time ...
    for byte in dbus_message:
        bytestream3.send_data(byte)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)

    # ... and two messages in one go
    bytestream3.send_data(dbus_message + dbus_message)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)

    # OK, now let's try to accept a D-Bus tube using the old API
    bytestream4 = bytestream_cls(stream, q, 'beta', bob_full_jid,
        'test@localhost/Reource', True)

    contact_offer_dbus_tube(bytestream4, '69')

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
    bytestream = parse_si_reply (event.stanza)
    assert bytestream == bytestream4.get_ns()
    tube = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]' % ns.TUBES,
        event.stanza)
    assert len(tube) == 1

    # Init the bytestream
    event = bytestream4.open_bytestream(EventPattern('dbus-return', method='AcceptDBusTube'))

    address = event.value[0]
    assert len(address) > 0

    event = q.expect('dbus-signal', signal='TubeStateChanged',
        args=[69, 2]) # 2 == OPEN
    id = event.args[0]
    state = event.args[1]

    # OK, now let's try to accept a D-Bus tube using the new API
    bytestream5 = bytestream_cls(stream, q, 'gamma', bob_full_jid,
        self_full_jid, True)

    contact_offer_dbus_tube(bytestream5, '70')

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
    assert cs.TUBE_STATE not in props

    tube_chan = bus.get_object(conn.bus_name, path)
    tube_chan_iface = dbus.Interface(tube_chan, cs.CHANNEL)
    dbus_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)

    status = tube_chan.Get(cs.CHANNEL_IFACE_TUBE, 'State', dbus_interface=PROPERTIES_IFACE)
    assert status == cs.TUBE_STATE_LOCAL_PENDING

    # accept the tube (new API)
    call_async(q, dbus_tube_iface, 'AcceptDBusTube')

    event = q.expect('stream-iq', iq_type='result')
    bytestream = parse_si_reply (event.stanza)
    assert bytestream == bytestream5.get_ns()
    tube = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]' % ns.TUBES, event.stanza)
    assert len(tube) == 1

    # Init the bytestream
    return_event = bytestream5.open_bytestream(EventPattern('dbus-return', method='AcceptDBusTube'))

    _, state_event = q.expect_many(
        EventPattern('stream-iq', iq_type='result'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged'))

    addr = return_event.value[0]
    assert len(addr) > 0

    assert state_event.args[0] == cs.TUBE_STATE_OPEN

    # close the tube
    tube_chan_iface.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    # OK, we're done
    conn.Disconnect()

    q.expect('tube-signal', signal='Disconnected')
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    t.exec_tube_test(test)
