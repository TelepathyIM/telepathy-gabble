"""Test 1-1 tubes support."""

import base64
import errno
import os

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, watch_tube_signals
from gabbletest import exec_test, acknowledge_iq, sync_stream
from constants import *

from dbus import PROPERTIES_IFACE

from twisted.words.xish import domish, xpath
from twisted.internet.protocol import Factory, Protocol
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ

NS_TUBES = 'http://telepathy.freedesktop.org/xmpp/tubes'
NS_SI = 'http://jabber.org/protocol/si'
NS_FEATURE_NEG = 'http://jabber.org/protocol/feature-neg'
NS_IBB = 'http://jabber.org/protocol/ibb'
NS_X_DATA = 'jabber:x:data'
NS_BYTESTREAMS = 'http://jabber.org/protocol/bytestreams'

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

class Echo(Protocol):
    def dataReceived(self, data):
        self.transport.write(data)

def set_up_echo(name):
    factory = Factory()
    factory.protocol = Echo
    try:
        os.remove(os.getcwd() + '/stream' + name)
    except OSError, e:
        if e.errno != errno.ENOENT:
            raise
    reactor.listenUNIX(os.getcwd() + '/stream' + name, factory)

def check_conn_properties(q, bus, conn, stream, channel_list=None):
    properties = conn.GetAll(
            CONN_IFACE_REQUESTS,
            dbus_interface=PROPERTIES_IFACE)

    if channel_list == None:
        assert properties.get('Channels') == [], properties['Channels']
    else:
        for i in channel_list:
            assert i in properties['Channels'], \
                (i, properties['Channels'])

    assert ({CHANNEL_TYPE: CHANNEL_TYPE_TUBES,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             },
             [TARGET_HANDLE, TARGET_ID
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT
             },
             [TARGET_HANDLE, TARGET_ID, TUBE_PARAMETERS, STREAM_TUBE_SERVICE]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

def check_channel_properties(q, bus, conn, stream, channel, channel_type,
        contact_handle, contact_id, state=None):
    # Exercise basic Channel Properties from spec 0.17.7
    # on the channel of type channel_type
    channel_props = channel.GetAll(CHANNEL, dbus_interface=PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == contact_handle,\
            (channel_props.get('TargetHandle'), contact_handle)
    assert channel_props.get('TargetHandleType') == 1,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == channel_type , channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props, channel_props
    assert CHANNEL_IFACE_GROUP not in \
            channel_props['Interfaces'], \
            channel_props['Interfaces']
    assert channel_props['TargetID'] == contact_id
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    if channel_type == CHANNEL_TYPE_TUBES:
        assert state is None
        assert len(channel_props['Interfaces']) == 0, channel_props['Interfaces']
        supported_socket_types = channel.GetAvailableStreamTubeTypes()
    else:
        assert state is not None
        tube_props = channel.GetAll(CHANNEL_IFACE_TUBE,
                dbus_interface=PROPERTIES_IFACE)
        assert tube_props['State'] == state
        # no strict check but at least check the properties exist
        assert tube_props['Parameters'] is not None
        assert channel_props['Interfaces'] == \
            dbus.Array([CHANNEL_IFACE_TUBE], signature='s'), \
            channel_props['Interfaces']

        stream_tube_props = channel.GetAll(CHANNEL_TYPE_STREAM_TUBE,
                dbus_interface=PROPERTIES_IFACE)
        supported_socket_types = stream_tube_props['SupportedSocketTypes']

    # Support for different socket types. no strict check but at least check
    # there is some support.
    assert len(supported_socket_types) == 3

def check_NewChannel_signal(old_sig, channel_type, chan_path, contact_handle, suppress_handler):
    if chan_path is not None:
        assert old_sig[0] == chan_path
    assert old_sig[1] == channel_type
    assert old_sig[2] == HT_CONTACT
    assert old_sig[3] == contact_handle
    assert old_sig[4] == suppress_handler      # suppress handler

def check_NewChannels_signal(new_sig, channel_type, chan_path, contact_handle,
        contact_id, initiator_handle):
    assert len(new_sig) == 1
    assert len(new_sig[0]) == 1        # one channel
    assert len(new_sig[0][0]) == 2     # two struct members
    assert new_sig[0][0][0] == chan_path
    emitted_props = new_sig[0][0][1]

    assert emitted_props[CHANNEL_TYPE] == channel_type
    assert emitted_props[TARGET_HANDLE_TYPE] == 1
    assert emitted_props[TARGET_HANDLE] == contact_handle
    assert emitted_props[TARGET_ID] == contact_id
    assert emitted_props[REQUESTED] == True
    assert emitted_props[INITIATOR_HANDLE] == initiator_handle
    assert emitted_props[INITIATOR_ID] == 'test@localhost'


def test(q, bus, conn, stream):
    set_up_echo("")
    set_up_echo("2")

    check_conn_properties(q, bus, conn, stream)

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
    item = roster_event.query.addElement('item')
    item['jid'] = 'joe@localhost' # Joe cannot do tubes
    item['subscription'] = 'both'
    stream.send(roster)

    # Send Joe presence is without tube caps
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'joe@localhost/Joe'
    presence['to'] = 'test@localhost/Resource'
    c = presence.addElement('c')
    c['xmlns'] = 'http://jabber.org/protocol/caps'
    c['node'] = 'http://example.com/IDontSupportTubes'
    c['ver'] = '1.0'
    stream.send(presence)

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/disco#info',
        to='joe@localhost/Joe')
    result = event.stanza
    result['type'] = 'result'
    assert event.query['node'] == \
        'http://example.com/IDontSupportTubes#1.0'
    stream.send(result)

    # Send Bob presence and his tube caps
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@localhost/Bob'
    presence['to'] = 'test@localhost/Resource'
    c = presence.addElement('c')
    c['xmlns'] = 'http://jabber.org/protocol/caps'
    c['node'] = 'http://example.com/ICantBelieveItsNotTelepathy'
    c['ver'] = '1.2.3'
    stream.send(presence)

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/disco#info',
        to='bob@localhost/Bob')
    result = event.stanza
    result['type'] = 'result'
    assert event.query['node'] == \
        'http://example.com/ICantBelieveItsNotTelepathy#1.2.3'
    feature = event.query.addElement('feature')
    feature['var'] = NS_TUBES
    stream.send(result)

    # A tube request can be done only if the contact has tube capabilities
    # Ensure that Joe and Bob's caps have been received
    sync_stream(q, stream)

    # new requestotron
    requestotron = dbus.Interface(conn, CONN_IFACE_REQUESTS)

    # Test tubes with Joe. Joe does not have tube capabilities.
    # Gabble does not allow to offer a tube to him.
    joe_handle = conn.RequestHandles(1, ['joe@localhost'])[0]
    call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_TUBES, HT_CONTACT, joe_handle, True);

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    joe_chan_path = ret.value[0]

    joe_tubes_chan = bus.get_object(conn.bus_name, joe_chan_path)
    joe_tubes_iface = dbus.Interface(joe_tubes_chan, CHANNEL_TYPE_TUBES)
    path = os.getcwd() + '/stream'
    call_async(q, joe_tubes_iface, 'OfferStreamTube',
        'echo', sample_parameters, 0, dbus.ByteArray(path), 0, "")
    event = q.expect('dbus-error', method='OfferStreamTube')

    joe_tubes_chan.Close()

    # Test tubes with Bob. Bob does not have tube capabilities.
    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    # old requestotron
    call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_TUBES, 1, bob_handle, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 1
    chan_path = ret.value[0]

    check_NewChannel_signal(old_sig.args, CHANNEL_TYPE_TUBES, chan_path, bob_handle, True)
    check_NewChannels_signal(new_sig.args, CHANNEL_TYPE_TUBES, chan_path,
            bob_handle, 'bob@localhost', conn.GetSelfHandle())
    old_tubes_channel_properties = new_sig.args[0][0]

    check_conn_properties(q, bus, conn, stream, [old_tubes_channel_properties])

    # Try to CreateChannel with unknown properties
    # Gabble must return an error
    call_async(q, requestotron, 'CreateChannel',
            {CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             TARGET_HANDLE: bob_handle,
             'this.property.does.not.exist':
                'this.value.should.not.exist'
            });
    ret = q.expect_many(EventPattern('dbus-error', method='CreateChannel'))
    # CreateChannel failed, we expect no new channel
    check_conn_properties(q, bus, conn, stream, [old_tubes_channel_properties])

    # Try to CreateChannel with missing properties ("Service")
    # Gabble must return an error
    call_async(q, requestotron, 'CreateChannel',
            {CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             TARGET_HANDLE: bob_handle
            });
    ret = q.expect_many(EventPattern('dbus-error', method='CreateChannel'))
    # CreateChannel failed, we expect no new channel
    check_conn_properties(q, bus, conn, stream, [old_tubes_channel_properties])

    # Try to CreateChannel with correct properties
    # Gabble must succeed
    call_async(q, requestotron, 'CreateChannel',
            {CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             TARGET_HANDLE: bob_handle,
             STREAM_TUBE_SERVICE: 'newecho',
             TUBE_PARAMETERS: dbus.Dictionary({'foo': 'bar'}, signature='sv'),
            });

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
    assert (TUBE_STATE) not in new_chan_prop_asv
    assert (TUBE_PARAMETERS) not in new_chan_prop_asv
    assert new_chan_path.find("StreamTube") != -1, new_chan_path
    assert new_chan_path.find("SITubesChannel") == -1, new_chan_path
    # The path of the Channel.Type.Tubes object MUST be different to the path
    # of the Channel.Type.StreamTube object !
    assert chan_path != new_chan_path

    new_tube_chan = bus.get_object(conn.bus_name, new_chan_path)
    new_tube_iface = dbus.Interface(new_tube_chan, CHANNEL_TYPE_STREAM_TUBE)

    # check State and Parameters
    new_tube_props = new_tube_chan.GetAll(CHANNEL_IFACE_TUBE,
            dbus_interface=PROPERTIES_IFACE)

    # the tube created using the old API is in the "not offered" state
    assert new_tube_props['State'] == TUBE_CHANNEL_STATE_NOT_OFFERED
    assert new_tube_props['Parameters'] == {'foo': 'bar'}

    check_NewChannel_signal(old_sig.args, CHANNEL_TYPE_STREAM_TUBE,
            new_chan_path, bob_handle, True)
    check_NewChannels_signal(new_sig.args, CHANNEL_TYPE_STREAM_TUBE, new_chan_path, \
            bob_handle, 'bob@localhost', conn.GetSelfHandle())
    stream_tube_channel_properties = new_sig.args[0][0]

    check_conn_properties(q, bus, conn, stream,
            [old_tubes_channel_properties, stream_tube_channel_properties])

    tubes_chan = bus.get_object(conn.bus_name, chan_path)

    tubes_iface = dbus.Interface(tubes_chan, CHANNEL_TYPE_TUBES)

    check_channel_properties(q, bus, conn, stream, tubes_chan, CHANNEL_TYPE_TUBES,
            bob_handle, "bob@localhost")

    # Create another tube using old API
    call_async(q, tubes_iface, 'OfferStreamTube',
        'echo', sample_parameters, 0, dbus.ByteArray(path), 0, "")

    event, return_event, new_chan, new_chans = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-return', method='OfferStreamTube'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'))

    message = event.stanza
    assert message['to'] == 'bob@localhost/Bob' # check the resource
    tube_nodes = xpath.queryForNodes('/message/tube[@xmlns="%s"]' % NS_TUBES,
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
    check_NewChannel_signal(new_chan.args, CHANNEL_TYPE_STREAM_TUBE,
        None, bob_handle, False)
    check_NewChannels_signal(new_chans.args, CHANNEL_TYPE_STREAM_TUBE, new_chan.args[0],
        bob_handle, "bob@localhost", self_handle)

    props = new_chans.args[0][0][1]

    # We offered a tube using the old tube API and created one with the new
    # API, so there are 2 tubes. Check the new tube API works
    assert len(filter(lambda x:
                  x[1] == CHANNEL_TYPE_TUBES,
                  conn.ListChannels())) == 1
    channels = filter(lambda x:
      x[1] == CHANNEL_TYPE_STREAM_TUBE and
      x[0] == new_chan_path,
      conn.ListChannels())
    assert len(channels) == 1
    assert new_chan_path == channels[0][0]

    old_tube_chan = bus.get_object(conn.bus_name, new_chan.args[0])

    tube_basic_props = old_tube_chan.GetAll(CHANNEL,
            dbus_interface=PROPERTIES_IFACE)
    assert tube_basic_props.get("InitiatorHandle") == self_handle

    stream_tube_props = old_tube_chan.GetAll(CHANNEL_TYPE_STREAM_TUBE,
            dbus_interface=PROPERTIES_IFACE)
    assert stream_tube_props.get("Service") == "echo", stream_tube_props

    old_tube_props = old_tube_chan.GetAll(CHANNEL_IFACE_TUBE,
            dbus_interface=PROPERTIES_IFACE, byte_arrays=True)
    assert old_tube_props.get("Parameters") == dbus.Dictionary(sample_parameters)

    # Tube have been created using the old API and so is already offered
    assert old_tube_props['State'] == TUBE_CHANNEL_STATE_REMOTE_PENDING

    check_channel_properties(q, bus, conn, stream, tubes_chan, CHANNEL_TYPE_TUBES,
            bob_handle, "bob@localhost")
    check_channel_properties(q, bus, conn, stream, old_tube_chan,
            CHANNEL_TYPE_STREAM_TUBE, bob_handle, "bob@localhost", TUBE_CHANNEL_STATE_REMOTE_PENDING)

    # change the parameters of the not offered tube (the one created using the
    # new API)
    new_tube_chan.Set(CHANNEL_IFACE_TUBE, 'Parameters', new_sample_parameters,
            dbus_interface=PROPERTIES_IFACE)
    # check it is correctly changed
    new_tube_props = new_tube_chan.GetAll(CHANNEL_IFACE_TUBE,
            dbus_interface=PROPERTIES_IFACE, byte_arrays=True)
    assert new_tube_props.get("Parameters") == new_sample_parameters, \
            new_tube_props.get("Parameters")

    # Offer the first tube created (new API)
    path2 = os.getcwd() + '/stream2'
    call_async(q, new_tube_iface, 'OfferStreamTube',
        0, dbus.ByteArray(path2), 0, "")

    msg_event, new_tube_sig, state_event = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('dbus-signal', signal='TubeChannelStateChanged'))

    assert state_event.args[0] == TUBE_CHANNEL_STATE_REMOTE_PENDING

    message = msg_event.stanza
    assert message['to'] == 'bob@localhost/Bob' # check the resource
    tube_nodes = xpath.queryForNodes('/message/tube[@xmlns="%s"]' % NS_TUBES,
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
    tube_prop_iface = dbus.Interface(old_tube_chan, PROPERTIES_IFACE)
    call_async(q, tube_prop_iface, 'Set', CHANNEL_IFACE_TUBE,
            'Parameters', dbus.Dictionary(
            {dbus.String(u'foo2'): dbus.String(u'bar2')},
            signature=dbus.Signature('sv')),
            dbus_interface=PROPERTIES_IFACE)
    set_error = q.expect('dbus-error')
    # check it is *not* correctly changed
    new_tube_props = new_tube_chan.GetAll(CHANNEL_IFACE_TUBE,
            dbus_interface=PROPERTIES_IFACE, byte_arrays=True)
    assert new_tube_props.get("Parameters") == new_sample_parameters, \
            new_tube_props.get("Parameters")

    # The CM is the server, so fake a client wanting to talk to it
    # Old API tube
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    si = iq.addElement((NS_SI, 'si'))
    si['id'] = 'alpha'
    si['profile'] = NS_TUBES
    feature = si.addElement((NS_FEATURE_NEG, 'feature'))
    x = feature.addElement((NS_X_DATA, 'x'))
    x['type'] = 'form'
    field = x.addElement((None, 'field'))
    field['var'] = 'stream-method'
    field['type'] = 'list-single'
    option = field.addElement((None, 'option'))
    value = option.addElement((None, 'value'))
    value.addContent(NS_IBB)

    stream_node = si.addElement((NS_TUBES, 'stream'))
    stream_node['tube'] = str(stream_tube_id)
    stream.send(iq)

    si_reply_event, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeStateChanged',
                args=[stream_tube_id, 2])) # 2 == OPEN
    iq = si_reply_event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % NS_SI,
        iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    assert str(proto) == NS_IBB
    tube = xpath.queryForNodes('/si/tube[@xmlns="%s"]' % NS_TUBES, si)
    assert len(tube) == 1

    q.expect('dbus-signal', signal='StreamTubeNewConnection',
        args=[stream_tube_id, bob_handle])

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert (
        stream_tube_id,
        self_handle,
        1,      # Unix stream
        'echo',
        sample_parameters,
        2,      # OPEN
        ) in tubes

    # The CM is the server, so fake a client wanting to talk to it
    # New API tube
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    si = iq.addElement((NS_SI, 'si'))
    si['id'] = 'beta'
    si['profile'] = NS_TUBES
    feature = si.addElement((NS_FEATURE_NEG, 'feature'))
    x = feature.addElement((NS_X_DATA, 'x'))
    x['type'] = 'form'
    field = x.addElement((None, 'field'))
    field['var'] = 'stream-method'
    field['type'] = 'list-single'
    option = field.addElement((None, 'option'))
    value = option.addElement((None, 'value'))
    value.addContent(NS_IBB)

    stream_node = si.addElement((NS_TUBES, 'stream'))
    stream_node['tube'] = str(new_stream_tube_id)
    stream.send(iq)

    si_reply_event, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeChannelStateChanged',
                args=[2])) # 2 == OPEN
    iq = si_reply_event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % NS_SI,
        iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    assert str(proto) == NS_IBB
    tube = xpath.queryForNodes('/si/tube[@xmlns="%s"]' % NS_TUBES, si)
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
        2,      # OPEN
        ) in tubes, tubes

    # have the fake client open the stream
    # Old tube API
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    open = iq.addElement((NS_IBB, 'open'))
    open['sid'] = 'alpha'
    open['block-size'] = '4096'
    stream.send(iq)

    q.expect('stream-iq', iq_type='result')
    # have the fake client send us some data
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    message['from'] = 'bob@localhost/Bob'
    data_node = message.addElement((NS_IBB, 'data'))
    data_node['sid'] = 'alpha'
    data_node['seq'] = '0'
    data_node.addContent(base64.b64encode('hello, world'))
    stream.send(message)

    event = q.expect('stream-message', to='bob@localhost/Bob')
    message = event.stanza

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == 'alpha'
    binary = base64.b64decode(str(ibb_data))
    assert binary == 'hello, world'

    # have the fake client open the stream
    # New tube API
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    open = iq.addElement((NS_IBB, 'open'))
    open['sid'] = 'beta'
    open['block-size'] = '4096'
    stream.send(iq)

    q.expect('stream-iq', iq_type='result')
    # have the fake client send us some data
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    message['from'] = 'bob@localhost/Bob'
    data_node = message.addElement((NS_IBB, 'data'))
    data_node['sid'] = 'beta'
    data_node['seq'] = '0'
    data_node.addContent(base64.b64encode('hello, new world'))
    stream.send(message)

    event = q.expect('stream-message', to='bob@localhost/Bob')
    message = event.stanza

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == 'beta'
    binary = base64.b64decode(str(ibb_data))
    assert binary == 'hello, new world'

    # OK, how about D-Bus?
    call_async(q, tubes_iface, 'OfferDBusTube', 'com.example.TestCase', sample_parameters)

    event = q.expect('stream-iq', iq_type='set', to='bob@localhost/Bob')
    iq = event.stanza
    si_nodes = xpath.queryForNodes('/iq/si', iq)
    assert si_nodes is not None
    assert len(si_nodes) == 1
    si = si_nodes[0]
    assert si['profile'] == NS_TUBES
    dbus_stream_id = si['id']

    feature = xpath.queryForNodes('/si/feature', si)[0]
    x = xpath.queryForNodes('/feature/x', feature)[0]
    assert x['type'] == 'form'
    field = xpath.queryForNodes('/x/field', x)[0]
    assert field['var'] == 'stream-method'
    assert field['type'] == 'list-single'
    value = xpath.queryForNodes('/field/option/value', field)[0]
    assert str(value) == NS_BYTESTREAMS
    value = xpath.queryForNodes('/field/option/value', field)[1]
    assert str(value) == NS_IBB

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
    result['to'] = 'test@localhost/Resource'
    res_si = result.addElement((NS_SI, 'si'))
    res_feature = res_si.addElement((NS_FEATURE_NEG, 'feature'))
    res_x = res_feature.addElement((NS_X_DATA, 'x'))
    res_x['type'] = 'submit'
    res_field = res_x.addElement((None, 'field'))
    res_field['var'] = 'stream-method'
    res_value = res_field.addElement((None, 'value'))
    res_value.addContent(NS_IBB)

    stream.send(result)

    event = q.expect('stream-iq', iq_type='set', to='bob@localhost/Bob')
    iq = event.stanza
    open = xpath.queryForNodes('/iq/open', iq)[0]
    assert open.uri == NS_IBB
    assert open['sid'] == dbus_stream_id

    result = IQ(stream, 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result['to'] = 'test@localhost/Resource'

    stream.send(result)

    q.expect('dbus-signal', signal='TubeStateChanged',
        args=[dbus_tube_id, 2]) # 2 == OPEN

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert (
        dbus_tube_id,
        self_handle,
        0,      # DBUS
        'com.example.TestCase',
        sample_parameters,
        2,      # OPEN
        ) in tubes
    assert (
        stream_tube_id,
        self_handle,
        1,      # stream
        'echo',
        sample_parameters,
        2,      # OPEN
        ) in tubes

    dbus_tube_adr = tubes_iface.GetDBusTubeAddress(dbus_tube_id)
    dbus_tube_conn = Connection(dbus_tube_adr)

    signal = SignalMessage('/', 'foo.bar', 'baz')
    my_bus_name = ':123.whatever.you.like'
    signal.set_sender(my_bus_name)
    signal.append(42, signature='u')
    dbus_tube_conn.send_message(signal)

    event = q.expect('stream-message')
    message = event.stanza

    assert message['to'] == 'bob@localhost/Bob'

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == dbus_stream_id
    binary = base64.b64decode(str(ibb_data))
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
    msg = domish.Element(('jabber:client', 'message'))
    msg['to'] = 'test@localhost/Resource'
    msg['from'] = 'bob@localhost/Bob'
    data_node = msg.addElement('data', NS_IBB)
    data_node['sid'] = dbus_stream_id
    data_node['seq'] = str(seq)
    data_node.addContent(base64.b64encode(dbus_message))
    stream.send(msg)
    seq += 1

    # ... and a message one byte at a time ...

    for byte in dbus_message:
        msg = domish.Element(('jabber:client', 'message'))
        msg['to'] = 'test@localhost/Resource'
        msg['from'] = 'bob@localhost/Bob'
        data_node = msg.addElement('data', NS_IBB)
        data_node['sid'] = dbus_stream_id
        data_node['seq'] = str(seq)
        data_node.addContent(base64.b64encode(byte))
        stream.send(msg)
        seq += 1

    # ... and two messages in one go

    msg = domish.Element(('jabber:client', 'message'))
    msg['to'] = 'test@localhost/Resource'
    msg['from'] = 'bob@localhost/Bob'
    data_node = msg.addElement('data', NS_IBB)
    data_node['sid'] = dbus_stream_id
    data_node['seq'] = str(seq)
    data_node.addContent(base64.b64encode(dbus_message + dbus_message))
    stream.send(msg)
    seq += 1

    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)
    q.expect('tube-signal', signal='baz', args=[42], tube=dbus_tube_conn)

    # OK, now let's try to accept a D-Bus tube
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    si = iq.addElement((NS_SI, 'si'))
    si['id'] = 'beta'
    si['profile'] = NS_TUBES
    feature = si.addElement((NS_FEATURE_NEG, 'feature'))
    x = feature.addElement((NS_X_DATA, 'x'))
    x['type'] = 'form'
    field = x.addElement((None, 'field'))
    field['var'] = 'stream-method'
    field['type'] = 'list-single'
    option = field.addElement((None, 'option'))
    value = option.addElement((None, 'value'))
    value.addContent(NS_IBB)

    tube = si.addElement((NS_TUBES, 'tube'))
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
    assert type == 0 # D-Bus tube
    assert service == 'com.example.TestCase2'
    assert parameters == {'login': 'TEST'}
    assert state == 0 # local pending

    # accept the tube
    call_async(q, tubes_iface, 'AcceptDBusTube', id)

    event = q.expect('stream-iq', iq_type='result')
    iq = event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % NS_SI,
        iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    assert str(proto) == NS_IBB
    tube = xpath.queryForNodes('/si/tube[@xmlns="%s"]' % NS_TUBES, si)
    assert len(tube) == 1

    # Init the IBB bytestream
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    open = iq.addElement((NS_IBB, 'open'))
    open['sid'] = 'beta'
    open['block-size'] = '4096'
    stream.send(iq)

    event = q.expect('dbus-return', method='AcceptDBusTube')
    address = event.value[0]
    # FIXME: this is currently broken. See FIXME in tubes-channel.c
    #assert len(address) > 0

    event = q.expect('dbus-signal', signal='TubeStateChanged',
        args=[69, 2]) # 2 == OPEN
    id = event.args[0]
    state = event.args[1]

    # OK, we're done
    conn.Disconnect()

    q.expect('tube-signal', signal='Disconnected')
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
