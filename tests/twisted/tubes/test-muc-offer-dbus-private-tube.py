"""Test D-Bus private tube support"""

import base64

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream

from twisted.words.xish import domish, xpath
from ns import DISCO_INFO

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

# FIXME: use ns.py
NS_TUBES = 'http://telepathy.freedesktop.org/xmpp/tubes'
NS_SI = 'http://jabber.org/protocol/si'
NS_FEATURE_NEG = 'http://jabber.org/protocol/feature-neg'
NS_IBB = 'http://jabber.org/protocol/ibb'
NS_MUC_BYTESTREAM = 'http://telepathy.freedesktop.org/xmpp/protocol/muc-bytestream'
NS_X_DATA = 'jabber:x:data'

HT_CONTACT = 1

# FIXME: stolen from jingletest.py. Should be shared by all tests
def make_presence(fromjid, tojid, caps=None):
    el = domish.Element(('jabber:client', 'presence',))
    el['from'] = fromjid
    el['to'] = tojid

    if caps:
        cel = domish.Element(('http://jabber.org/protocol/caps', 'c'))
        for key,value in caps.items():
            cel[key] = value
        el.addChild(cel)

    return el

def make_caps_disco_reply(stream, req, features):
    iq = make_result_iq(stream, req)
    query = iq.firstChildElement()

    for f in features:
        el = domish.Element((None, 'feature'))
        el['var'] = f
        query.addChild(el)

    return iq

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    self_handle = conn.GetSelfHandle()
    alice_handle = conn.RequestHandles(HT_CONTACT, ["alice@localhost"])[0]

    # send Alice's presence
    caps =  { 'ext': '', 'ver': '0.0.0',
        'node': 'http://example.com/fake-client0' }
    presence = make_presence('alice@localhost/Test', 'test@localhost', caps)
    stream.send(presence)

    q.expect('dbus-signal', signal='PresencesChanged',
        args = [{alice_handle: (2L, u'available', u'')}])

    # reply to disco query
    event = q.expect('stream-iq', to='alice@localhost/Test', query_ns=DISCO_INFO)
    stream.send(make_caps_disco_reply(stream, event.stanza, [NS_TUBES]))

    sync_stream(q, stream)

    # request tubes channe (old API)
    tubes_path = conn.RequestChannel('org.freedesktop.Telepathy.Channel.Type.Tubes',
        HT_CONTACT, alice_handle, True)
    tubes_chan = bus.get_object(conn.bus_name, tubes_path)
    tubes_iface = dbus.Interface(tubes_chan, tp_name_prefix + '.Channel.Type.Tubes')
    tubes_chan_iface = dbus.Interface(tubes_chan, tp_name_prefix + '.Channel')

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = tubes_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == alice_handle,\
            (channel_props.get('TargetHandle'), alice_handle)
    assert channel_props.get('TargetHandleType') == HT_CONTACT,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.Tubes',\
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
    assert new_tube_event.args[2] == 0       # DBUS
    assert new_tube_event.args[3] == 'com.example.TestCase'
    assert new_tube_event.args[4] == sample_parameters
    assert new_tube_event.args[5] == 1       # Remote Pending

    # handle offer_return_event
    assert offer_return_event.value[0] == dbus_tube_id

    # handle SI iq
    iq = iq_event.stanza

    tube_nodes = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]'
        % NS_TUBES, iq)
    assert len(tube_nodes) == 1
    tube = tube_nodes[0]
    tube['type'] = 'dbus'
    assert tube['initiator'] == 'test@localhost'
    assert tube['service'] == 'com.example.TestCase'
    dbus_stream_id = tube['stream-id']
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

    # handle offer_return_event
    assert dbus_tube_id == offer_return_event.value[0]

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 1
    sorted(tubes) == sorted([(
        dbus_tube_id,
        self_handle,
        0,      # DBUS
        'com.example.TestCase',
        sample_parameters,
        1,      # Remote Pending
        )])

    # Alice accepts the tube and wants to use IBB
    result = make_result_iq(stream, iq)
    result['from'] = 'alice@localhost/Test'
    si = result.firstChildElement()
    feature = si.addElement((NS_FEATURE_NEG, 'feature'))
    x = feature.addElement((NS_X_DATA, 'x'))
    x['type'] = 'submit'
    field = x.addElement((None, 'field'))
    field['var'] = 'stream-method'
    value = field.addElement((None, 'value'))
    value.addContent(NS_IBB)
    si.addElement((NS_TUBES, 'tube'))
    stream.send(result)

    # wait IBB init IQ
    event = q.expect('stream-iq', to='alice@localhost/Test',
        query_name='open', query_ns=NS_IBB)
    iq = event.stanza
    open = xpath.queryForNodes('/iq/open', iq)[0]
    assert open['sid'] == dbus_stream_id

    # open the IBB bytestream
    reply = make_result_iq(stream, iq)
    stream.send(reply)

    q.expect('dbus-signal', signal='TubeStateChanged',
        interface='org.freedesktop.Telepathy.Channel.Type.Tubes',
        args=[dbus_tube_id, 2])

    dbus_tube_adr = tubes_iface.GetDBusTubeAddress(dbus_tube_id)
    tube = Connection(dbus_tube_adr)
    signal = SignalMessage('/', 'foo.bar', 'baz')
    signal.append(42, signature='u')
    tube.send_message(signal)

    event = q.expect('stream-message', to='alice@localhost/Test')
    message = event.stanza

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

    # close the tube
    tubes_iface.CloseTube(dbus_tube_id)
    q.expect('dbus-signal', signal='TubeClosed', args=[dbus_tube_id])

    # and close the tubes channel
    tubes_chan_iface.Close()
    q.expect('dbus-signal', signal='Closed')

    # OK, we're done
    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
