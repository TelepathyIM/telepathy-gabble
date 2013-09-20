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
    bob_handle = conn.get_contact_handle_sync('bob@localhost')

    # Try CreateChannel with correct properties
    # Gabble must succeed
    call_async(q, conn.Requests, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_HANDLE: bob_handle,
             cs.STREAM_TUBE_SERVICE: "newecho",
            })

    def find_stream_tube(channels):
        for path, props in channels:
            if props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE:
                return path, props

        return None, None

    def new_chan_predicate(e):
        path, _ = find_stream_tube(e.args[0])
        return path is not None

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels',
                     predicate=new_chan_predicate),
        )

    new_chan_path = ret.value[0]
    new_chan_prop_asv = ret.value[1]
    # State and Parameters are mutables so not announced
    assert cs.TUBE_STATE not in new_chan_prop_asv
    assert cs.TUBE_PARAMETERS not in new_chan_prop_asv
    assert new_chan_path.find("StreamTube") != -1, new_chan_path
    assert new_chan_path.find("SITubesChannel") == -1, new_chan_path

    new_tube_chan = bus.get_object(conn.bus_name, new_chan_path)
    new_tube_iface = dbus.Interface(new_tube_chan, cs.CHANNEL_TYPE_STREAM_TUBE)

    # check State and Parameters
    new_tube_props = new_tube_chan.GetAll(cs.CHANNEL_IFACE_TUBE,
            dbus_interface=cs.PROPERTIES_IFACE)

    # the tube created using the new API is in the "not offered" state
    assert new_tube_props['State'] == cs.TUBE_CHANNEL_STATE_NOT_OFFERED

    _, stream_tube_channel_properties = find_stream_tube(new_sig.args[0])
    assert cs.TUBE_STATE not in stream_tube_channel_properties
    assert cs.TUBE_PARAMETERS not in stream_tube_channel_properties

    # Offer the first tube created
    call_async(q, new_tube_iface, 'Offer', address_type, address2, access_control, new_sample_parameters)

    msg_event, state_event = q.expect_many(
        EventPattern('stream-message'),
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
    stream_tube_id = long(tube['id'])

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
    tube_prop_iface = dbus.Interface(new_tube_chan, cs.PROPERTIES_IFACE)
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

    si_reply_event, _, new_conn_event, socket_event = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeChannelStateChanged',
                args=[cs.TUBE_STATE_OPEN]),
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

    # The CM is the server, so fake a client wanting to talk to it
    # New API tube
    bytestream2 = bytestream_cls(stream, q, 'beta', bob_full_jid,
        self_full_jid, True)
    iq, si = bytestream2.create_si_offer(ns.TUBES)

    stream_node = si.addElement((ns.TUBES, 'stream'))
    stream_node['tube'] = str(stream_tube_id)
    stream.send(iq)

    si_reply_event, new_conn_event, socket_event = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
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

    t.cleanup()

if __name__ == '__main__':
    t.exec_stream_tube_test(test)
