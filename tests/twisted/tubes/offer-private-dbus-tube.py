"""Test D-Bus private tube support"""

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, unwrap, watch_tube_signals,\
    assertContains, assertEquals
from gabbletest import sync_stream, make_presence
import constants as cs
import tubetestutil as t

from twisted.words.xish import xpath
import ns
from bytestream import create_from_si_offer, announce_socks5_proxy
from caps_helper import send_disco_reply

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

    q.expect('dbus-signal', signal='TubeChannelStateChanged',
        args=[cs.TUBE_STATE_OPEN])

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

def offer_new_dbus_tube(q, bus, conn, stream, self_handle, alice_handle,
    bytestream_cls, access_control):

    # Offer a tube to Alice (new API)

    def new_chan_predicate(e):
        types = []
        for _, props in e.args[0]:
            types.append(props[cs.CHANNEL_TYPE])

        return cs.CHANNEL_TYPE_DBUS_TUBE in types

    def find_dbus_tube(channels):
        for path, props in channels:
            if props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE:
                return path, props

        return None, None

    call_async(q, conn.Requests, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_ID: 'alice@localhost',
             cs.DBUS_TUBE_SERVICE_NAME: 'com.example.TestCase'
            }, byte_arrays=True)
    cc_ret, nc = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels',
                     predicate=new_chan_predicate),
        )
    tube_path, tube_props = cc_ret.value
    _, new_channel_props = find_dbus_tube(nc.args[0])

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

    for path, props in nc.args[0]:
        assertContains((path, props), all_channels)

    assertEquals(tube_props, new_channel_props)

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
    offer_return_event, iq_event, state_event = q.expect_many(
        EventPattern('dbus-return', method='Offer'),
        EventPattern('stream-iq', to='alice@localhost/Test'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged'),
        )

    tube_address = offer_return_event.value[0]
    assert len(tube_address) > 0

    assert state_event.args[0] == cs.TUBE_CHANNEL_STATE_REMOTE_PENDING

    status = tube_chan.Get(cs.CHANNEL_IFACE_TUBE, 'State', dbus_interface=cs.PROPERTIES_IFACE)
    assert status == cs.TUBE_STATE_REMOTE_PENDING

    tube_chan_iface.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

def test(q, bus, conn, stream, bytestream_cls, access_control):
    disco_event = q.expect('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS)

    announce_socks5_proxy(q, stream, disco_event.stanza)

    t.check_conn_properties(q, conn)

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
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
    send_disco_reply(stream, disco_event.stanza, [], [ns.TUBES])

    sync_stream(q, stream)

    offer_new_dbus_tube(q, bus, conn, stream, self_handle, alice_handle, bytestream_cls, access_control)

if __name__ == '__main__':
    t.exec_dbus_tube_test(test)
