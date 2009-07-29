"""Test D-Bus private tube support"""

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, unwrap, watch_tube_signals,\
    assertContains
from gabbletest import sync_stream, make_presence
import constants as cs
import tubetestutil as t

from twisted.words.xish import xpath
import ns
from bytestream import create_from_si_offer, announce_socks5_proxy
from caps_helper import make_caps_disco_reply

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

def alice_accepts_tube(q, stream, iq_event, dbus_tube_id, bytestream_cls):
    iq = iq_event.stanza

    bytestream, profile = create_from_si_offer(stream, q, bytestream_cls, iq,
        'test@localhost/Resource')

    assert profile == ns.TUBES

    tube_nodes = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]'
        % ns.TUBES, iq)
    assert len(tube_nodes) == 1
    tube = tube_nodes[0]
    tube['type'] = 'dbus'
    assert tube['initiator'] == 'test@localhost'
    assert tube['service'] == 'com.example.TestCase'
    assert tube['id'] == str(dbus_tube_id)

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

    # Alice accepts the tube
    result, si = bytestream.create_si_reply(iq)
    si.addElement((ns.TUBES, 'tube'))
    stream.send(result)

    bytestream.wait_bytestream_open()

    q.expect('dbus-signal', signal='TubeStateChanged',
        args=[dbus_tube_id, cs.TUBE_STATE_OPEN])

    return bytestream

def send_dbus_message_to_alice(q, stream, dbus_tube_adr, bytestream):
    tube = Connection(dbus_tube_adr)
    signal = SignalMessage('/', 'foo.bar', 'baz')
    signal.append(42, signature='u')
    tube.send_message(signal)

    binary = bytestream.get_data()

    # little and big endian versions of: SIGNAL, NO_REPLY, protocol v1,
    # 4-byte payload
    assert binary.startswith('l\x04\x01\x01' '\x04\x00\x00\x00') or \
           binary.startswith('B\x04\x01\x01' '\x00\x00\x00\x04')
    # little and big endian versions of the 4-byte payload, UInt32(42)
    assert (binary[0] == 'l' and binary.endswith('\x2a\x00\x00\x00')) or \
           (binary[0] == 'B' and binary.endswith('\x00\x00\x00\x2a'))
    # XXX: verify that it's actually in the "sender" slot, rather than just
    # being in the message somewhere

    watch_tube_signals(q, tube)

    dbus_message = binary

    # Have the fake client send us a message all in one go...
    bytestream.send_data(dbus_message)
    q.expect('tube-signal', signal='baz', args=[42], tube=tube)

    # ... and a message one byte at a time ...
    for byte in dbus_message:
        bytestream.send_data(byte)
    q.expect('tube-signal', signal='baz', args=[42], tube=tube)

    # ... and two messages in one go
    bytestream.send_data(dbus_message + dbus_message)
    q.expect('tube-signal', signal='baz', args=[42], tube=tube)
    q.expect('tube-signal', signal='baz', args=[42], tube=tube)

def offer_old_dbus_tube(q, bus, conn, stream, self_handle, alice_handle, bytestream_cls):
    # request tubes channel (old API)
    tubes_path = conn.RequestChannel(cs.CHANNEL_TYPE_TUBES, cs.HT_CONTACT,
            alice_handle, True)
    tubes_chan = bus.get_object(conn.bus_name, tubes_path)
    tubes_iface = dbus.Interface(tubes_chan, cs.CHANNEL_TYPE_TUBES)
    tubes_chan_iface = dbus.Interface(tubes_chan, cs.CHANNEL)

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = tubes_chan.GetAll(cs.CHANNEL, dbus_interface=cs.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == alice_handle,\
            (channel_props.get('TargetHandle'), alice_handle)
    assert channel_props.get('TargetHandleType') == cs.HT_CONTACT,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == cs.CHANNEL_TYPE_TUBES,\
            channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props, channel_props
    assert channel_props['Interfaces'] == [], channel_props['Interfaces']
    assert channel_props['TargetID'] == 'alice@localhost', channel_props['TargetID']
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    # Offer a D-Bus tube using old API
    call_async(q, tubes_iface, 'OfferDBusTube',
            'com.example.TestCase', sample_parameters)

    new_tube_event, iq_event, offer_return_event = \
        q.expect_many(
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('stream-iq', to='alice@localhost/Test'),
        EventPattern('dbus-return', method='OfferDBusTube'))

    # handle new_tube_event
    dbus_tube_id = new_tube_event.args[0]
    assert new_tube_event.args[1] == self_handle
    assert new_tube_event.args[2] == cs.TUBE_TYPE_DBUS
    assert new_tube_event.args[3] == 'com.example.TestCase'
    assert new_tube_event.args[4] == sample_parameters
    assert new_tube_event.args[5] == cs.TUBE_STATE_REMOTE_PENDING

    # handle offer_return_event
    assert offer_return_event.value[0] == dbus_tube_id

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 1
    expected_tube = (dbus_tube_id, self_handle, cs.TUBE_TYPE_DBUS,
        'com.example.TestCase', sample_parameters, cs.TUBE_STATE_REMOTE_PENDING)
    t.check_tube_in_tubes(expected_tube, tubes)

    bytestream = alice_accepts_tube(q, stream, iq_event, dbus_tube_id, bytestream_cls)

    dbus_tube_adr = tubes_iface.GetDBusTubeAddress(dbus_tube_id)
    send_dbus_message_to_alice(q, stream, dbus_tube_adr, bytestream)

    # close the tube
    tubes_iface.CloseTube(dbus_tube_id)
    q.expect('dbus-signal', signal='TubeClosed', args=[dbus_tube_id])

    # and close the tubes channel
    tubes_chan_iface.Close()
    q.expect('dbus-signal', signal='Closed')


def offer_new_dbus_tube(q, bus, conn, stream, self_handle, alice_handle,
    bytestream_cls, access_control):

    # Offer a tube to Alice (new API)

    call_async(q, conn.Requests, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_ID: 'alice@localhost',
             cs.DBUS_TUBE_SERVICE_NAME: 'com.example.TestCase'
            }, byte_arrays=True)
    cc_ret, nc = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    tube_path, tube_props = cc_ret.value
    new_channel_details = nc.args[0]

    # check tube channel properties
    assert tube_props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE
    assert tube_props[cs.INTERFACES] == [cs.CHANNEL_IFACE_TUBE]
    assert tube_props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
    assert tube_props[cs.TARGET_HANDLE] == alice_handle
    assert tube_props[cs.TARGET_ID] == 'alice@localhost'
    assert tube_props[cs.REQUESTED] == True
    assert tube_props[cs.INITIATOR_HANDLE] == self_handle
    assert tube_props[cs.INITIATOR_ID] == "test@localhost"
    assert tube_props[cs.DBUS_TUBE_SERVICE_NAME] == 'com.example.TestCase'
    assert tube_props[cs.DBUS_TUBE_SUPPORTED_ACCESS_CONTROLS] == [cs.SOCKET_ACCESS_CONTROL_CREDENTIALS,
        cs.SOCKET_ACCESS_CONTROL_LOCALHOST]
    assert cs.DBUS_TUBE_DBUS_NAMES not in tube_props
    assert cs.TUBE_PARAMETERS not in tube_props
    assert cs.TUBE_STATE not in tube_props

    # get the list of all channels to check that newly announced ones are in it
    all_channels = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE, byte_arrays=True)

    for path, props in new_channel_details:
        assertContains((path, props), all_channels)

    # Under the current implementation, creating a new-style Tube channel
    # ensures that an old-style Tubes channel exists, even though Tube channels
    # aren't visible on the Tubes channel until they're offered.  Another
    # correct implementation would have the Tubes channel spring up only when
    # the Tube is offered.
    #
    # Anyway. Given the current implementation, they should be announced together.
    assert len(new_channel_details) == 2, unwrap(new_channel_details)
    found_tubes = False
    found_tube = False
    for path, details in new_channel_details:
        if details[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TUBES:
            found_tubes = True
            tubes_chan = bus.get_object(conn.bus_name, path)
            tubes_iface = dbus.Interface(tubes_chan, cs.CHANNEL_TYPE_TUBES)
        elif details[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE:
            found_tube = True
            assert tube_path == path, (tube_path, path)
        else:
            assert False, (path, details)
    assert found_tube and found_tubes, unwrap(new_channel_details)

    # The tube's not offered, so it shouldn't be shown on the old interface.
    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 0, tubes

    tube_chan = bus.get_object(conn.bus_name, tube_path)
    tube_chan_iface = dbus.Interface(tube_chan, cs.CHANNEL)
    dbus_tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_DBUS_TUBE)

    # check State and Parameters
    props = tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE, dbus_interface=cs.PROPERTIES_IFACE,
        byte_arrays=True)
    assert props['State'] == cs.TUBE_CHANNEL_STATE_NOT_OFFERED

    # check ServiceName and DBusNames
    props = tube_chan.GetAll(cs.CHANNEL_TYPE_DBUS_TUBE, dbus_interface=cs.PROPERTIES_IFACE)
    assert props['ServiceName'] == 'com.example.TestCase'
    assert props['DBusNames'] == {}

    # Only when we offer the tube should it appear on the Tubes channel and an
    # IQ be sent to Alice. We sync the stream to ensure the IQ would have
    # arrived if it had been sent.
    sync_stream(q, stream)
    call_async(q, dbus_tube_iface, 'Offer', sample_parameters, access_control)
    offer_return_event, iq_event, new_tube_event, state_event = q.expect_many(
        EventPattern('dbus-return', method='Offer'),
        EventPattern('stream-iq', to='alice@localhost/Test'),
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged'),
        )

    tube_address = offer_return_event.value[0]
    assert len(tube_address) > 0

    assert state_event.args[0] == cs.TUBE_CHANNEL_STATE_REMOTE_PENDING

    # Now the tube's been offered, it should be shown on the old interface
    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 1
    expected_tube = (None, self_handle, cs.TUBE_TYPE_DBUS, 'com.example.TestCase',
        sample_parameters, cs.TUBE_STATE_REMOTE_PENDING)
    t.check_tube_in_tubes(expected_tube, tubes)

    status = tube_chan.Get(cs.CHANNEL_IFACE_TUBE, 'State', dbus_interface=cs.PROPERTIES_IFACE)
    assert status == cs.TUBE_STATE_REMOTE_PENDING

    tube_chan_iface.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

def test(q, bus, conn, stream, bytestream_cls, access_control):
    conn.Connect()

    _, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    announce_socks5_proxy(q, stream, disco_event.stanza)

    t.check_conn_properties(q, conn)

    self_handle = conn.GetSelfHandle()
    alice_handle = conn.RequestHandles(cs.HT_CONTACT, ["alice@localhost"])[0]

    # send Alice's presence
    caps =  { 'ext': '', 'ver': '0.0.0',
        'node': 'http://example.com/fake-client0' }
    presence = make_presence('alice@localhost/Test', caps=caps)
    stream.send(presence)

    _, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged',
            args = [{alice_handle: (2L, u'available', u'')}]),
        EventPattern('stream-iq', to='alice@localhost/Test',
            query_ns=ns.DISCO_INFO),
        )

    # reply to disco query
    stream.send(make_caps_disco_reply(stream, disco_event.stanza, [ns.TUBES]))

    sync_stream(q, stream)

    offer_old_dbus_tube(q, bus, conn, stream, self_handle, alice_handle, bytestream_cls)
    offer_new_dbus_tube(q, bus, conn, stream, self_handle, alice_handle, bytestream_cls, access_control)

if __name__ == '__main__':
    t.exec_dbus_tube_test(test)
