"""Test IBB stream tube support in the context of a MUC."""

import base64
import dbus

from servicetest import call_async, EventPattern, tp_name_prefix, EventProtocolClientFactory
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ

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
    room_handle = handles[0]

    # join the muc
    call_async(q, conn, 'RequestChannel',
        tp_name_prefix + '.Channel.Type.Text', 2, room_handle, True)

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
    text_chan = bus.get_object(conn.bus_name, event.value[0])

    # Bob offers a muc tube
    tube_id = 666
    stream_id = 1234
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    tubes = presence.addElement((NS_TUBES, 'tubes'))
    tube = tubes.addElement((None, 'tube'))
    tube['type'] = 'dbus'
    tube['service'] = 'org.telepathy.freedesktop.test'
    tube['id'] = str(tube_id)
    tube['stream-id'] = str(stream_id)
    tube['dbus-name'] = ':2.Y2Fzc2lkeS10ZXN0MgAA'
    tube['initiator'] = 'chat@conf.localhost/bob'
    parameters = tube.addElement((None, 'parameters'))
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 's'
    parameter['type'] = 'str'
    parameter.addContent('hello')
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 'ay'
    parameter['type'] = 'bytes'
    parameter.addContent('aGVsbG8=')
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 'u'
    parameter['type'] = 'uint'
    parameter.addContent('123')
    parameter = parameters.addElement((None, 'parameter'))
    parameter['name'] = 'i'
    parameter['type'] = 'int'
    parameter.addContent('-123')

    stream.send(presence)

    # tubes channel is automatically created
    event = q.expect('dbus-signal', signal='NewChannel')

    if event.args[1] == 'org.freedesktop.Telepathy.Channel.Type.Text':
        # skip this one, try the next one
        event = q.expect('dbus-signal', signal='NewChannel')

    assert event.args[1] == 'org.freedesktop.Telepathy.Channel.Type.Tubes',\
        event.args
    assert event.args[2] == 2 # Handle_Type_Room
    assert event.args[3] == room_handle

    tubes_chan = bus.get_object(conn.bus_name, event.args[0])
    tubes_iface = dbus.Interface(tubes_chan, event.args[1])

    channel_props = tubes_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props['TargetID'] == 'chat@conf.localhost', channel_props
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == ''
    assert channel_props['InitiatorHandle'] == 0

    tubes_self_handle = tubes_chan.GetSelfHandle(
        dbus_interface=tp_name_prefix + '.Channel.Interface.Group')

    q.expect('dbus-signal', signal='NewTube',
        args=[tube_id, bob_handle, 0, 'org.telepathy.freedesktop.test', sample_parameters, 0])

    tubes = tubes_iface.ListTubes(byte_arrays=True)
    assert tubes == [(
        tube_id,
        bob_handle,
        0,      # D-Bus
        'org.telepathy.freedesktop.test',
        sample_parameters,
        0,      # local pending
        )]

    # reject the tube
    tubes_iface.CloseTube(tube_id)
    q.expect('dbus-signal', signal='TubeClosed', args=[tube_id])

    # close the text channel
    text_chan.Close()

    # OK, we're done
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
