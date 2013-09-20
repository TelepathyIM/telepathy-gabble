
"""
Test MUC support.
"""

import dbus

from gabbletest import make_result_iq, exec_test, sync_stream, disconnect_conn
from servicetest import call_async, EventPattern, tp_name_prefix
import constants as cs

def test(q, bus, conn, stream):
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
            tp_name_prefix + '.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert properties.get('Channels') == [], properties['Channels']
    assert ({tp_name_prefix + '.Channel.ChannelType': cs.CHANNEL_TYPE_ROOM_LIST,
             tp_name_prefix + '.Channel.TargetHandleType': 0,
             },
             [cs.CHANNEL_TYPE_ROOM_LIST + '.Server'],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # FIXME: actually list the rooms!

    call_async(q, conn.Requests, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType': cs.CHANNEL_TYPE_ROOM_LIST,
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              cs.CHANNEL_TYPE_ROOM_LIST + '.Server':
                'conference.example.net',
              })

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = bus.get_object(conn.bus_name, path2)

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            cs.CHANNEL_TYPE_ROOM_LIST
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == 0
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == 0
    assert props[tp_name_prefix + '.Channel.TargetID'] == ''
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.Properties.Get(cs.CONN, "SelfHandle")
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == 'test@localhost'
    assert props[cs.CHANNEL_TYPE_ROOM_LIST+ '.Server'] == \
            'conference.example.net'

    assert sig.args[0][0][0] == path2
    assert sig.args[0][0][1] == props

    assert chan.Get(cs.CHANNEL_TYPE_ROOM_LIST, 'Server',
            dbus_interface=dbus.PROPERTIES_IFACE) == \
                    'conference.example.net'

    # FIXME: actually list the rooms!

    call_async(q, conn.Requests, 'EnsureChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_ROOM_LIST,
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              cs.CHANNEL_TYPE_ROOM_LIST + '.Server':
                'conference.example.net',
              })

    ret = q.expect('dbus-return', method='EnsureChannel')
    yours, ensured_path, ensured_props = ret.value

    assert not yours
    assert ensured_path == path2, (ensured_path, path2)

    disconnect_conn(q, conn, stream, [
    EventPattern('dbus-signal', signal='Closed', path=path2),
    EventPattern('dbus-signal', signal='ChannelClosed', args=[path2])])

if __name__ == '__main__':
    exec_test(test)

