"""
test OLPC Buddy properties change notifications
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"

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

    # Alice, one our friends changed her properties
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'alice@localhost'
    message['to'] = 'test@localhost'
    event = message.addElement(('http://jabber.org/protocol/pubsub#event',
        'event'))

    items = event.addElement((None, 'items'))
    items['node'] = NS_OLPC_BUDDY_PROPS
    item = items.addElement((None, 'item'))
    properties = item.addElement((NS_OLPC_BUDDY_PROPS, 'properties'))

    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#005FE4,#00A0FF')
    stream.send(message)

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    contact = event.args[0]
    props = event.args[1]

    assert props == {'color' : '#005FE4,#00A0FF'}

    # The indexer informs us about a buddy properties change.
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'test@localhost'

    change = message.addElement((NS_OLPC_BUDDY, 'change'))
    change['jid'] = 'bob@localhost'
    properties = change.addElement((NS_OLPC_BUDDY_PROPS, 'properties'))
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#FFFFFF,#AAAAAA')

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'

    stream.send(message)

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    contact = event.args[0]
    props = event.args[1]

    assert props == {'color' : '#FFFFFF,#AAAAAA'}

    # Alice changes now her current-activity
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'alice@localhost'
    message['to'] = 'test@localhost'
    event = message.addElement(('http://jabber.org/protocol/pubsub#event',
        'event'))

    items = event.addElement((None, 'items'))
    items['node'] = NS_OLPC_CURRENT_ACTIVITY
    item = items.addElement((None, 'item'))

    activity = item.addElement((NS_OLPC_CURRENT_ACTIVITY, 'activity'))
    activity['room'] = 'testroom@conference.localhost'
    activity['type'] = 'testactivity'

    stream.send(message)

    event = q.expect('dbus-signal', signal='CurrentActivityChanged')
    contact = event.args[0]
    activity = event.args[1]
    room = event.args[2]
    room_id = conn.InspectHandles(2, [room])[0]

    assert activity == 'testactivity'
    assert room_id == 'testroom@conference.localhost'

    # The indexer informs us about a buddy current-activity change.
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'test@localhost'

    change = message.addElement((NS_OLPC_BUDDY, 'change'))
    change['jid'] = 'bob@localhost'
    activity = change.addElement((NS_OLPC_CURRENT_ACTIVITY, 'activity'))
    activity['type'] = 'testactivity2'
    activity['room'] = 'testroom2@conference.localhost'

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    event = q.expect('dbus-signal', signal='CurrentActivityChanged')
    contact = event.args[0]
    activity = event.args[1]
    room = event.args[2]
    room_id = conn.InspectHandles(2, [room])[0]

    assert activity == 'testactivity2'
    assert room_id == 'testroom2@conference.localhost'

    # The indexer informs us about an activity properties change
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'test@localhost'

    change = message.addElement((NS_OLPC_ACTIVITY, 'change'))
    change['activity'] = 'testactivity'
    change['room'] = 'testactivity@conference.localhost'
    properties = change.addElement((NS_OLPC_ACTIVITY_PROPS, 'properties'))
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'tags'
    property.addContent('game')

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    room = event.args[0]
    properties = event.args[1]

    assert properties == {'tags': 'game'}

if __name__ == '__main__':
    exec_test(test)
