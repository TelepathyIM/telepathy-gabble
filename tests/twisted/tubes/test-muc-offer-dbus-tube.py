"""Test IBB tube support in the context of a MUC."""

import base64

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath

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

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    call_async(q, conn, 'RequestHandles', 2,
        ['chat@conf.localhost'])

    event = q.expect('stream-iq', to='conf.localhost',
            query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]

    # request tubes channel
    call_async(q, conn, 'RequestChannel',
        tp_name_prefix + '.Channel.Type.Tubes', 2, handles[0], True)

    _, stream_event = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))

    # Send presence for other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

    q.expect('dbus-signal', signal='MembersChanged',
            args=[u'', [2, 3], [], [], [], 0, 0])

    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/test']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']
    bob_handle = 3

    event = q.expect('dbus-return', method='RequestChannel')

    tubes_chan = bus.get_object(conn.bus_name, event.value[0])
    tubes_iface = dbus.Interface(tubes_chan,
            tp_name_prefix + '.Channel.Type.Tubes')

    tubes_self_handle = tubes_chan.GetSelfHandle(
        dbus_interface=tp_name_prefix + '.Channel.Interface.Group')

    # Offer a D-Bus tube
    call_async(q, tubes_iface, 'OfferDBusTube',
            'com.example.TestCase', sample_parameters)

    new_tube_event, presence_event, offer_return_event, dbus_changed_event = \
        q.expect_many(
        EventPattern('dbus-signal', signal='NewTube'),
        EventPattern('stream-presence', to='chat@conf.localhost/test'),
        EventPattern('dbus-return', method='OfferDBusTube'),
        EventPattern('dbus-signal', signal='DBusNamesChanged'))

    # handle new_tube_event
    dbus_tube_id = new_tube_event.args[0]
    assert new_tube_event.args[1] == tubes_self_handle
    assert new_tube_event.args[2] == 0       # DBUS
    assert new_tube_event.args[3] == 'com.example.TestCase'
    assert new_tube_event.args[4] == sample_parameters
    assert new_tube_event.args[5] == 2       # OPEN

    # handle offer_return_event
    assert offer_return_event.value[0] == dbus_tube_id

    # handle presence_event
    # We announce our newly created tube in our muc presence
    presence = presence_event.stanza
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
        tube['type'] = 'dbus'
        assert tube['initiator'] == 'chat@conf.localhost/test'
        assert tube['service'] == 'com.example.TestCase'
        dbus_stream_id = tube['stream-id']
        my_bus_name = tube['dbus-name']
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

    # handle dbus_changed_event
    assert dbus_changed_event.args[0] == dbus_tube_id
    assert dbus_changed_event.args[1][0][0] == tubes_self_handle
    assert dbus_changed_event.args[1][0][1] == my_bus_name

    # handle offer_return_event
    assert dbus_tube_id == offer_return_event.value[0]

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 1
    sorted(tubes) == sorted([(
        dbus_tube_id,
        tubes_self_handle,
        0,      # DBUS
        'com.example.TestCase',
        sample_parameters,
        2,      # OPEN
        )])

    dbus_tube_adr = tubes_iface.GetDBusTubeAddress(dbus_tube_id)
    tube = Connection(dbus_tube_adr)
    signal = SignalMessage('/', 'foo.bar', 'baz')
    signal.append(42, signature='u')
    tube.send_message(signal)

    event = q.expect('stream-message', to='chat@conf.localhost',
        message_type='groupchat')
    message = event.stanza

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_MUC_BYTESTREAM,
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

    # OK, we're done
    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
