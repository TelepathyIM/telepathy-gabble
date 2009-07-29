"""Test 1-1 tubes support."""

import dbus

from servicetest import call_async, EventPattern, sync_dbus, assertEquals
from gabbletest import acknowledge_iq, sync_stream, make_result_iq
import constants as cs
import ns
import tubetestutil as t

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

def test(q, bus, conn, stream, bytestream_cls,
        address_type, access_control, access_control_param):
    address1 = t.set_up_echo(q, address_type, True, streamfile='stream')
    address2 = t.set_up_echo(q, address_type, True, streamfile='stream2')

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
    assert event.query['node'] == \
        'http://example.com/ICantBelieveItsNotTelepathy#1.2.3'
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES
    stream.send(result)

    # A tube request can be done only if the contact has tube capabilities
    # Ensure that Bob's caps have been received
    sync_stream(q, stream)

    # Also ensure that all the new contact list channels have been announced,
    # so that the NewChannel(s) signals we look for after calling
    # RequestChannel are the ones we wanted.
    sync_dbus(bus, q, conn)

    # Test tubes with Bob. Bob has tube capabilities.
    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    # old tubes API
    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TUBES, cs.HT_CONTACT,
        bob_handle, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 1
    chan_path = ret.value[0]

    t.check_NewChannel_signal(old_sig.args, cs.CHANNEL_TYPE_TUBES, chan_path,
            bob_handle, True)
    t.check_NewChannels_signal(conn, new_sig.args, cs.CHANNEL_TYPE_TUBES, chan_path,
            bob_handle, 'bob@localhost', self_handle)
    old_tubes_channel_properties = new_sig.args[0][0]

    t.check_conn_properties(q, conn, [old_tubes_channel_properties])

    # Try CreateChannel with correct properties
    # Gabble must succeed
    call_async(q, conn.Requests, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_HANDLE: bob_handle,
             cs.STREAM_TUBE_SERVICE: "newecho",
            })

    # the NewTube signal (old API) is not fired now as the tube wasn't offered
    # yet
    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2 # CreateChannel returns 2 values: o, a{sv}
    new_chan_path = ret.value[0]
    new_chan_prop_asv = ret.value[1]
    # State and Parameters are mutables so not announced
    assert cs.TUBE_STATE not in new_chan_prop_asv
    assert cs.TUBE_PARAMETERS not in new_chan_prop_asv
    assert new_chan_path.find("StreamTube") != -1, new_chan_path
    assert new_chan_path.find("SITubesChannel") == -1, new_chan_path
    # The path of the Channel.Type.Tubes object MUST be different to the path
    # of the Channel.Type.StreamTube object !
    assert chan_path != new_chan_path

    new_tube_chan = bus.get_object(conn.bus_name, new_chan_path)
    new_tube_iface = dbus.Interface(new_tube_chan, cs.CHANNEL_TYPE_STREAM_TUBE)

    # check State and Parameters
    new_tube_props = new_tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE,
            dbus_interface=cs.PROPERTIES_IFACE)

    # the tube created using the new API is in the "not offered" state
    assert new_tube_props['State'] == cs.TUBE_CHANNEL_STATE_NOT_OFFERED

    t.check_NewChannel_signal(old_sig.args, cs.CHANNEL_TYPE_STREAM_TUBE,
            new_chan_path, bob_handle, True)
    t.check_NewChannels_signal(conn, new_sig.args, cs.CHANNEL_TYPE_STREAM_TUBE,
            new_chan_path, bob_handle, 'bob@localhost', self_handle)
    stream_tube_channel_properties = new_sig.args[0][0]
    assert cs.TUBE_STATE not in stream_tube_channel_properties
    assert cs.TUBE_PARAMETERS not in stream_tube_channel_properties

    t.check_conn_properties(q, conn,
            [old_tubes_channel_properties, stream_tube_channel_properties])

    tubes_chan = bus.get_object(conn.bus_name, chan_path)
    tubes_iface = dbus.Interface(tubes_chan, cs.CHANNEL_TYPE_TUBES)

    t.check_channel_properties(q, bus, conn, tubes_chan, cs.CHANNEL_TYPE_TUBES,
            bob_handle, "bob@localhost")

    # Create another tube using old API
    call_async(q, tubes_iface, 'OfferStreamTube',
        'echo', sample_parameters, address_type, address1,
        access_control, access_control_param)

    event, return_event, new_chan, new_chans = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-return', method='OfferStreamTube'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'))

    message = event.stanza
    assert message['to'] == bob_full_jid
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


    # the tube channel (new API) is announced
    t.check_NewChannel_signal(new_chan.args, cs.CHANNEL_TYPE_STREAM_TUBE,
        None, bob_handle, False)
    t.check_NewChannels_signal(conn, new_chans.args, cs.CHANNEL_TYPE_STREAM_TUBE,
        new_chan.args[0], bob_handle, "bob@localhost", self_handle)

    props = new_chans.args[0][0][1]
    assert cs.TUBE_STATE not in props
    assert cs.TUBE_PARAMETERS not in props

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

    old_tube_chan = bus.get_object(conn.bus_name, new_chan.args[0])

    tube_basic_props = old_tube_chan.GetAll(cs.CHANNEL,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert tube_basic_props.get("InitiatorHandle") == self_handle

    stream_tube_props = old_tube_chan.GetAll(cs.CHANNEL_TYPE_STREAM_TUBE,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert stream_tube_props.get("Service") == "echo", stream_tube_props

    old_tube_props = old_tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE,
            dbus_interface=cs.PROPERTIES_IFACE, byte_arrays=True)
    assert old_tube_props.get("Parameters") == dbus.Dictionary(sample_parameters)

    # Tube have been created using the old API and so is already offered
    assert old_tube_props['State'] == cs.TUBE_CHANNEL_STATE_REMOTE_PENDING

    t.check_channel_properties(q, bus, conn, tubes_chan, cs.CHANNEL_TYPE_TUBES,
            bob_handle, "bob@localhost")
    t.check_channel_properties(q, bus, conn, old_tube_chan,
            cs.CHANNEL_TYPE_STREAM_TUBE, bob_handle, "bob@localhost",
            cs.TUBE_CHANNEL_STATE_REMOTE_PENDING)

    # Offer the first tube created (new API)
    call_async(q, new_tube_iface, 'Offer', address_type, address2, access_control, new_sample_parameters)

    msg_event, new_tube_sig, state_event = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged'))

    assert state_event.args[0] == cs.TUBE_CHANNEL_STATE_REMOTE_PENDING

    message = msg_event.stanza
    assert message['to'] == bob_full_jid
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

    # check NewTube signal (old API)
    id, initiator_handle, type, service, params, state = new_tube_sig.args
    assert initiator_handle == self_handle
    assert type == 1 # stream
    assert service == 'newecho'
    assert params == new_sample_parameters
    assert state == 1 # remote pending

    # The new tube has been offered, the parameters cannot be changed anymore
    # We need to use call_async to check the error
    tube_prop_iface = dbus.Interface(old_tube_chan, cs.PROPERTIES_IFACE)
    call_async(q, tube_prop_iface, 'Set', cs.CHANNEL_IFACE_TUBE,
            'Parameters', dbus.Dictionary(
            {dbus.String(u'foo2'): dbus.String(u'bar2')},
            signature=dbus.Signature('sv')),
            dbus_interface=cs.PROPERTIES_IFACE)
    set_error = q.expect('dbus-error')
    # check it is *not* correctly changed
    new_tube_props = new_tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE,
            dbus_interface=cs.PROPERTIES_IFACE, byte_arrays=True)
    assert new_tube_props.get("Parameters") == new_sample_parameters, \
            new_tube_props.get("Parameters")

    # The CM is the server, so fake a client wanting to talk to it
    # Old API tube
    bytestream1 = bytestream_cls(stream, q, 'alpha', bob_full_jid,
        self_full_jid, True)
    iq, si = bytestream1.create_si_offer(ns.TUBES)

    stream_node = si.addElement((ns.TUBES, 'stream'))
    stream_node['tube'] = str(stream_tube_id)
    stream.send(iq)

    si_reply_event, _, _, new_conn_event, socket_event = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeStateChanged',
                args=[stream_tube_id, cs.TUBE_STATE_OPEN]),
            EventPattern('dbus-signal', signal='StreamTubeNewConnection',
                args=[stream_tube_id, bob_handle]),
            EventPattern('dbus-signal', signal='NewRemoteConnection'),
            EventPattern('socket-connected'))

    bytestream1.check_si_reply(si_reply_event.stanza)
    tube = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]' % ns.TUBES,
        si_reply_event.stanza)
    assert len(tube) == 1

    handle, access, id = new_conn_event.args
    assert handle == bob_handle
    protocol = socket_event.protocol
    # we don't want to echo the access control byte
    protocol.echoed = False

    # start to read from the transport so we can read the control byte
    protocol.transport.startReading()
    t.check_new_connection_access(q, access_control, access, protocol)
    protocol.echoed = True

    expected_tube = (stream_tube_id, self_handle, cs.TUBE_TYPE_STREAM, 'echo',
        sample_parameters, cs.TUBE_STATE_OPEN)
    tubes = tubes_iface.ListTubes(byte_arrays=True)
    t.check_tube_in_tubes(expected_tube, tubes)

    # The CM is the server, so fake a client wanting to talk to it
    # New API tube
    bytestream2 = bytestream_cls(stream, q, 'beta', bob_full_jid,
        self_full_jid, True)
    iq, si = bytestream2.create_si_offer(ns.TUBES)

    stream_node = si.addElement((ns.TUBES, 'stream'))
    stream_node['tube'] = str(new_stream_tube_id)
    stream.send(iq)

    si_reply_event, _, new_conn_event, socket_event = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeChannelStateChanged',
                args=[cs.TUBE_STATE_OPEN]),
            EventPattern('dbus-signal', signal='NewRemoteConnection'),
            EventPattern('socket-connected'))

    bytestream2.check_si_reply(si_reply_event.stanza)
    tube = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]' % ns.TUBES,
        si_reply_event.stanza)
    assert len(tube) == 1

    handle, access, conn_id = new_conn_event.args
    assert handle == bob_handle
    protocol = socket_event.protocol
    # we don't want to echo the access control byte
    protocol.echoed = False

    # start to read from the transport so we can read the control byte
    protocol.transport.startReading()
    t.check_new_connection_access(q, access_control, access, protocol)
    protocol.echoed = True

    expected_tube = (new_stream_tube_id, self_handle, cs.TUBE_TYPE_STREAM,
        'newecho', new_sample_parameters, cs.TUBE_STATE_OPEN)
    t.check_tube_in_tubes (expected_tube, tubes_iface.ListTubes(byte_arrays=True))

    # have the fake client open the stream
    bytestream1.open_bytestream()

    # have the fake client send us some data
    data = 'hello, world'
    bytestream1.send_data(data)

    binary = bytestream1.get_data(len(data))
    assert binary == data, binary

    # have the fake client open the stream
    bytestream2.open_bytestream()

    # have the fake client send us some data
    data = 'hello, new world'
    bytestream2.send_data(data)

    binary = bytestream2.get_data(len(data))
    assert binary == data, binary

    # peer closes the bytestream
    bytestream2.close()
    e = q.expect('dbus-signal', signal='ConnectionClosed')
    assertEquals(conn_id, e.args[0])
    assertEquals(cs.CONNECTION_LOST, e.args[1])

if __name__ == '__main__':
    t.exec_stream_tube_test(test)
