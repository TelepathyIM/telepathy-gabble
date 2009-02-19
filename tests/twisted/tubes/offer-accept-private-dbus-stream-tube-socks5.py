"""Test 1-1 tubes support."""

import os

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, watch_tube_signals, sync_dbus
from gabbletest import exec_test, acknowledge_iq, sync_stream
import constants as cs
import ns
import tubetestutil as t
from bytestream import S5BFactory, socks5_expect_connection, socks5_connect, \
    send_socks5_init, expect_socks5_init, expect_socks5_reply, \
    create_si_offer

from twisted.words.xish import domish, xpath
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ

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

def test(q, bus, conn, stream):
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

    requestotron = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    # Test tubes with Bob. Bob does not have tube capabilities.
    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    # old requestotron
    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TUBES, cs.HT_CONTACT,
            bob_handle, True);

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 1
    chan_path = ret.value[0]

    t.check_NewChannel_signal(old_sig.args, cs.CHANNEL_TYPE_TUBES, chan_path,
        bob_handle, True)
    t.check_NewChannels_signal(new_sig.args, cs.CHANNEL_TYPE_TUBES, chan_path,
            bob_handle, 'bob@localhost', self_handle)
    old_tubes_channel_properties = new_sig.args[0][0]

    t.check_conn_properties(q, conn, [old_tubes_channel_properties])
    # Try to CreateChannel with correct properties
    # Gabble must succeed
    call_async(q, requestotron, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_HANDLE: bob_handle,
             cs.STREAM_TUBE_SERVICE: "newecho",
            })
    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    new_chan_path, new_chan_prop_asv = ret.value

    assert new_chan_path.find("StreamTube") != -1, new_chan_path
    assert new_chan_path.find("SITubesChannel") == -1, new_chan_path
    # The path of the Channel.Type.Tubes object MUST be different to the path
    # of the Channel.Type.StreamTube object !
    assert chan_path != new_chan_path

    t.check_NewChannel_signal(old_sig.args, cs.CHANNEL_TYPE_STREAM_TUBE,
            new_chan_path, bob_handle, True)
    t.check_NewChannels_signal(new_sig.args, cs.CHANNEL_TYPE_STREAM_TUBE,
            new_chan_path, bob_handle, 'bob@localhost', self_handle)
    stream_tube_channel_properties = new_sig.args[0][0]

    t.check_conn_properties(q, conn,
            [old_tubes_channel_properties, stream_tube_channel_properties])

    tubes_chan = bus.get_object(conn.bus_name, chan_path)
    tubes_iface = dbus.Interface(tubes_chan, cs.CHANNEL_TYPE_TUBES)

    t.check_channel_properties(q, bus, conn, tubes_chan, cs.CHANNEL_TYPE_TUBES,
            bob_handle, "bob@localhost")

    # Offer the tube, old API
    # FIXME: make set_up_echo return this
    path = os.getcwd() + '/stream'
    call_async(q, tubes_iface, 'OfferStreamTube',
        'echo', sample_parameters, 0, dbus.ByteArray(path), 0, "")

    event = q.expect('stream-message')
    message = event.stanza
    assert message['to'] == bob_full_jid # check the resource
    tube_nodes = xpath.queryForNodes('/message/tube[@xmlns="%s"]' % ns.TUBES,
        message)
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    tube = tube_nodes[0]

    assert tube['service'] == 'echo'
    assert tube['type'] == 'stream'
    assert not tube.hasAttribute('initiator')
    stream_tube_id = long(tube['id'])

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

    # We offered a tube using the old tube API and created one with the new
    # API, so there are 2 tubes. Check the new tube API works
    assert len(filter(lambda x:
                  x[1] == cs.CHANNEL_TYPE_TUBES,
                  conn.ListChannels())) == 1
    channels = filter(lambda x:
      x[1] == cs.CHANNEL_TYPE_STREAM_TUBE and
      x[0] == new_chan_path,
      conn.ListChannels())
    assert len(channels) == 1
    assert new_chan_path == channels[0][0]

    tube_chan = bus.get_object(conn.bus_name, channels[0][0])
    tube_iface = dbus.Interface(tube_chan, cs.CHANNEL_TYPE_STREAM_TUBE)
    tube_prop_iface = dbus.Interface(tube_chan, dbus.PROPERTIES_IFACE)

    service = tube_prop_iface.Get(cs.CHANNEL_TYPE_STREAM_TUBE, 'Service')
    assert service == "newecho", service

    t.check_channel_properties(q, bus, conn, tubes_chan, cs.CHANNEL_TYPE_TUBES,
            bob_handle, "bob@localhost")
    t.check_channel_properties(q, bus, conn, tube_chan,
            cs.CHANNEL_TYPE_STREAM_TUBE, bob_handle, "bob@localhost",
            cs.TUBE_STATE_NOT_OFFERED)

    # Offer the tube, new API
    path2 = os.getcwd() + '/stream2'
    call_async(q, tube_iface, 'OfferStreamTube',
        0, dbus.ByteArray(path2), 0, "", new_sample_parameters)

    event = q.expect('stream-message')
    message = event.stanza
    assert message['to'] == bob_full_jid # check the resource
    tube_nodes = xpath.queryForNodes('/message/tube[@xmlns="%s"]' % ns.TUBES,
        message)
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    tube = tube_nodes[0]

    assert tube['service'] == 'newecho'
    assert tube['type'] == 'stream'
    assert not tube.hasAttribute('initiator')
    new_stream_tube_id = long(tube['id'])

    params = {}
    parameter_nodes = xpath.queryForNodes('/tube/parameters/parameter', tube)
    for node in parameter_nodes:
        assert node['name'] not in params
        params[node['name']] = (node['type'], str(node))
    assert params == {'ay': ('bytes', 'bmV3aGVsbG8='),
                      's': ('str', 'newhello'),
                      'i': ('int', '-123'),
                      'u': ('uint', '123'),
                     }
    # The new tube has been offered, the parameters cannot be changed anymore
    # We need to use call_async to check the error
    call_async(q, tube_prop_iface, 'Set', cs.CHANNEL_IFACE_TUBE,
            'Parameters', dbus.Dictionary(
            {dbus.String(u'foo2'): dbus.String(u'bar2')},
            signature=dbus.Signature('sv')),
            dbus_interface=dbus.PROPERTIES_IFACE)
    set_error = q.expect('dbus-error')
    # check it is *not* correctly changed
    params = tube_prop_iface.Get(cs.CHANNEL_IFACE_TUBE, 'Parameters',
            byte_arrays=True)
    assert params == new_sample_parameters, (params, new_sample_parameters)

    # The CM is the server, so fake a client wanting to talk to it
    # Old API tube
    iq, si = create_si_offer(stream, bob_full_jid, self_full_jid, 'alpha', ns.TUBES,
        [ns.BYTESTREAMS])

    stream_node = si.addElement((ns.TUBES, 'stream'))
    stream_node['tube'] = str(stream_tube_id)
    stream.send(iq)

    si_reply_event, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeStateChanged',
                args=[stream_tube_id, cs.TUBE_STATE_OPEN]))
    iq = si_reply_event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % ns.SI,
        iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    assert str(proto) == ns.BYTESTREAMS
    tube = xpath.queryForNodes('/si/tube[@xmlns="%s"]' % ns.TUBES, si)
    assert len(tube) == 1

    q.expect('dbus-signal', signal='StreamTubeNewConnection',
        args=[stream_tube_id, bob_handle])

    expected_tube = (stream_tube_id, self_handle, cs.TUBE_TYPE_STREAM, 'echo',
        sample_parameters, cs.TUBE_STATE_OPEN)
    tubes = tubes_iface.ListTubes(byte_arrays=True)
    t.check_tube_in_tubes(expected_tube, tubes)

    # The CM is the server, so fake a client wanting to talk to it
    # New API tube
    iq, si = create_si_offer(stream, bob_full_jid, self_full_jid, 'beta', ns.TUBES,
        [ns.BYTESTREAMS])

    stream_node = si.addElement((ns.TUBES, 'stream'))
    stream_node['tube'] = str(new_stream_tube_id)
    stream.send(iq)

    si_reply_event, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeChannelStateChanged',
                args=[cs.TUBE_STATE_OPEN]))
    iq = si_reply_event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % ns.SI,
        iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    assert str(proto) == ns.BYTESTREAMS
    tube = xpath.queryForNodes('/si/tube[@xmlns="%s"]' % ns.TUBES, si)
    assert len(tube) == 1

    q.expect('dbus-signal', signal='StreamTubeNewConnection',
        args=[bob_handle])

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert (
        new_stream_tube_id,
        self_handle,
        1,      # Unix stream
        'newecho',
        new_sample_parameters,
        cs.TUBE_STATE_OPEN,
        ) in tubes, tubes

    reactor.listenTCP(5086, S5BFactory(q.append))

    # have the fake client open the stream
    # Old tube API
    send_socks5_init(stream, bob_full_jid, self_full_jid, 'alpha', 'tcp', [
        # Not working streamhost
        ('invalid.invalid', 'invalid.invalid', '5086'),
        # Working streamhost
        (bob_full_jid, '127.0.0.1', '5086'),
        # This works too but should not be tried as gabble should just
        # connect to the previous one
        ('bob@localhost', '127.0.0.1', '5086')])

    transport = socks5_expect_connection(q, 'alpha', bob_full_jid, self_full_jid)

    streamhost_used = expect_socks5_reply(q)
    assert streamhost_used['jid'] == bob_full_jid

    transport.write("HELLO WORLD")
    event = q.expect('s5b-data-received')
    assert event.data == 'hello world'

    # this connection is disconnected
    transport.loseConnection()

    reactor.listenTCP(5085, S5BFactory(q.append))

    send_socks5_init(stream, bob_full_jid, self_full_jid, 'beta', 'tcp', [
        # Not working streamhost
        ('invalid.invalid', 'invalid.invalid', '5086'),
        # Working streamhost
        (bob_full_jid, '127.0.0.1', '5086'),
        # This works too but should not be tried as gabble should just
        # connect to the previous one
        ('bob@localhost', '127.0.0.1', '5086')])

    transport = socks5_expect_connection(q, 'beta', bob_full_jid, self_full_jid)

    streamhost_used = expect_socks5_reply(q)
    assert streamhost_used['jid'] == bob_full_jid

    transport.write("HELLO, NEW WORLD")
    event = q.expect('s5b-data-received')
    assert event.data == 'hello, new world'

    # OK, how about D-Bus?
    call_async(q, tubes_iface, 'OfferDBusTube',
        'com.example.TestCase', sample_parameters)

    event = q.expect('stream-iq', iq_type='set', to=bob_full_jid)
    iq = event.stanza
    si_nodes = xpath.queryForNodes('/iq/si', iq)
    assert si_nodes is not None
    assert len(si_nodes) == 1
    si = si_nodes[0]
    assert si['profile'] == ns.TUBES
    dbus_stream_id = si['id']

    feature = xpath.queryForNodes('/si/feature', si)[0]
    x = xpath.queryForNodes('/feature/x', feature)[0]
    assert x['type'] == 'form'
    field = xpath.queryForNodes('/x/field', x)[0]
    assert field['var'] == 'stream-method'
    assert field['type'] == 'list-single'
    value = xpath.queryForNodes('/field/option/value', field)[0]
    assert str(value) == ns.BYTESTREAMS
    value = xpath.queryForNodes('/field/option/value', field)[1]
    assert str(value) == ns.IBB

    tube = xpath.queryForNodes('/si/tube', si)[0]
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

    result = IQ(stream, 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result['to'] = self_full_jid
    res_si = result.addElement((ns.SI, 'si'))
    res_feature = res_si.addElement((ns.FEATURE_NEG, 'feature'))
    res_x = res_feature.addElement((ns.X_DATA, 'x'))
    res_x['type'] = 'submit'
    res_field = res_x.addElement((None, 'field'))
    res_field['var'] = 'stream-method'
    res_value = res_field.addElement((None, 'value'))
    res_value.addContent(ns.BYTESTREAMS)

    stream.send(result)

    id, mode, sid, hosts = expect_socks5_init(q)
    assert mode == 'tcp'
    assert sid == dbus_stream_id
    jid, host, port = hosts[0]

    transport = socks5_connect(q, host, port, sid, self_full_jid, bob_full_jid)

    result = IQ(stream, 'result')
    result['id'] = id
    result['from'] = bob_full_jid
    result['to'] = self_full_jid

    stream.send(result)

    q.expect('dbus-signal', signal='TubeStateChanged',
        args=[dbus_tube_id, cs.TUBE_STATE_OPEN])

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    expected_dtube = (dbus_tube_id, self_handle, cs.TUBE_TYPE_DBUS,
        'com.example.TestCase', sample_parameters, cs.TUBE_STATE_OPEN)
    expected_stube = (stream_tube_id, self_handle, cs.TUBE_TYPE_STREAM,
        'echo', sample_parameters, cs.TUBE_STATE_OPEN)
    t.check_tube_in_tubes(expected_dtube, tubes)
    t.check_tube_in_tubes(expected_stube, tubes)

    dbus_tube_adr = tubes_iface.GetDBusTubeAddress(dbus_tube_id)
    dbus_tube_conn = Connection(dbus_tube_adr)

    signal = SignalMessage('/', 'foo.bar', 'baz')
    my_bus_name = ':123.whatever.you.like'
    signal.set_sender(my_bus_name)
    signal.append(42, signature='u')
    dbus_tube_conn.send_message(signal)

    event = q.expect('s5b-data-received')
    dbus_message = event.data

    # little and big endian versions of: SIGNAL, NO_REPLY, protocol v1,
    # 4-byte payload
    assert dbus_message.startswith('l\x04\x01\x01' '\x04\x00\x00\x00') or \
           dbus_message.startswith('B\x04\x01\x01' '\x00\x00\x00\x04')
    # little and big endian versions of the 4-byte payload, UInt32(42)
    assert (dbus_message[0] == 'l' and dbus_message.endswith('\x2a\x00\x00\x00')) or \
           (dbus_message[0] == 'B' and dbus_message.endswith('\x00\x00\x00\x2a'))
    # XXX: verify that it's actually in the "sender" slot, rather than just
    # being in the message somewhere
    assert my_bus_name in dbus_message

    watch_tube_signals(q, dbus_tube_conn)

    # Have the fake client send us a message all in one go...
    transport.write(dbus_message)

    # ... and a message one byte at a time ...
    for byte in dbus_message:
        transport.write(byte)

    # ... and two messages in one go
    transport.write(dbus_message + dbus_message)

    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)

    # OK, now let's try to accept a D-Bus tube
    iq, si = create_si_offer(stream, bob_full_jid, self_full_jid, 'beta', ns.TUBES,
        [ns.BYTESTREAMS])

    tube = si.addElement((ns.TUBES, 'tube'))
    tube['type'] = 'dbus'
    tube['service'] = 'com.example.TestCase2'
    tube['id'] = '69'
    parameters = tube.addElement((None, 'parameters'))
    parameter = parameters.addElement((None, 'parameter'))
    parameter['type'] = 'str'
    parameter['name'] = 'login'
    parameter.addContent('TEST')

    stream.send(iq)

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

    # accept the tube
    call_async(q, tubes_iface, 'AcceptDBusTube', id)

    event = q.expect('stream-iq', iq_type='result')
    iq = event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % ns.SI,
        iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    assert str(proto) == ns.BYTESTREAMS
    tube = xpath.queryForNodes('/si/tube[@xmlns="%s"]' % ns.TUBES, si)
    assert len(tube) == 1

    reactor.listenTCP(5084, S5BFactory(q.append))

    # Init the SOCKS5 bytestream
    send_socks5_init(stream, bob_full_jid, self_full_jid, 'beta', 'tcp', [
        (bob_full_jid, '127.0.0.1', '5084')])

    event, _ = q.expect_many(
        EventPattern('dbus-return', method='AcceptDBusTube'),
        EventPattern('s5b-connected'))
    address = event.value[0]
    assert len(address) > 0

    # OK, we're done
    conn.Disconnect()

    q.expect('tube-signal', signal='Disconnected')
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
