"""
test OLPC search activity
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq, sync_stream

from twisted.words.xish import xpath
from util import (announce_gadget, request_random_activity_view,
    answer_error_to_pubsub_request, send_reply_to_activity_view_request,
    parse_properties, properties_to_xml, create_gadget_message, close_view)
import ns
import constants as cs

tp_name_prefix = 'org.freedesktop.Telepathy'
olpc_name_prefix = 'org.laptop.Telepathy'

def check_view(view, conn, activities, buddies):
    act = view.Get(olpc_name_prefix + '.Channel.Interface.View',
        'Activities',
        dbus_interface=dbus.PROPERTIES_IFACE)

    assert sorted(act) == sorted(activities)

    handles = view.Get(olpc_name_prefix + '.Channel.Interface.View',
        'Buddies',
        dbus_interface=dbus.PROPERTIES_IFACE)
    assert sorted(conn.InspectHandles(1, handles)) == sorted(buddies)

def test(q, bus, conn, stream):
    conn.Connect()

    handles = {}

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    activity_prop_iface = dbus.Interface(conn,
            'org.laptop.Telepathy.ActivityProperties')
    buddy_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')
    requests_iface = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    # Gadget was not announced yet
    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.ActivityView',
            'org.laptop.Telepathy.Channel.Interface.View.MaxSize': 5,
          })

    event = q.expect('dbus-error', method='CreateChannel')
    announce_gadget(q, stream, disco_event.stanza)
    sync_stream(q, stream)

    # check if we can request Activity views
    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)

    assert ({cs.CHANNEL_TYPE:
            olpc_name_prefix + '.Channel.Type.ActivityView'},

            [olpc_name_prefix + '.Channel.Interface.View.MaxSize',
             olpc_name_prefix + '.Channel.Type.ActivityView.Properties',
             olpc_name_prefix + '.Channel.Type.ActivityView.Participants'],
         ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    # request 3 random activities (view 1)
    view_path = request_random_activity_view(q, stream, conn, 3, '1',
            [('activity1', 'room1@conference.localhost',
                {'color': ('str', '#005FE4,#00A0FF')},
                [('lucien@localhost', {'color': ('str', '#AABBCC,#CCBBAA')}),
                 ('jean@localhost', {})]),])

    view1 = bus.get_object(conn.bus_name, view_path)

    # check org.freedesktop.Telepathy.Channel D-Bus properties
    props = view1.GetAll(cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['ChannelType'] == 'org.laptop.Telepathy.Channel.Type.ActivityView'
    assert 'org.laptop.Telepathy.Channel.Interface.View' in props['Interfaces']
    assert props['TargetHandle'] == 0
    assert props['TargetID'] == ''
    assert props['TargetHandleType'] == 0

    # check org.laptop.Telepathy.Channel.Interface.View D-Bus properties
    props = view1.GetAll(
        'org.laptop.Telepathy.Channel.Interface.View',
        dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['MaxSize'] == 3

    # check org.laptop.Telepathy.Channel.Type.ActivityView D-Bus properties
    props = view1.GetAll(
        'org.laptop.Telepathy.Channel.Type.ActivityView',
        dbus_interface=dbus.PROPERTIES_IFACE)

    assert props['Properties'] == {}
    assert props['Participants'] == []

    assert view1.GetChannelType(dbus_interface=cs.CHANNEL) ==\
            'org.laptop.Telepathy.Channel.Type.ActivityView'

    ## Current views ##
    # view 1: activity 1 (with: Lucien, Jean)

    handles['lucien'], handles['jean'] = \
            conn.RequestHandles(1, ['lucien@localhost', 'jean@localhost'])

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    handles['room1'], props = event.args
    assert conn.InspectHandles(2, [handles['room1']])[0] == \
            'room1@conference.localhost'
    assert props == {'color': '#005FE4,#00A0FF'}

    q.expect('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.Channel.Interface.View',
            args=[[('activity1', handles['room1'])], []])

    # participants are added to view
    q.expect('dbus-signal', signal='BuddiesChanged',
            args=[[handles['lucien'], handles['jean']], []])

    # participants are added to activity
    q.expect_many(
        EventPattern('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.BuddyInfo',
            args=[handles['lucien'], [('activity1', handles['room1'])]]),
        EventPattern('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.BuddyInfo',
            args=[handles['jean'], [('activity1', handles['room1'])]]))

    # check activities and buddies in view
    check_view(view1, conn, [('activity1', handles['room1'])],
            ['lucien@localhost', 'jean@localhost'])

    # we can now get activity properties
    props = activity_prop_iface.GetProperties(handles['room1'])
    assert props == {'color': '#005FE4,#00A0FF'}

    # and we can get participant's properties too
    props = buddy_prop_iface.GetProperties(handles['lucien'])
    assert props == {'color': '#AABBCC,#CCBBAA'}

    # and their activities
    call_async (q, buddy_prop_iface, 'GetActivities', handles['lucien'])

    event = q.expect('stream-iq', to='lucien@localhost', query_name='pubsub',
            query_ns=ns.PUBSUB)
    # return an error, we can't query pubsub node
    answer_error_to_pubsub_request(stream, event.stanza)

    q.expect('dbus-return', method='GetActivities',
            value=([('activity1', handles['room1'])],))

    # activity search by properties (view 2)
    props = dbus.Dictionary({'color': '#AABBCC,#001122'}, signature='sv')

    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.ActivityView',
            'org.laptop.Telepathy.Channel.Interface.View.MaxSize': 5,
            'org.laptop.Telepathy.Channel.Type.ActivityView.Properties': props,
          })

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=ns.OLPC_ACTIVITY),
        EventPattern('dbus-return', method='CreateChannel'))

    properties_nodes = xpath.queryForNodes('/iq/view/activity/properties',
            iq_event.stanza)
    props = parse_properties(properties_nodes[0])
    assert props == {'color': ('str', '#AABBCC,#001122')}

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '2'
    assert view['size'] == '5'

    send_reply_to_activity_view_request(stream, iq_event.stanza,
            [('activity2', 'room2@conference.localhost',
                {'color': ('str', '#AABBCC,#001122')}, [])])

    ## Current views ##
    # view 1: activity 1 (with: Lucien, Jean)
    # view 2: activity 2

    view_path = return_event.value[0]
    view2 = bus.get_object(conn.bus_name, view_path)

    props = return_event.value[1]
    assert props['org.laptop.Telepathy.Channel.Type.ActivityView.Properties'] == \
            {'color': '#AABBCC,#001122'}
    assert props['org.laptop.Telepathy.Channel.Type.ActivityView.Participants'] == []

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    handles['room2'], props = event.args
    assert conn.InspectHandles(2, [handles['room2']])[0] == 'room2@conference.localhost'
    assert props == {'color': '#AABBCC,#001122'}

    q.expect('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.Channel.Interface.View',
            args=[[('activity2', handles['room2'])], []])

    act = view2.Get(olpc_name_prefix + '.Channel.Interface.View',
        'Activities', dbus_interface=dbus.PROPERTIES_IFACE)
    assert sorted(act) == [('activity2', handles['room2'])]

    buddies = view2.Get(olpc_name_prefix + '.Channel.Interface.View',
        'Buddies', dbus_interface=dbus.PROPERTIES_IFACE)
    assert buddies == []

    # activity search by participants (view 3)
    participants = conn.RequestHandles(1, ["alice@localhost", "bob@localhost"])

    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.ActivityView',
            'org.laptop.Telepathy.Channel.Interface.View.MaxSize': 5,
            'org.laptop.Telepathy.Channel.Type.ActivityView.Participants': participants,
          })

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=ns.OLPC_ACTIVITY),
        EventPattern('dbus-return', method='CreateChannel'))

    buddies = xpath.queryForNodes('/iq/view/activity/buddy', iq_event.stanza)
    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '3'
    assert view['size'] == '5'
    assert len(buddies) == 2
    assert (buddies[0]['jid'], buddies[1]['jid']) == ('alice@localhost',
            'bob@localhost')

    send_reply_to_activity_view_request(stream, iq_event.stanza,
            [('activity3', 'room3@conference.localhost',
                {'color': ('str', '#AABBCC,#001122')}, [])])

    ## Current views ##
    # view 1: activity 1 (with: Lucien, Jean)
    # view 2: activity 2
    # view 3: activity 3

    view_path = return_event.value[0]
    view3 = bus.get_object(conn.bus_name, view_path)

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    handles['room3'], props = event.args
    assert conn.InspectHandles(2, [handles['room3']])[0] == 'room3@conference.localhost'
    assert props == {'color': '#AABBCC,#001122'}

    q.expect('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.Channel.Interface.View',
            args=[[('activity3', handles['room3'])], []])

    act = view3.Get(olpc_name_prefix + '.Channel.Interface.View',
        'Activities',
        dbus_interface=dbus.PROPERTIES_IFACE)
    assert sorted(act) == [('activity3', handles['room3'])]

    # add activity 4 to view 1
    message = create_gadget_message('alice@localhost')

    added = message.addElement((ns.OLPC_ACTIVITY, 'added'))
    added['id'] = '1'
    activity = added.addElement((None, 'activity'))
    activity['id'] = 'activity4'
    activity['room'] = 'room4@conference.localhost'
    properties = activity.addElement((ns.OLPC_ACTIVITY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#DDEEDD,#EEDDEE')}):
        properties.addChild(node)
    buddy = activity.addElement((None, 'buddy'))
    buddy['jid'] = 'fernand@localhost'
    properties = buddy.addElement((ns.OLPC_BUDDY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#AABBAA,#BBAABB')}):
        properties.addChild(node)
    buddy = activity.addElement((None, 'buddy'))
    buddy['jid'] = 'jean@localhost'

    stream.send(message)

    ## Current views ##
    # view 1: activity 1 (with: Lucien, Jean), activity 4 (with: Fernand, Jean)
    # view 2: activity 2
    # view 3: activity 3
    # participants are added to view

    handles['fernand'] = conn.RequestHandles(1, ['fernand@localhost',])[0]

    # activity is added
    event = q.expect('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.Channel.Interface.View')
    added, removed = event.args
    assert len(added) == 1
    id, handles['room4'] = added[0]
    assert id == 'activity4'
    assert sorted(conn.InspectHandles(2, [handles['room4']])) == \
            ['room4@conference.localhost']

    # buddies are added to view
    q.expect('dbus-signal', signal='BuddiesChanged',
            args=[[handles['fernand'], handles['jean']], []])

    # buddies are added to activity
    q.expect_many(
        EventPattern('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.BuddyInfo',
            args=[handles['fernand'], [('activity4', handles['room4'])]]),
        EventPattern('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.BuddyInfo',
            args=[handles['jean'], [('activity1', handles['room1']),
                ('activity4', handles['room4'])]]))

    # check activities and buddies in view
    check_view(view1, conn, [
        ('activity1', handles['room1']), ('activity4', handles['room4'])],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost'])

    # check activity's properties
    props = activity_prop_iface.GetProperties(handles['room4'])
    assert props == {'color': '#DDEEDD,#EEDDEE'}

    # Gadget informs us about an activity properties change
    message = create_gadget_message('alice@localhost')

    change = message.addElement((ns.OLPC_ACTIVITY, 'change'))
    change['activity'] = 'activity1'
    change['room'] = 'room1@conference.localhost'
    change['id'] = '1'
    properties = change.addElement((ns.OLPC_ACTIVITY_PROPS, 'properties'))
    for node in properties_to_xml({'tags': ('str', 'game'), \
            'color': ('str', '#AABBAA,#BBAABB')}):
        properties.addChild(node)

    stream.send(message)

    q.expect('dbus-signal', signal='ActivityPropertiesChanged',
            args=[handles['room1'], {'tags': 'game', 'color': '#AABBAA,#BBAABB'}])

    # we now get the new activity properties
    props = activity_prop_iface.GetProperties(handles['room1'])
    assert props == {'tags': 'game', 'color': '#AABBAA,#BBAABB'}

    # Marcel joined activity 1
    message = create_gadget_message('alice@localhost')

    activity = message.addElement((ns.OLPC_ACTIVITY, 'activity'))
    activity['room'] = 'room1@conference.localhost'
    activity['view'] = '1'
    joined = activity.addElement((None, 'joined'))
    joined['jid'] = 'marcel@localhost'
    properties = joined.addElement((ns.OLPC_BUDDY_PROPS, "properties"))
    for node in properties_to_xml({'color': ('str', '#CCCCCC,#DDDDDD')}):
        properties.addChild(node)

    stream.send(message)

    ## Current views ##
    # view 1: activity 1 (with: Lucien, Jean, Marcel), activity 4 (with
    # Fernand, Jean)
    # view 2: activity 2
    # view 3: activity 3

    handles['marcel'] = conn.RequestHandles(1, ['marcel@localhost',])[0]

    q.expect_many(
            EventPattern('dbus-signal', signal='BuddiesChanged',
                args=[[handles['marcel']], []]),
            EventPattern('dbus-signal', signal='PropertiesChanged',
                args=[handles['marcel'], {'color': '#CCCCCC,#DDDDDD'}]),
            EventPattern('dbus-signal', signal='ActivitiesChanged',
                interface='org.laptop.Telepathy.BuddyInfo',
                args=[handles['marcel'], [('activity1', handles['room1'])]]))

    # check activities and buddies in view
    check_view(view1, conn, [
        ('activity1', handles['room1']),('activity4', handles['room4'])],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost',
            'marcel@localhost'])

    # Marcel left activity 1
    message = create_gadget_message('alice@localhost')

    activity = message.addElement((ns.OLPC_ACTIVITY, 'activity'))
    activity['room'] = 'room1@conference.localhost'
    activity['view'] = '1'
    left = activity.addElement((None, 'left'))
    left['jid'] = 'marcel@localhost'

    stream.send(message)

    ## Current views ##
    # view 1: activity 1 (with: Lucien, Jean), activity 4 (with Fernand, Jean)
    # view 2: activity 2
    # view 3: activity 3

    q.expect_many(
            EventPattern('dbus-signal', signal='BuddiesChanged',
                args=[[], [handles['marcel']]]),
            EventPattern('dbus-signal', signal='ActivitiesChanged',
                interface='org.laptop.Telepathy.BuddyInfo',
                args=[handles['marcel'], []]))

    # check activities and buddies in view
    check_view(view1, conn, [
        ('activity1', handles['room1']),('activity4', handles['room4'])],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost'])

    # Jean left activity 1
    message = create_gadget_message('alice@localhost')

    activity = message.addElement((ns.OLPC_ACTIVITY, 'activity'))
    activity['room'] = 'room1@conference.localhost'
    activity['view'] = '1'
    left = activity.addElement((None, 'left'))
    left['jid'] = 'jean@localhost'

    stream.send(message)

    ## Current views ##
    # view 1: activity 1 (with: Lucien), activity 4 (with Fernand, Jean)
    # view 2: activity 2
    # view 3: activity 3

    q.expect('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.BuddyInfo',
            args=[handles['jean'], [('activity4', handles['room4'])]])

    # Jean wasn't removed from the view as he is still in activity 4
    check_view(view1, conn, [
        ('activity1', handles['room1']),('activity4', handles['room4'])],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost'])

    # remove activity 1 from view 1
    message = create_gadget_message('alice@localhost')

    removed = message.addElement((ns.OLPC_ACTIVITY, 'removed'))
    removed['id'] = '1'
    activity = removed.addElement((None, 'activity'))
    activity['id'] = 'activity1'
    activity['room'] = 'room1@conference.localhost'

    stream.send(message)

    ## Current views ##
    # view 1: activity 4 (with Jean, Fernand)
    # view 2: activity 2
    # view 3: activity 3

    q.expect_many(
    # participants are removed from the view
            EventPattern('dbus-signal', signal='BuddiesChanged',
                args=[[], [handles['lucien']]]),
    # activity is removed from the view
            EventPattern('dbus-signal', signal='ActivitiesChanged',
                interface='org.laptop.Telepathy.Channel.Interface.View',
                args=[[], [('activity1', handles['room1'])]]),
            EventPattern('dbus-signal', signal='ActivitiesChanged',
                interface='org.laptop.Telepathy.BuddyInfo',
                args=[handles['lucien'], []]))

    # check activities and buddies in view
    check_view(view1, conn, [
        ('activity4', handles['room4'])],
        ['fernand@localhost', 'jean@localhost'])

    # close view 1
    call_async(q, view1, 'Close')
    event_msg, _, _, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'),
        # Jean and Fernand left activity4
        EventPattern('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.BuddyInfo',
            args=[handles['jean'], []]),
        EventPattern('dbus-signal', signal='ActivitiesChanged',
            interface='org.laptop.Telepathy.BuddyInfo',
            args=[handles['fernand'], []]))

    close = xpath.queryForNodes('/message/close', event_msg.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '1'

    close_view(q, view2, '2')

    close_view(q, view3, '3')

    # View request without MaxSize property
    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.ActivityView',
          })

    event = q.expect('dbus-error', method='CreateChannel')
    assert event.error.get_dbus_name() == cs.INVALID_ARGUMENT

    # test participants and properties search
    props = dbus.Dictionary({'color': '#AABBCC,#001122'}, signature='sv')
    participants = conn.RequestHandles(1, ["alice@localhost", "bob@localhost"])

    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.ActivityView',
            'org.laptop.Telepathy.Channel.Interface.View.MaxSize': 5,
            'org.laptop.Telepathy.Channel.Type.ActivityView.Properties': props,
            'org.laptop.Telepathy.Channel.Type.ActivityView.Participants': participants,
          })

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=ns.OLPC_ACTIVITY),
        EventPattern('dbus-return', method='CreateChannel'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '4'
    assert view['size'] == '5'

    properties_nodes = xpath.queryForNodes('/iq/view/activity/properties',
            iq_event.stanza)
    props = parse_properties(properties_nodes[0])
    assert props == {'color': ('str', '#AABBCC,#001122')}

    buddies = xpath.queryForNodes('/iq/view/activity/buddy', iq_event.stanza)
    assert len(buddies) == 2
    assert (buddies[0]['jid'], buddies[1]['jid']) == ('alice@localhost',
            'bob@localhost')

    view_path = return_event.value[0]
    props = return_event.value[1]
    view4 = bus.get_object(conn.bus_name, view_path)

    assert props['org.laptop.Telepathy.Channel.Type.ActivityView.Properties'] == \
            dbus.Dictionary({'color': '#AABBCC,#001122'}, signature='sv')
    assert conn.InspectHandles(1, props['org.laptop.Telepathy.Channel.Type.ActivityView.Participants']) == \
            ["alice@localhost", "bob@localhost"]

if __name__ == '__main__':
    exec_test(test)
