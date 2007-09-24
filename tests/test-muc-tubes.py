"""Test IBB tube support in the context of a MUC."""

import base64
import errno
import os

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

# must come before the twisted imports due to side-effects
from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match, tp_name_prefix

from twisted.internet.protocol import Factory, Protocol
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
from twisted.internet import reactor

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

NS_TUBES = 'http://telepathy.freedesktop.org/xmpp/tubes'
NS_SI = 'http://jabber.org/protocol/si'
NS_FEATURE_NEG = 'http://jabber.org/protocol/feature-neg'
NS_IBB = 'http://jabber.org/protocol/ibb'
NS_MUC_BYTESTREAM = 'http://telepathy.freedesktop.org/xmpp/protocol/muc-bytestream'
NS_X_DATA = 'jabber:x:data'


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
    call_async(data['test'], data['conn_iface'], 'RequestHandles', 2,
        ['chat@conf.localhost'])
    return True

@match('stream-iq', to='conf.localhost',
    query_ns='http://jabber.org/protocol/disco#info')
def expect_disco(event, data):
    result = make_result_iq(data['stream'], event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    data['stream'].send(result)
    return True

@match('dbus-return', method='RequestHandles')
def expect_request_handles_return(event, data):
    handles = event.value[0]

    call_async(data['test'], data['conn_iface'], 'RequestChannel',
        tp_name_prefix + '.Channel.Type.Tubes', 2, handles[0], True)
    return True

@lazy
@match('dbus-signal', signal='MembersChanged',
    args=[u'', [], [], [], [2], 0, 0])
def expect_members_changed1(event, data):
    return True

@match('stream-presence', to='chat@conf.localhost/test')
def expect_presence(event, data):
    # Send presence for other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='MembersChanged',
    args=[u'', [3], [], [], [], 0, 0])
def expect_members_changed2(event, data):
    assert data['conn_iface'].InspectHandles(1, [3]) == [
        'chat@conf.localhost/bob']
    # a slight hack
    data['bob_handle'] = 3

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    data['stream'].send(presence)
    return True

@match('dbus-return', method='RequestChannel')
def expect_request_channel_return(event, data):
    bus = data['conn']._bus
    data['tubes_chan'] = bus.get_object(
        data['conn'].bus_name, event.value[0])
    data['tubes_iface'] = dbus.Interface(data['tubes_chan'],
        tp_name_prefix + '.Channel.Type.Tubes')

    data['tubes_self_handle'] = data['tubes_chan'].GetSelfHandle(
        dbus_interface=tp_name_prefix + '.Channel.Interface.Group')

    # Unix socket
    path = os.getcwd() + '/stream'
    call_async(data['test'], data['tubes_iface'], 'OfferStreamTube',
        'echo', sample_parameters, 0, dbus.ByteArray(path), 0, "")

    return True

@lazy
@match('dbus-signal', signal='NewTube')
def expect_new_tube_stream(event, data):
    data['stream_tube_id'] = event.args[0]
    assert event.args[1] == data['tubes_self_handle']
    assert event.args[2] == 1       # Stream
    assert event.args[3] == 'echo'
    assert event.args[4] == sample_parameters
    assert event.args[5] == 2       # OPEN

    return True

@lazy
@match('stream-presence')
def expect_tubes_in_presence1(event, data):
    presence = event.stanza

    assert presence['to'] == 'chat@conf.localhost/test'

    x_nodes = xpath.queryForNodes('/presence/x[@xmlns="http://jabber.org/'
            'protocol/muc"]', presence)
    assert x_nodes is not None
    assert len(x_nodes) == 1

    tubes_nodes = xpath.queryForNodes('/presence/tubes[@xmlns="%s"]'
        % NS_TUBES, presence)
    assert tubes_nodes is not None
    assert len(tubes_nodes) == 1

    tube_nodes = xpath.queryForNodes('/tubes/tube', tubes_nodes[0])
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    for tube in tube_nodes:
        assert tube['type'] == 'stream'
        assert tube['initiator'] == 'chat@conf.localhost/test'
        assert tube['service'] == 'echo'
        assert not tube.hasAttribute('stream-id')
        assert not tube.hasAttribute('dbus-name')
        #assert tube['id'] == str(data['stream_tube_id'])

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

    return True

@lazy
@match('dbus-return', method='OfferStreamTube')
def expect_offer_stream_tube_return(event, data):
    call_async(data['test'], data['tubes_iface'], 'ListTubes',
        byte_arrays=True)
    return True

@match('dbus-return', method='ListTubes')
def expect_list_tubes_return1(event, data):
    assert event.value[0] == [(
        data['stream_tube_id'],
        data['tubes_self_handle'],
        1,      # Stream
        'echo',
        sample_parameters,
        2,      # OPEN
        )]

    # FIXME: if we use an unknown JID here, everything fails
    # (the code uses lookup where it should use ensure)

    # The CM is the server, so fake a client wanting to talk to it
    iq = IQ(data['stream'], 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'chat@conf.localhost/bob'
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

    tube = si.addElement((NS_TUBES, 'tube'))
    tube['id'] = str(data['stream_tube_id'])
    tube['offering'] = 'false'

    data['stream'].send(iq)

    return True

@lazy
@match('stream-iq', iq_type='result')
def expect_stream_initiation_ok(event, data):
    return True

@match('dbus-signal', signal='StreamTubeNewConnection')
def expect_stream_new_connection(event, data):
    assert event.args[0] == data['stream_tube_id']
    assert event.args[1] == data['bob_handle']

    # have the fake client open the stream
    iq = IQ(data['stream'], 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'chat@conf.localhost/bob'
    open = iq.addElement((NS_IBB, 'open'))
    open['sid'] = 'alpha'
    open['block-size'] = '4096'
    data['stream'].send(iq)
    return True

@match('stream-iq', iq_type='result')
def expect_stream_ibb_open_ok(event, data):

    # have the fake client send us some data
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    message['from'] = 'chat@conf.localhost/bob'
    data_node = message.addElement((NS_IBB, 'data'))
    data_node['sid'] = 'alpha'
    data_node['seq'] = '0'
    data_node.addContent(base64.b64encode('hello, world'))
    data['stream'].send(message)
    return True

@match('stream-message')
def expect_stream_echo(event, data):
    message = event.stanza

    assert message['to'] == 'chat@conf.localhost/bob'
    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == 'alpha'
    binary = base64.b64decode(str(ibb_data))
    assert binary == 'hello, world'

    # OK, enough streams. Now try D-Bus
    call_async(data['test'], data['tubes_iface'], 'OfferDBusTube',
        'com.example.TestCase', sample_parameters)
    return True

@lazy
@match('dbus-signal', signal='NewTube')
def expect_new_tube_dbus(event, data):
    data['dbus_tube_id'] = event.args[0]
    assert event.args[1] == data['tubes_self_handle']
    assert event.args[2] == 0       # DBUS
    assert event.args[3] == 'com.example.TestCase'
    assert event.args[4] == sample_parameters
    assert event.args[5] == 2       # OPEN

    return True


@lazy
@match('stream-presence')
def expect_tubes_in_presence2(event, data):
    presence = event.stanza

    assert presence['to'] == 'chat@conf.localhost/test'

    x_nodes = xpath.queryForNodes('/presence/x[@xmlns="http://jabber.org/'
            'protocol/muc"]', presence)
    assert x_nodes is not None
    assert len(x_nodes) == 1

    tubes_nodes = xpath.queryForNodes('/presence/tubes[@xmlns="%s"]'
        % NS_TUBES, presence)
    assert tubes_nodes is not None
    assert len(tubes_nodes) == 1

    tube_nodes = xpath.queryForNodes('/tubes/tube', tubes_nodes[0])
    assert tube_nodes is not None
    assert len(tube_nodes) == 2
    for tube in tube_nodes:
        assert tube['type'] in ('dbus', 'stream')

        if tube['type'] == 'dbus':
            assert tube['initiator'] == 'chat@conf.localhost/test'
            assert tube['service'] == 'com.example.TestCase'
            data['dbus_stream_id'] = tube['stream-id']
            #assert tube['dbus-name'] == data['my_bus_name']
            #assert tube['id'] == str(data['dbus_tube_id'])

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

    return True

@lazy
@match('dbus-return', method='OfferDBusTube')
def expect_offer_dbus_tube_return(event, data):
    call_async(data['test'], data['tubes_iface'], 'ListTubes',
        byte_arrays=True)
    return True


@lazy
@match('dbus-signal', signal='DBusNamesChanged')
def expect_dbus_names_changed(event, data):
    assert event.args[0] == data['dbus_tube_id']
    assert event.args[1][0][0] == data['tubes_self_handle']
    data['my_bus_name'] = event.args[1][0][1]

    return True

@match('dbus-return', method='ListTubes')
def expect_list_tubes_return2(event, data):
    assert sorted(event.value[0]) == sorted([(
        data['dbus_tube_id'],
        data['tubes_self_handle'],
        0,      # DBUS
        'com.example.TestCase',
        sample_parameters,
        2,      # OPEN
        ),(
        data['stream_tube_id'],
        data['tubes_self_handle'],
        1,      # stream
        'echo',
        sample_parameters,
        2,      # OPEN
        )])

    call_async(data['test'], data['tubes_iface'], 'GetDBusServerAddress',
        data['dbus_tube_id'])

    return True

@match('dbus-return', method='GetDBusServerAddress')
def expect_get_dbus_server_address_return(event, data):
    data['tube'] = Connection(event.value[0])
    signal = SignalMessage('/', 'foo.bar', 'baz')
    signal.append(42, signature='u')
    data['tube'].send_message(signal)
    return True

@match('stream-message')
def expect_message_dbus(event, data):
    message = event.stanza

    assert message['to'] == 'chat@conf.localhost'
    assert message['type'] == 'groupchat'

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_MUC_BYTESTREAM,
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

    # OK, we're done

    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()
