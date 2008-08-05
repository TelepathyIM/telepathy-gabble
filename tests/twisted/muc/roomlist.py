
"""
Test MUC support.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go, make_result_iq, exec_test, sync_stream
from servicetest import call_async, lazy, match, EventPattern, \
        tp_name_prefix, tp_path_prefix

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

    properties = conn.GetAll(
            tp_name_prefix + '.Connection.Interface.Requests.DRAFT',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties.get('Channels') == [], properties['Channels']
    assert ({tp_name_prefix + '.Channel.ChannelType':
                tp_name_prefix + '.Channel.Type.RoomList',
             tp_name_prefix + '.Channel.TargetHandleType': 0,
             },
             [],
             [tp_name_prefix + '.Channel.Type.RoomList.Server'],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    call_async(q, conn, 'RequestChannel',
        tp_name_prefix + '.Channel.Type.RoomList', 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    bus = dbus.SessionBus()
    path1 = ret.value[0]
    chan = bus.get_object(conn.bus_name, path1)

    assert new_sig.args[0][0][0] == path1

    props = new_sig.args[0][0][1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            tp_name_prefix + '.Channel.Type.RoomList'
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == 0
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == 0
    assert props[tp_name_prefix + '.Channel.FUTURE.Requested'] == True
    assert props[tp_name_prefix + '.Channel.FUTURE.TargetID'] == ''
    assert props[tp_name_prefix + '.Channel.FUTURE.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.FUTURE.InitiatorID'] \
            == 'test@localhost'
    assert props[tp_name_prefix + '.Channel.Type.RoomList.Server'] == \
            'conf.localhost'

    assert old_sig.args[0] == path1
    assert old_sig.args[1] == tp_name_prefix + '.Channel.Type.RoomList'
    assert old_sig.args[2] == 0     # handle type
    assert old_sig.args[3] == 0     # handle
    assert old_sig.args[4] == 1     # suppress handler

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.GetAll(
            tp_name_prefix + '.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == 0,\
            channel_props.get('TargetHandle')
    assert channel_props.get('TargetHandleType') == 0,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            tp_name_prefix + '.Channel.Type.RoomList',\
            channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props
    assert tp_name_prefix + '.Channel.FUTURE' in \
            channel_props['Interfaces']

    # Exercise FUTURE properties
    future_props = chan.GetAll(
            tp_name_prefix + '.Channel.FUTURE',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert future_props['Requested'] == True
    assert future_props['TargetID'] == ''
    assert future_props['InitiatorID'] == 'test@localhost'
    assert future_props['InitiatorHandle'] == conn.GetSelfHandle()

    assert chan.Get(
            tp_name_prefix + '.Channel.Type.RoomList', 'Server',
            dbus_interface='org.freedesktop.DBus.Properties') == \
                    'conf.localhost'

    # FIXME: actually list the rooms!

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests.DRAFT')
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                tp_name_prefix + '.Channel.Type.RoomList',
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              tp_name_prefix + '.Channel.Type.RoomList.Server':
                'conference.example.net',
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = bus.get_object(conn.bus_name, path2)

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            tp_name_prefix + '.Channel.Type.RoomList'
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == 0
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == 0
    assert props[tp_name_prefix + '.Channel.FUTURE.Requested'] == True
    assert props[tp_name_prefix + '.Channel.FUTURE.TargetID'] == ''
    assert props[tp_name_prefix + '.Channel.FUTURE.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.FUTURE.InitiatorID'] \
            == 'test@localhost'
    assert props[tp_name_prefix + '.Channel.Type.RoomList.Server'] == \
            'conference.example.net'

    assert new_sig.args[0][0][0] == path2
    assert new_sig.args[0][0][1] == props

    assert old_sig.args[0] == path2
    assert old_sig.args[1] == tp_name_prefix + '.Channel.Type.RoomList'
    assert old_sig.args[2] == 0     # handle type
    assert old_sig.args[3] == 0     # handle
    assert old_sig.args[4] == 1     # suppress handler

    assert chan.Get(
            tp_name_prefix + '.Channel.Type.RoomList', 'Server',
            dbus_interface='org.freedesktop.DBus.Properties') == \
                    'conference.example.net'

    # FIXME: actually list the rooms!

    conn.Disconnect()

    q.expect_many(
            EventPattern('dbus-signal', signal='Closed',
                path=path1[len(tp_path_prefix):]),
            EventPattern('dbus-signal', signal='Closed',
                path=path2[len(tp_path_prefix):]),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path1]),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path2]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            )

if __name__ == '__main__':
    exec_test(test)

