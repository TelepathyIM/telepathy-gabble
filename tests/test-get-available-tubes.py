"""Test GetAvailableStreamTubeTypes and GetAvailableTubeTypes"""

import base64
import errno
import os

import dbus

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish

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

    q.expect('dbus-signal', signal='MembersChanged',
            args=[u'', [3], [], [], [], 0, 0])

    conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']
    bob_handle = 3

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

    event = q.expect('dbus-return', method='RequestChannel')

    tubes_chan = bus.get_object(conn.bus_name, event.value[0])
    tubes_iface_muc = dbus.Interface(tubes_chan,
            tp_name_prefix + '.Channel.Type.Tubes')

    # FIXME: these using a "1-1" tubes channel too

    # test GetAvailableTubeTypes
    tube_types = tubes_iface_muc.GetAvailableTubeTypes()
    assert len(tube_types) == 2
    assert 0 in tube_types # D-Bus tube
    assert 1 in tube_types # Stream tube

    # test GetAvailableStreamTubeTypes
    stream_tubes_types = tubes_iface_muc.GetAvailableStreamTubeTypes()
    assert len(stream_tubes_types) == 3
    # Unix sockets supports Socket_Access_Control_Localhost
    assert stream_tubes_types[0] == [0]
    # Ipv4 sockets supports Socket_Access_Control_Localhost
    assert stream_tubes_types[2] == [0]
    # Ipv6 sockets supports Socket_Access_Control_Localhost
    assert stream_tubes_types[3] == [0]

    # OK, we're done
    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
