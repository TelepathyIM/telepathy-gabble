"""Test 1-1 tubes support."""

import base64
import errno
import os

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath
from twisted.internet.protocol import Factory, Protocol
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ

NS_TUBES = 'http://telepathy.freedesktop.org/xmpp/tubes'
NS_SI = 'http://jabber.org/protocol/si'
NS_FEATURE_NEG = 'http://jabber.org/protocol/feature-neg'
NS_IBB = 'http://jabber.org/protocol/ibb'
NS_X_DATA = 'jabber:x:data'

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')


class Echo(Protocol):
    def dataReceived(self, data):
        self.transport.write(data)

def set_up_echo():
    factory = Factory()
    factory.protocol = Echo
    try:
        os.remove(os.getcwd() + '/stream')
    except OSError, e:
        if e.errno != errno.ENOENT:
            raise
    reactor.listenUNIX(os.getcwd() + '/stream', factory)


def test(q, bus, conn, stream):
    set_up_echo()
    conn.Connect()

    _, vcard_event, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns='jabber:iq:roster'))

    acknowledge_iq(stream, vcard_event.stanza)

    roster = roster_event.stanza
    roster['type'] = 'result'
    item = roster_event.query.addElement('item')
    item['jid'] = 'bob@localhost'
    item['subscription'] = 'both'
    stream.send(roster)

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

    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]
    chan_path = conn.RequestChannel(tp_name_prefix + '.Channel.Type.Tubes', 1,
            bob_handle, True)
    tubes_chan = bus.get_object(conn.bus_name, chan_path)
    tubes_iface = dbus.Interface(tubes_chan,
        tp_name_prefix + '.Channel.Type.Tubes')

    self_handle = conn.GetSelfHandle()

    # Unix socket
    path = os.getcwd() + '/stream'
    call_async(q, tubes_iface, 'OfferStreamTube',
        'echo', sample_parameters, 0, dbus.ByteArray(path), 0, "")

    event = q.expect('stream-message')
    message = event.stanza
    tube_nodes = xpath.queryForNodes('/message/tube[@xmlns="%s"]' % NS_TUBES,
        message)
    if tube_nodes is None:
        return False

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

    # The CM is the server, so fake a client wanting to talk to it
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
    assert tubes == [(
        stream_tube_id,
        self_handle,
        1,      # Unix stream
        'echo',
        sample_parameters,
        2,      # OPEN
        )]

    # have the fake client open the stream
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
    return True

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

    # OK, how about D-Bus?
    call_async(q, tubes_iface, 'OfferDBusTube',
        'com.example.TestCase', sample_parameters)

    event = q.expect('stream-iq', iq_type='set', to='bob@localhost/Bob')
    iq = event.stanza
    si_nodes = xpath.queryForNodes('/iq/si', iq)
    if si_nodes is None:
        return False

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
    assert sorted(tubes) == sorted([(
        dbus_tube_id,
        self_handle,
        0,      # DBUS
        'com.example.TestCase',
        sample_parameters,
        2,      # OPEN
        ),(
        stream_tube_id,
        self_handle,
        1,      # stream
        'echo',
        sample_parameters,
        2,      # OPEN
        )])

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

    def got_signal_cb(*args, **kwargs):
        q.handle_event(Event('tube-signal',
            path=kwargs['path'],
            signal=kwargs['member'],
            args=map(unwrap, args)))

    dbus_tube_conn.add_signal_receiver(got_signal_cb,
            path_keyword='path', member_keyword='member',
            byte_arrays=True)

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
        data['stream'].send(msg)
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

    q.expect('tube-signal', signal='baz', args=[42])
    q.expect('tube-signal', signal='baz', args=[42])
    q.expect('tube-signal', signal='baz', args=[42])
    q.expect('tube-signal', signal='baz', args=[42])

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
    return True

    q.expect('tube-signal', signal='Disconnected')
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
