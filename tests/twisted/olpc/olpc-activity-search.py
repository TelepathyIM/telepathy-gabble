"""
test OLPC search activity
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"

NS_PUBSUB = "http://jabber.org/protocol/pubsub"
NS_DISCO_INFO = "http://jabber.org/protocol/disco#info"
NS_DISCO_ITEMS = "http://jabber.org/protocol/disco#items"


NS_AMP = "http://jabber.org/protocol/amp"

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=NS_DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    # announce Gadget service
    reply = make_result_iq(stream, disco_event.stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    item = query.addElement((None, 'item'))
    item['jid'] = 'gadget.localhost'
    stream.send(reply)

    # wait for Gadget disco#info query
    event = q.expect('stream-iq', to='gadget.localhost', query_ns=NS_DISCO_INFO)
    reply = make_result_iq(stream, event.stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    identity = query.addElement((None, 'identity'))
    identity['category'] = 'collaboration'
    identity['type'] = 'gadget'
    identity['name'] = 'OLPC Gadget'
    feature = query.addElement((None, 'feature'))
    feature['var'] = NS_OLPC_BUDDY
    feature = query.addElement((None, 'feature'))
    feature['var'] = NS_OLPC_ACTIVITY
    stream.send(reply)

    activity_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.ActivityProperties')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    sync_stream(q, stream)

    # request 3 random activities
    call_async(q, gadget_iface, 'RequestRandomActivities', 3)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=NS_OLPC_ACTIVITY),
        EventPattern('dbus-return', method='RequestRandomActivities'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '0'
    random = xpath.queryForNodes('/iq/view/random', iq_event.stanza)
    assert len(random) == 1
    assert random[0]['max'] == '3'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    activity = view.addElement((None, "activity"))
    activity['room'] = 'room1@conference.localhost'
    properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#005FE4,#00A0FF')
    stream.send(reply)

    view_path = return_event.value[0]
    view0 = bus.get_object(conn.bus_name, view_path)
    view0_iface = dbus.Interface(view0, 'org.laptop.Telepathy.ActivityView')

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    handle, props = event.args
    assert conn.InspectHandles(2, [handle])[0] == 'room1@conference.localhost'
    assert props == {'color': '#005FE4,#00A0FF'}

    # activity search by properties
    props = {'color': '#AABBCC,#001122'}
    call_async(q, gadget_iface, 'SearchActivitiesByProperties', props)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=NS_OLPC_ACTIVITY),
        EventPattern('dbus-return', method='SearchActivitiesByProperties'))

    properties = xpath.queryForNodes('/iq/view/activity/properties/property', iq_event.stanza)
    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '1'
    assert len(properties) == 1
    property = properties[0]
    assert property['type'] == 'str'
    assert property['name'] == 'color'
    assert property.children == ['#AABBCC,#001122']

    # reply to request
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    activity = view.addElement((None, "activity"))
    activity['room'] = 'room2@conference.localhost'
    properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#AABBCC,#001122')
    stream.send(reply)

    view_path = return_event.value[0]
    view1 = bus.get_object(conn.bus_name, view_path)
    view1_iface = dbus.Interface(view1, 'org.laptop.Telepathy.ActivityView')

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    handle, props = event.args
    assert conn.InspectHandles(2, [handle])[0] == 'room2@conference.localhost'
    assert props == {'color': '#AABBCC,#001122'}

    # activity search by participants
    participants = conn.RequestHandles(1, ["alice@localhost", "bob@localhost"])
    call_async(q, gadget_iface, 'SearchActivitiesByParticipants', participants)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=NS_OLPC_ACTIVITY),
        EventPattern('dbus-return', method='SearchActivitiesByParticipants'))

    buddies = xpath.queryForNodes('/iq/view/activity/buddy', iq_event.stanza)
    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '2'
    assert len(buddies) == 2
    assert (buddies[0]['jid'], buddies[1]['jid']) == ('alice@localhost', 'bob@localhost')

    # reply to request
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    activity = view.addElement((None, "activity"))
    activity['room'] = 'room2@conference.localhost'
    properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#AABBCC,#001122')
    stream.send(reply)

    view_path = return_event.value[0]
    view2 = bus.get_object(conn.bus_name, view_path)
    view2_iface = dbus.Interface(view2, 'org.laptop.Telepathy.ActivityView')

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    handle, props = event.args
    assert conn.InspectHandles(2, [handle])[0] == 'room2@conference.localhost'
    assert props == {'color': '#AABBCC,#001122'}

    # close view 0
    call_async(q, view0_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '0'

    # close view 1
    call_async(q, view1_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '1'

    # close view 2
    call_async(q, view2_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '2'


if __name__ == '__main__':
    exec_test(test)
