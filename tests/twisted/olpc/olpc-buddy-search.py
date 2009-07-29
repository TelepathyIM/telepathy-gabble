"""
test OLPC search buddy
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import xpath
from twisted.words.protocols.jabber.client import IQ

from util import (announce_gadget, properties_to_xml, parse_properties,
    create_gadget_message, close_view)
import ns
import constants as cs

tp_name_prefix = 'org.freedesktop.Telepathy'
olpc_name_prefix = 'org.laptop.Telepathy'

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')
    requests_iface = dbus.Interface(conn, tp_name_prefix + '.Connection.Interface.Requests')

    # Gadget was not announced yet
    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.BuddyView',
            'org.laptop.Telepathy.Channel.Interface.View.MaxSize': 5,
          })

    event = q.expect('dbus-error', method='CreateChannel')
    announce_gadget(q, stream, disco_event.stanza)
    call_async(q, conn, 'RequestHandles', 1, ['bob@localhost'])

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]
    bob_handle = handles[0]

    call_async(q, buddy_info_iface, 'GetProperties', bob_handle)

    # wait for pubsub query
    event = q.expect('stream-iq', to='bob@localhost', query_ns=ns.PUBSUB)
    query = event.stanza
    assert query['to'] == 'bob@localhost'

    # send an error as reply
    reply = IQ(stream, 'error')
    reply['id'] = query['id']
    reply['to'] = 'alice@localhost'
    reply['from'] = 'bob@localhost'
    stream.send(reply)

    # wait for buddy search query
    event = q.expect('stream-iq', to='gadget.localhost',
            query_ns=ns.OLPC_BUDDY)
    buddies = xpath.queryForNodes('/iq/query/buddy', event.stanza)
    assert len(buddies) == 1
    buddy = buddies[0]
    assert buddy['jid'] == 'bob@localhost'

    # send reply to the search query
    reply = make_result_iq(stream, event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    query = xpath.queryForNodes('/iq/query', reply)[0]
    buddy = query.addElement((None, "buddy"))
    buddy['jid'] = 'bob@localhost'
    properties = buddy.addElement((ns.OLPC_BUDDY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#005FE4,#00A0FF')}):
        properties.addChild(node)
    stream.send(reply)

    event = q.expect('dbus-return', method='GetProperties')
    props = event.value[0]

    assert props == {'color': '#005FE4,#00A0FF' }

    # check if we can request Buddy views
    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)

    assert ({tp_name_prefix + '.Channel.ChannelType':
            olpc_name_prefix + '.Channel.Type.BuddyView'},
            [olpc_name_prefix + '.Channel.Interface.View.MaxSize',
             olpc_name_prefix + '.Channel.Type.BuddyView.Properties',
             olpc_name_prefix + '.Channel.Type.BuddyView.Alias'],
         ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    # request 3 random buddies
    call_async(q, requests_iface, 'CreateChannel',
        { tp_name_prefix + '.Channel.ChannelType':
            olpc_name_prefix + '.Channel.Type.BuddyView',
          olpc_name_prefix + '.Channel.Interface.View.MaxSize': 3
          })

    iq_event, return_event, new_channels_event, new_channel_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=ns.OLPC_BUDDY),
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='NewChannel'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '1'
    assert view['size'] == '3'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'charles@localhost'
    properties = buddy.addElement((ns.OLPC_BUDDY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#AAAAAA,#BBBBBB')}):
        properties.addChild(node)
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'bob@localhost'
    properties = buddy.addElement((ns.OLPC_BUDDY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#005FE4,#00A0FF')}):
        properties.addChild(node)
    stream.send(reply)

    view_path = return_event.value[0]
    props = return_event.value[1]
    view1 = bus.get_object(conn.bus_name, view_path)

    # check NewChannels arg
    channels = new_channels_event.args[0]
    assert len(channels) == 1
    chan, props_ = channels[0]
    assert chan == view_path
    assert props == props_

    # check NewChannel arg
    chan = new_channel_event.args[0]
    assert chan == view_path

    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Properties'] == {}
    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Alias'] == ''

    # check org.freedesktop.Telepathy.Channel D-Bus properties
    props = view1.GetAll(cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['ChannelType'] == 'org.laptop.Telepathy.Channel.Type.BuddyView'
    assert 'org.laptop.Telepathy.Channel.Interface.View' in props['Interfaces']
    assert props['TargetHandle'] == 0
    assert props['TargetID'] == ''
    assert props['TargetHandleType'] == 0

    # check org.laptop.Telepathy.Channel.Interface.View D-Bus properties
    props = view1.GetAll(
        'org.laptop.Telepathy.Channel.Interface.View',
        dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['MaxSize'] == 3

    # check org.laptop.Telepathy.Channel.Type.BuddyView D-Bus properties
    props = view1.GetAll(
        'org.laptop.Telepathy.Channel.Type.BuddyView',
        dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['Properties'] == {}
    assert props['Alias'] == ''

    assert view1.GetChannelType(dbus_interface=cs.CHANNEL) ==\
            'org.laptop.Telepathy.Channel.Type.BuddyView'

    event = q.expect('dbus-signal', signal='BuddiesChanged')
    added, removed = event.args
    assert removed == []
    assert len(added) == 2
    assert sorted(conn.InspectHandles(1, added)) == ['bob@localhost',
            'charles@localhost']

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    event = q.expect('dbus-signal', signal='PropertiesChanged')

    # we can now get bob's properties
    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]
    props = buddy_info_iface.GetProperties(bob_handle)
    assert props == {'color': '#005FE4,#00A0FF'}

    # Bob changed his properties
    message = create_gadget_message("test@localhost")

    change = message.addElement((ns.OLPC_BUDDY, 'change'))
    change['jid'] = 'bob@localhost'
    change['id'] = '1'
    properties = change.addElement((ns.OLPC_BUDDY_PROPS, 'properties'))
    for node in properties_to_xml({'color': ('str', '#FFFFFF,#AAAAAA')}):
        properties.addChild(node)

    stream.send(message)

    event = q.expect('dbus-signal', signal='PropertiesChanged',
            args=[bob_handle, {'color': '#FFFFFF,#AAAAAA'}])

    # we now get the new properties
    props = buddy_info_iface.GetProperties(bob_handle)
    assert props == {'color': '#FFFFFF,#AAAAAA'}

    # buddy search
    props = dbus.Dictionary({'color': '#AABBCC,#001122'}, signature='sv')
    call_async(q, requests_iface, 'CreateChannel',
        { tp_name_prefix + '.Channel.ChannelType':
            olpc_name_prefix + '.Channel.Type.BuddyView',
          olpc_name_prefix + '.Channel.Interface.View.MaxSize': 10,
          olpc_name_prefix + '.Channel.Type.BuddyView.Properties': props
          })

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=ns.OLPC_BUDDY),
        EventPattern('dbus-return', method='CreateChannel'))

    properties_node = xpath.queryForNodes('/iq/view/buddy/properties',
            iq_event.stanza)
    props = parse_properties(properties_node[0])
    assert props == {'color': ('str', '#AABBCC,#001122')}

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '2'
    assert view['size'] == '10'

    # reply to request
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'charles@localhost'
    properties = buddy.addElement((ns.OLPC_BUDDY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#AABBCC,#001122')}):
        properties.addChild(node)
    stream.send(reply)

    view_path = return_event.value[0]
    props = return_event.value[1]
    view2 = bus.get_object(conn.bus_name, view_path)

    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Properties'] == dbus.Dictionary({'color': '#AABBCC,#001122'}, signature='sv')
    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Alias'] == ''

    # check org.laptop.Telepathy.Channel.Type.BuddyView D-Bus properties
    props = view2.GetAll(
        'org.laptop.Telepathy.Channel.Type.BuddyView',
        dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['Properties'] == {'color': '#AABBCC,#001122'}
    assert props['Alias'] == ''

    event = q.expect('dbus-signal', signal='BuddiesChanged')
    added, removed = event.args
    assert removed == []
    assert len(added) == 1
    handle = added[0]
    assert conn.InspectHandles(1, [handle])[0] == 'charles@localhost'

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    handle, props = event.args
    assert conn.InspectHandles(1, [handle])[0] == 'charles@localhost'
    assert props == {'color': '#AABBCC,#001122'}

    # add a buddy to view 1
    message = create_gadget_message("test@localhost")

    added = message.addElement((ns.OLPC_BUDDY, 'added'))
    added['id'] = '1'
    buddy = added.addElement((None, 'buddy'))
    buddy['jid'] = 'oscar@localhost'
    properties = buddy.addElement((ns.OLPC_BUDDY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#000000,#AAAAAA')}):
        properties.addChild(node)

    stream.send(message)

    event = q.expect('dbus-signal', signal='BuddiesChanged')
    added, removed = event.args
    assert removed == []
    assert len(added) == 1
    handle = added[0]
    assert conn.InspectHandles(1, added)[0] == 'oscar@localhost'

    members = view1.Get(olpc_name_prefix + '.Channel.Interface.View',
        'Buddies', dbus_interface=dbus.PROPERTIES_IFACE)

    members = sorted(conn.InspectHandles(1, members))
    assert sorted(members) == ['bob@localhost', 'charles@localhost',
            'oscar@localhost']

    # remove a buddy from view 1
    message = create_gadget_message("test@localhost")

    added = message.addElement((ns.OLPC_BUDDY, 'removed'))
    added['id'] = '1'
    buddy = added.addElement((None, 'buddy'))
    buddy['jid'] = 'bob@localhost'

    stream.send(message)

    event = q.expect('dbus-signal', signal='BuddiesChanged')
    added, removed = event.args
    assert added == []
    assert len(removed) == 1
    handle = removed[0]
    assert conn.InspectHandles(1, [handle])[0] == 'bob@localhost'

    members = view1.Get(olpc_name_prefix + '.Channel.Interface.View',
        'Buddies', dbus_interface=dbus.PROPERTIES_IFACE)
    members = sorted(conn.InspectHandles(1, members))
    assert sorted(members) == ['charles@localhost', 'oscar@localhost']

    # test alias search
    call_async(q, requests_iface, 'CreateChannel',
        { tp_name_prefix + '.Channel.ChannelType':
            olpc_name_prefix + '.Channel.Type.BuddyView',
          olpc_name_prefix + '.Channel.Interface.View.MaxSize': 10,
          olpc_name_prefix + '.Channel.Type.BuddyView.Alias': 'tom'
          })


    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=ns.OLPC_BUDDY),
        EventPattern('dbus-return', method='CreateChannel'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '3'
    assert view['size'] == '10'
    buddy = xpath.queryForNodes('/iq/view/buddy', iq_event.stanza)
    assert len(buddy) == 1
    assert buddy[0]['alias'] == 'tom'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'tom@localhost'
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'thomas@localhost'
    stream.send(reply)

    view_path = return_event.value[0]
    props = return_event.value[1]
    view3 = bus.get_object(conn.bus_name, view_path)

    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Properties'] == {}
    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Alias'] == 'tom'

    # check org.laptop.Telepathy.Channel.Type.BuddyView D-Bus properties
    props = view3.GetAll(
        'org.laptop.Telepathy.Channel.Type.BuddyView',
        dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['Properties'] == {}
    assert props['Alias'] == 'tom'

    event = q.expect('dbus-signal', signal='BuddiesChanged')
    added, removed = event.args
    assert removed == []
    assert len(added) == 2
    assert sorted(conn.InspectHandles(1, added)) == ['thomas@localhost',
            'tom@localhost']

    close_view(q, view1, '1')

    close_view(q, view2, '2')

    close_view(q, view3, '3')

    # View request without MaxSize property
    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.BuddyView',
          })

    event = q.expect('dbus-error', method='CreateChannel')
    assert event.error.get_dbus_name() == cs.INVALID_ARGUMENT

    # test alias and properties search
    props = dbus.Dictionary({'color': '#AABBCC,#001122'}, signature='sv')
    call_async(q, requests_iface, 'CreateChannel',
        { tp_name_prefix + '.Channel.ChannelType':
            olpc_name_prefix + '.Channel.Type.BuddyView',
          olpc_name_prefix + '.Channel.Interface.View.MaxSize': 5,
          olpc_name_prefix + '.Channel.Type.BuddyView.Properties': props,
          olpc_name_prefix + '.Channel.Type.BuddyView.Alias': 'jean'
          })

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=ns.OLPC_BUDDY),
        EventPattern('dbus-return', method='CreateChannel'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '4'
    assert view['size'] == '5'

    properties_node = xpath.queryForNodes('/iq/view/buddy/properties',
            iq_event.stanza)
    props = parse_properties(properties_node[0])
    assert props == {'color': ('str', '#AABBCC,#001122')}

    buddy = xpath.queryForNodes('/iq/view/buddy', iq_event.stanza)
    assert len(buddy) == 1
    assert buddy[0]['alias'] == 'jean'

    view_path = return_event.value[0]
    props = return_event.value[1]
    view4 = bus.get_object(conn.bus_name, view_path)

    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Properties'] == dbus.Dictionary({'color': '#AABBCC,#001122'}, signature='sv')
    assert props['org.laptop.Telepathy.Channel.Type.BuddyView.Alias'] == 'jean'

if __name__ == '__main__':
    exec_test(test)
