
"""
Test MUC support.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go, make_result_iq, exec_test, sync_stream
from servicetest import call_async, lazy, match, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    event = q.expect('stream-iq', to='localhost',
        query_ns='http://jabber.org/protocol/disco#items')
    result = make_result_iq(stream, event.stanza)
    item = result.firstChildElement().addElement('item')
    item['jid'] = 'conf.localhost'
    stream.send(result)

    event = q.expect('stream-iq', to='conf.localhost',
        query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    identity = result.firstChildElement().addElement('identity')
    identity['category'] = 'conference'
    identity['name'] = 'conference service'
    identity['type'] = 'text'
    stream.send(result)

    # Make sure the stream has been processed
    sync_stream(q, stream)

    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.RoomList', 0, 0, True)

    event = q.expect('dbus-return', method='RequestChannel')

    bus = dbus.SessionBus()
    chan = bus.get_object(conn.bus_name, event.value[0])

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == 0,\
            channel_props.get('TargetHandle')
    assert channel_props.get('TargetHandleType') == 0,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.RoomList',\
            channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props
    assert 'org.freedesktop.Telepathy.Channel.FUTURE' in \
            channel_props['Interfaces']

    # Exercise FUTURE properties
    future_props = chan.GetAll(
            'org.freedesktop.Telepathy.Channel.FUTURE',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert future_props['TargetID'] == ''
    assert future_props['InitiatorID'] == 'test@localhost'
    assert future_props['InitiatorHandle'] == conn.GetSelfHandle()

    # FIXME: actually list the rooms!

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

