"""Test D-Bus private tube support"""

import base64

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream
from constants import *
from tubetestutil import *

from twisted.words.xish import domish, xpath
from ns import DISCO_INFO, TUBES, SI, FEATURE_NEG, IBB, MUC_BYTESTREAM, X_DATA

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

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

def alice_accepts_tube(q, stream, iq_event, dbus_tube_id):
    iq = iq_event.stanza

    tube_nodes = xpath.queryForNodes('/iq/si/tube[@xmlns="%s"]'
        % TUBES, iq)
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

    # Alice accepts the tube and wants to use IBB
    result = make_result_iq(stream, iq)
    result['from'] = 'alice@localhost/Test'
    si = result.firstChildElement()
    feature = si.addElement((FEATURE_NEG, 'feature'))
    x = feature.addElement((X_DATA, 'x'))
    x['type'] = 'submit'
    field = x.addElement((None, 'field'))
    field['var'] = 'stream-method'
    value = field.addElement((None, 'value'))
    value.addContent(IBB)
    si.addElement((TUBES, 'tube'))
    stream.send(result)

    # wait IBB init IQ
    event = q.expect('stream-iq', to='alice@localhost/Test',
        query_name='open', query_ns=IBB)
    iq = event.stanza
    open = xpath.queryForNodes('/iq/open', iq)[0]
    assert open['sid'] == dbus_stream_id

    # open the IBB bytestream
    reply = make_result_iq(stream, iq)
    stream.send(reply)

    return dbus_stream_id

def send_dbus_message_to_alice(q, stream, dbus_tube_adr, dbus_stream_id):
    tube = Connection(dbus_tube_adr)
    signal = SignalMessage('/', 'foo.bar', 'baz')
    signal.append(42, signature='u')
    tube.send_message(signal)

    event = q.expect('stream-message', to='alice@localhost/Test')
    message = event.stanza

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % IBB,
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

def offer_old_dbus_tube(q, bus, conn, stream, self_handle, alice_handle):
    # request tubes channel (old API)
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
    assert new_tube_event.args[5] == TUBE_STATE_REMOTE_PENDING

    # handle offer_return_event
    assert offer_return_event.value[0] == dbus_tube_id

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 1
    expected_tube = (dbus_tube_id, self_handle, TUBE_TYPE_DBUS,
        'com.example.TestCase', sample_parameters, TUBE_STATE_REMOTE_PENDING)
    check_tube_in_tubes(expected_tube, tubes)

    dbus_stream_id = alice_accepts_tube(q, stream, iq_event, dbus_tube_id)

    q.expect('dbus-signal', signal='TubeStateChanged',
        interface='org.freedesktop.Telepathy.Channel.Type.Tubes',
        args=[dbus_tube_id, 2])

    dbus_tube_adr = tubes_iface.GetDBusTubeAddress(dbus_tube_id)
    send_dbus_message_to_alice(q, stream, dbus_tube_adr, dbus_stream_id)

    # close the tube
    tubes_iface.CloseTube(dbus_tube_id)
    q.expect('dbus-signal', signal='TubeClosed', args=[dbus_tube_id])

    # and close the tubes channel
    tubes_chan_iface.Close()
    q.expect('dbus-signal', signal='Closed')


def offer_new_dbus_tube(q, bus, conn, stream, self_handle, alice_handle):
    requestotron = dbus.Interface(conn,
        'org.freedesktop.Telepathy.Connection.Interface.Requests')

    # Can we request a DBusTube channel?
    properties = conn.GetAll(
        'org.freedesktop.Telepathy.Connection.Interface.Requests',
        dbus_interface='org.freedesktop.DBus.Properties')

    # check if we can request 1-1 DBus tube
    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
            'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT',
         'org.freedesktop.Telepathy.Channel.TargetHandleType': HT_CONTACT,
         },
         ['org.freedesktop.Telepathy.Channel.TargetHandle',
          'org.freedesktop.Telepathy.Channel.TargetID',
          'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT.Parameters',
          'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName',
         ]
        ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    # Offer a tube to Alice (new API)

    call_async(q, requestotron, 'CreateChannel',
        {'org.freedesktop.Telepathy.Channel.ChannelType':
            'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT',
         'org.freedesktop.Telepathy.Channel.TargetHandleType':
            HT_CONTACT,
         'org.freedesktop.Telepathy.Channel.TargetID':
            'alice@localhost',
         'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT.Parameters':
            sample_parameters,
         'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName':
            'com.example.TestCase'
         }, byte_arrays=True)
    cc_ret, nc = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    tube_path, tube_props = cc_ret.value
    new_channel_details = nc.args[0]

    # check tube channel properties
    assert tube_props[tp_name_prefix + '.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT'
    assert tube_props[tp_name_prefix + '.Channel.Interfaces'] ==\
            ['org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT']
    assert tube_props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_CONTACT
    assert tube_props[tp_name_prefix + '.Channel.TargetHandle'] == alice_handle
    assert tube_props[tp_name_prefix + '.Channel.TargetID'] == 'alice@localhost'
    assert tube_props[tp_name_prefix + '.Channel.Requested'] == True
    assert tube_props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == self_handle
    assert tube_props[tp_name_prefix + '.Channel.InitiatorID'] \
            == "test@localhost"
    assert tube_props[tp_name_prefix + '.Channel.Interface.Tube.DRAFT.Parameters'] == sample_parameters
    assert tube_props[tp_name_prefix + '.Channel.Interface.Tube.DRAFT.Status'] == TUBE_CHANNEL_STATE_NOT_OFFERED

    # Under the current implementation, creating a new-style Tube channel
    # ensures that an old-style Tubes channel exists, even though Tube channels
    # aren't visible on the Tubes channel until they're offered.  Another
    # correct implementation would have the Tubes channel spring up only when
    # the Tube is offered.
    #
    # Anyway. Given the current implementation, they should be announced together.
    assert len(new_channel_details) == 2, unwrap(new_channel_details)
    found_tubes = False
    found_tube = False
    for path, details in new_channel_details:
        if details[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            found_tubes = True
            tubes_chan = bus.get_object(conn.bus_name, path)
            tubes_iface = dbus.Interface(tubes_chan, CHANNEL_TYPE_TUBES)
        elif details[CHANNEL_TYPE] == CHANNEL_TYPE_DBUS_TUBE:
            found_tube = True
            assert tube_path == path, (tube_path, path)
        else:
            assert False, (path, details)
    assert found_tube and found_tubes, unwrap(new_channel_details)

    # The tube's not offered, so it shouldn't be shown on the old interface.
    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 0, tubes

    tube_chan = bus.get_object(conn.bus_name, tube_path)
    tube_chan_iface = dbus.Interface(tube_chan, tp_name_prefix + '.Channel')
    dbus_tube_iface = dbus.Interface(tube_chan, CHANNEL_TYPE_DBUS_TUBE)

    # Only when we offer the tube should it appear on the Tubes channel and an
    # IQ be sent to Alice. We sync the stream to ensure the IQ would have
    # arrived if it had been sent.
    sync_stream(q, stream)
    call_async(q, dbus_tube_iface, 'OfferDBusTube')
    offer_return_event, iq_event, new_tube_event = q.expect_many(
        EventPattern('dbus-return', method='OfferDBusTube'),
        EventPattern('stream-iq', to='alice@localhost/Test'),
        EventPattern('dbus-signal', signal='NewTube'),
        )

    tube_address = offer_return_event.value[0]
    assert len(tube_address) > 0

    # Now the tube's been offered, it should be shown on the old interface
    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert len(tubes) == 1
    expected_tube = (None, self_handle, TUBE_TYPE_DBUS, 'com.example.TestCase',
        sample_parameters, TUBE_STATE_REMOTE_PENDING)
    check_tube_in_tubes(expected_tube, tubes)

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

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
    stream.send(make_caps_disco_reply(stream, event.stanza, [TUBES]))

    sync_stream(q, stream)

    offer_old_dbus_tube(q, bus, conn, stream, self_handle, alice_handle)
    offer_new_dbus_tube(q, bus, conn, stream, self_handle, alice_handle)

    # OK, we're done
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
