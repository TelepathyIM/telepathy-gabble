"""Test 1-1 tubes support."""

import base64
import errno
import os

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

# must come before the twisted imports due to side-effects
from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match, tp_name_prefix, unwrap, Event

from twisted.internet.protocol import Factory, Protocol
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
from twisted.internet import reactor

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


@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

    set_up_echo()

    return True

@match('stream-iq', query_ns='jabber:iq:roster')
def expect_roster_iq(event, data):
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'bob@localhost'
    item['subscription'] = 'both'

    data['stream'].send(event.stanza)

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@localhost/Bob'
    presence['to'] = 'test@localhost/Resource'
    c = presence.addElement('c')
    c['xmlns'] = 'http://jabber.org/protocol/caps'
    c['node'] = 'http://example.com/ICantBelieveItsNotTelepathy'
    c['ver'] = '1.2.3'
    data['stream'].send(presence)

    return True

@match('stream-iq', iq_type='get',
    query_ns='http://jabber.org/protocol/disco#info',
    to='bob@localhost/Bob')
def expect_caps_disco(event, data):
    event.stanza['type'] = 'result'
    assert event.query['node'] == \
        'http://example.com/ICantBelieveItsNotTelepathy#1.2.3'
    feature = event.query.addElement('feature')
    feature['var'] = NS_TUBES

    data['stream'].send(event.stanza)

    call_async(data['test'], data['conn_iface'], 'RequestHandles', 1,
        ['bob@localhost'])
    return True

@match('dbus-return', method='RequestHandles')
def expect_request_handles_return(event, data):
    data['bob_handle'] = event.value[0][0]

    call_async(data['test'], data['conn_iface'], 'RequestChannel',
        tp_name_prefix + '.Channel.Type.Tubes', 1, data['bob_handle'], True)

    return True

@match('dbus-return', method='RequestChannel')
def expect_request_channel_return(event, data):
    bus = data['conn']._bus
    data['tubes_chan'] = bus.get_object(
        data['conn'].bus_name, event.value[0])
    data['tubes_iface'] = dbus.Interface(data['tubes_chan'],
        tp_name_prefix + '.Channel.Type.Tubes')

    data['self_handle'] = data['conn_iface'].GetSelfHandle()

    # Unix socket
    path = os.getcwd() + '/stream'
    call_async(data['test'], data['tubes_iface'], 'OfferStreamTube',
        'echo', sample_parameters, 0, dbus.ByteArray(path), 0, "")

    return True

@match('stream-iq', iq_type='set', to='bob@localhost/Bob')
def expect_stream_initiation_stream(event, data):

    iq = event.stanza
    si_nodes = xpath.queryForNodes('/iq/si', iq)
    if si_nodes is None:
        return False

    assert len(si_nodes) == 1
    si = si_nodes[0]
    assert si['profile'] == NS_TUBES
    data['stream_stream_id'] = si['id']

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
    assert tube['service'] == 'echo'
    # FIXME: tube['id'] has rubbish in it
    assert tube['type'] == 'stream'
    data['stream_tube_id'] = long(tube['id'])

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

    result = IQ(data['stream'], 'result')
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

    data['stream'].send(result)

    return True

@match('stream-iq', iq_type='set', to='bob@localhost/Bob')
def expect_ibb_open_stream(event, data):
    iq = event.stanza
    open = xpath.queryForNodes('/iq/open', iq)[0]
    assert open.uri == NS_IBB
    assert open['sid'] == data['stream_stream_id']

    result = IQ(data['stream'], 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result['to'] = 'test@localhost/Resource'

    data['stream'].send(result)

    return True

@match('dbus-signal', signal='TubeStateChanged')
def expect_tube_open_stream(event, data):
    assert event.args[0] == data['stream_tube_id']
    assert event.args[1] == 2       # OPEN

    call_async(data['test'], data['tubes_iface'], 'ListTubes',
        byte_arrays=True)

    return True

@match('dbus-return', method='ListTubes')
def expect_list_tubes_return1(event, data):
    assert event.value[0] == [(
        data['stream_tube_id'],
        data['self_handle'],
        1,      # Unix stream
        'echo',
        sample_parameters,
        2,      # OPEN
        )]

    # FIXME: if we use an unknown JID here, everything fails
    # (the code uses lookup where it should use ensure)

    # The CM is the server, so fake a client wanting to talk to it
    iq = IQ(data['stream'], 'set')
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

    stream = si.addElement((NS_TUBES, 'stream'))
    stream['tube'] = str(data['stream_tube_id'])

    data['stream'].send(iq)

    return True

@match('stream-iq', iq_type='result')
def expect_stream_initiation_ok_stream(event, data):
    return True

@match('dbus-signal', signal='StreamTubeNewConnection')
def expect_new_connection_stream(event, data):
    assert event.args[0] == data['stream_tube_id']
    assert event.args[1] == data['bob_handle']

    # have the fake client open the stream
    iq = IQ(data['stream'], 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    open = iq.addElement((NS_IBB, 'open'))
    open['sid'] = 'alpha'
    open['block-size'] = '4096'
    data['stream'].send(iq)
    return True

@match('stream-iq', iq_type='result')
def expect_ibb_open_ok_stream(event, data):

    # have the fake client send us some data
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    message['from'] = 'bob@localhost/Bob'
    data_node = message.addElement((NS_IBB, 'data'))
    data_node['sid'] = 'alpha'
    data_node['seq'] = '0'
    data_node.addContent(base64.b64encode('hello, world'))
    data['stream'].send(message)
    return True

@match('stream-message')
def expect_echo_stream(event, data):
    message = event.stanza

    assert message['to'] == 'bob@localhost/Bob'
    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == 'alpha'
    binary = base64.b64decode(str(ibb_data))
    assert binary == 'hello, world'

    # OK, how about D-Bus?
    call_async(data['test'], data['tubes_iface'], 'OfferDBusTube',
        'com.example.TestCase', sample_parameters)

    return True

@match('stream-iq', iq_type='set', to='bob@localhost/Bob')
def expect_stream_initiation_dbus(event, data):

    iq = event.stanza
    si_nodes = xpath.queryForNodes('/iq/si', iq)
    if si_nodes is None:
        return False

    assert len(si_nodes) == 1
    si = si_nodes[0]
    assert si['profile'] == NS_TUBES
    data['dbus_stream_id'] = si['id']

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
    assert tube['stream-id'] == data['dbus_stream_id']
    assert not tube.hasAttribute('dbus-name')
    assert tube['type'] == 'dbus'
    data['dbus_tube_id'] = long(tube['id'])

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

    result = IQ(data['stream'], 'result')
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

    data['stream'].send(result)

    return True

@match('stream-iq', iq_type='set', to='bob@localhost/Bob')
def expect_ibb_open_dbus(event, data):
    iq = event.stanza
    open = xpath.queryForNodes('/iq/open', iq)[0]
    assert open.uri == NS_IBB
    assert open['sid'] == data['dbus_stream_id']

    result = IQ(data['stream'], 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result['to'] = 'test@localhost/Resource'

    data['stream'].send(result)

    return True

@match('dbus-signal', signal='TubeStateChanged')
def expect_tube_open_dbus(event, data):
    assert event.args[0] == data['dbus_tube_id']
    assert event.args[1] == 2       # OPEN

    call_async(data['test'], data['tubes_iface'], 'ListTubes',
        byte_arrays=True)

    return True

@match('dbus-return', method='ListTubes')
def expect_list_tubes_return_dbus(event, data):
    assert sorted(event.value[0]) == sorted([(
        data['dbus_tube_id'],
        data['self_handle'],
        0,      # DBUS
        'com.example.TestCase',
        sample_parameters,
        2,      # OPEN
        ),(
        data['stream_tube_id'],
        data['self_handle'],
        1,      # stream
        'echo',
        sample_parameters,
        2,      # OPEN
        )])

    call_async(data['test'], data['tubes_iface'], 'GetDBusTubeAddress',
        data['dbus_tube_id'])

    return True

@match('dbus-return', method='GetDBusTubeAddress')
def expect_get_dbus_tube_address_return(event, data):
    data['dbus_tube_conn'] = Connection(event.value[0])
    signal = SignalMessage('/', 'foo.bar', 'baz')
    data['my_bus_name'] = ':123.whatever.you.like'
    signal.set_sender(data['my_bus_name'])
    signal.append(42, signature='u')
    data['dbus_tube_conn'].send_message(signal)
    return True

@match('stream-message')
def expect_message_dbus(event, data):
    message = event.stanza

    assert message['to'] == 'bob@localhost/Bob'

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == data['dbus_stream_id']
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
    assert data['my_bus_name'] in binary

    def got_signal_cb(*args, **kwargs):
        data['test'].handle_event(Event('tube-signal',
            path=kwargs['path'],
            signal=kwargs['member'],
            args=map(unwrap, args)))

    data['dbus_tube_conn'].add_signal_receiver(got_signal_cb,
            path_keyword='path', member_keyword='member',
            byte_arrays=True)

    dbus_message = binary
    seq = 0

    # Have the fake client send us a message all in one go...
    msg = domish.Element(('jabber:client', 'message'))
    msg['to'] = 'test@localhost/Resource'
    msg['from'] = 'bob@localhost/Bob'
    data_node = msg.addElement('data', NS_IBB)
    data_node['sid'] = data['dbus_stream_id']
    data_node['seq'] = str(seq)
    data_node.addContent(base64.b64encode(dbus_message))
    data['stream'].send(msg)
    seq += 1

    # ... and a message one byte at a time ...

    for byte in dbus_message:
        msg = domish.Element(('jabber:client', 'message'))
        msg['to'] = 'test@localhost/Resource'
        msg['from'] = 'bob@localhost/Bob'
        data_node = msg.addElement('data', NS_IBB)
        data_node['sid'] = data['dbus_stream_id']
        data_node['seq'] = str(seq)
        data_node.addContent(base64.b64encode(byte))
        data['stream'].send(msg)
        seq += 1

    # ... and two messages in one go

    msg = domish.Element(('jabber:client', 'message'))
    msg['to'] = 'test@localhost/Resource'
    msg['from'] = 'bob@localhost/Bob'
    data_node = msg.addElement('data', NS_IBB)
    data_node['sid'] = data['dbus_stream_id']
    data_node['seq'] = str(seq)
    data_node.addContent(base64.b64encode(dbus_message + dbus_message))
    data['stream'].send(msg)
    seq += 1

    return True

@match('tube-signal', signal='baz', args=[42])
def expect_tube_signal_1(event, data):
    return True

@match('tube-signal', signal='baz', args=[42])
def expect_tube_signal_2(event, data):
    return True

@match('tube-signal', signal='baz', args=[42])
def expect_tube_signal_3(event, data):
    return True

@match('tube-signal', signal='baz', args=[42])
def expect_tube_signal_4(event, data):

    # OK, we're done
    data['conn_iface'].Disconnect()
    return True

@match('tube-signal', signal='Disconnected')
def expect_tube_disconnected(event, data):
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()
