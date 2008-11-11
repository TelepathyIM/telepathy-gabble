"""Test 1-1 tubes support."""

import base64
import errno
import os

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, tp_name_prefix, \
     watch_tube_signals, sync_dbus, EventProtocolClientFactory
from gabbletest import exec_test, acknowledge_iq, sync_stream

from twisted.words.xish import domish, xpath
from twisted.internet.protocol import Factory, Protocol
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ

from gabbleconfig import HAVE_DBUS_TUBES

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

new_sample_parameters = dbus.Dictionary({
    's': 'newhello',
    'ay': dbus.ByteArray('newhello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')


def check_conn_properties(q, bus, conn, stream, channel_list=None):
    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')

    if channel_list == None:
        assert properties.get('Channels') == [], properties['Channels']
    else:
        for i in channel_list:
            assert i in properties['Channels'], \
                (i, properties['Channels'])

    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Tubes',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
             },
             ['org.freedesktop.Telepathy.Channel.TargetHandle',
              'org.freedesktop.Telepathy.Channel.TargetID',
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']
    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
             },
             ['org.freedesktop.Telepathy.Channel.TargetHandle',
              'org.freedesktop.Telepathy.Channel.TargetID',
              'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT.Parameters',
              'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service',
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

def check_channel_properties(q, bus, conn, stream, channel, channel_type,
        contact_handle, contact_id, state=None):
    # Exercise basic Channel Properties from spec 0.17.7
    # on the channel of type channel_type
    channel_props = channel.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == contact_handle,\
            (channel_props.get('TargetHandle'), contact_handle)
    assert channel_props.get('TargetHandleType') == 1,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.' + channel_type,\
            channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props, channel_props
    assert 'org.freedesktop.Telepathy.Channel.Interface.Group' not in \
            channel_props['Interfaces'], \
            channel_props['Interfaces']
    assert channel_props['TargetID'] == contact_id
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()


    if channel_type == "Tubes":
        assert state is None
        assert len(channel_props['Interfaces']) == 0, channel_props['Interfaces']
        supported_socket_types = channel.GetAvailableStreamTubeTypes()
    else:
        assert state is not None
        tube_props = channel.GetAll(
                'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT',
                dbus_interface='org.freedesktop.DBus.Properties')
        assert tube_props['Status'] == state
        # no strict check but at least check the properties exist
        assert tube_props['Parameters'] is not None
        assert channel_props['Interfaces'] == \
            dbus.Array(['org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT'],
                    signature='s'), \
            channel_props['Interfaces']

        stream_tube_props = channel.GetAll(
                'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
                dbus_interface='org.freedesktop.DBus.Properties')
        supported_socket_types = stream_tube_props['SupportedSocketTypes']

    # Support for different socket types. no strict check but at least check
    # there is some support.
    assert len(supported_socket_types) == 3

def check_NewChannel_signal(old_sig, channel_type, chan_path, contact_handle):
    assert old_sig[0] == chan_path
    assert old_sig[1] == tp_name_prefix + '.Channel.Type.' + channel_type
    assert old_sig[2] == 1         # contact handle
    assert old_sig[3] == contact_handle
    assert old_sig[4] == True      # suppress handler

def check_NewChannels_signal(new_sig, channel_type, chan_path, contact_handle,
        contact_id, initiator_handle):
    assert len(new_sig) == 1
    assert len(new_sig[0]) == 1        # one channel
    assert len(new_sig[0][0]) == 2     # two struct members
    assert new_sig[0][0][0] == chan_path
    emitted_props = new_sig[0][0][1]

    assert emitted_props[tp_name_prefix + '.Channel.ChannelType'] ==\
            tp_name_prefix + '.Channel.Type.' + channel_type
    assert emitted_props[tp_name_prefix + '.Channel.TargetHandleType'] == 1
    assert emitted_props[tp_name_prefix + '.Channel.TargetHandle'] ==\
            contact_handle
    assert emitted_props[tp_name_prefix + '.Channel.TargetID'] == \
            contact_id
    assert emitted_props[tp_name_prefix + '.Channel.Requested'] == True
    assert emitted_props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == initiator_handle
    assert emitted_props[tp_name_prefix + '.Channel.InitiatorID'] == \
            'test@localhost'


def test(q, bus, conn, stream):
    check_conn_properties(q, bus, conn, stream)

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
    item['jid'] = 'bob@localhost' # Bob can do tubes
    item['subscription'] = 'both'
    item = roster_event.query.addElement('item')
    item['jid'] = 'joe@localhost' # Joe cannot do tubes
    item['subscription'] = 'both'
    stream.send(roster)

    # Send Joe presence is without caps
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

    # Send Bob presence and his caps
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

    # Receive a tube offer from Bob
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    bob_jid = 'bob@localhost/Bob'
    message['from'] = bob_jid
    tube_node = message.addElement((NS_TUBES, 'tube'))
    tube_node['type'] = 'stream'
    tube_node['service'] = 'http'
    stream_tube_id = 49
    tube_node['id'] = str(stream_tube_id)
    stream.send(message)

    old_sig, new_sig = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    chan_path = old_sig.args[0]
    assert old_sig.args[1] == \
        'org.freedesktop.Telepathy.Channel.Type.Tubes', \
        old_sig.args[1]
    assert old_sig.args[2] == 1 # Handle_Type_Contact
    bob_handle = old_sig.args[3]
    assert old_sig.args[2] == 1, old_sig.args[2] # Suppress_Handler
    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1
    assert new_sig.args[0][0][0] == chan_path, new_sig.args[0][0]
    assert new_sig.args[0][0][1] is not None

    event = q.expect('dbus-signal', signal='NewTube')

    old_sig, new_sig = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    new_chan_path = old_sig.args[0]
    assert new_chan_path != chan_path
    assert old_sig.args[1] == \
        'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT', \
        old_sig.args[1]
    assert old_sig.args[2] == 1 # Handle_Type_Contact
    bob_handle = old_sig.args[3]
    assert old_sig.args[2] == 1, old_sig.args[2] # Suppress_Handler
    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1
    assert new_sig.args[0][0][0] == new_chan_path, new_sig.args[0][0]
    assert new_sig.args[0][0][1] is not None

    # create channel proxies
    tubes_chan = bus.get_object(conn.bus_name, chan_path)
    tubes_iface = dbus.Interface(tubes_chan,
            tp_name_prefix + '.Channel.Type.Tubes')

    # Accept the tube
    call_async(q, tubes_iface, 'AcceptStreamTube', stream_tube_id, 0, 0, '',
            byte_arrays=True)

    accept_return_event, _ = q.expect_many(
        EventPattern('dbus-return', method='AcceptStreamTube'),
        EventPattern('dbus-signal', signal='TubeStateChanged',
            args=[stream_tube_id, 2]))

    unix_socket_adr = accept_return_event.value[0]

    factory = EventProtocolClientFactory(q)
    reactor.connectUNIX(unix_socket_adr, factory)

    event = q.expect('socket-connected')
    protocol = event.protocol
    protocol.sendData("hello initiator")

    # expect SI request
    event = q.expect('stream-iq', to=bob_jid, query_ns=NS_SI,
        query_name='si')
    iq = event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % NS_SI,
        iq)[0]
    values = xpath.queryForNodes(
        '/si/feature[@xmlns="%s"]/x[@xmlns="%s"]/field/option/value'
        % ('http://jabber.org/protocol/feature-neg', 'jabber:x:data'), si)
    assert NS_IBB in [str(v) for v in values]

    stream_node = xpath.queryForNodes('/si/stream[@xmlns="%s"]' %
        NS_TUBES, si)[0]
    assert stream_node is not None
    assert stream_node['tube'] == str(stream_tube_id)
    stream_id = si['id']

    # OK, we're done
    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
