"""test OLPC Buddy properties change notifications"""
import base64
import errno
import os

import dbus

# must come before the twisted imports due to side-effects
from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match, tp_name_prefix, unwrap, Event

from twisted.internet.protocol import Factory, Protocol
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
from twisted.internet import reactor

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"

NS_AMP = "http://jabber.org/protocol/amp"

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

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

    data['stream'].send(message)

    return True

@match('dbus-signal', signal='PropertiesChanged')
def expect_friends_properties_changed(event, data):
    contact = event.args[0]
    props = event.args[1]

    assert props == {'color' : '#005FE4,#00A0FF'}

    # The indexer informs us about a buddy properties change.
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'index.jabber.laptop.org'
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

    data['stream'].send(message)
    return True

@match('dbus-signal', signal='PropertiesChanged')
def expect_indexer_properties_changed(event, data):
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

    data['stream'].send(message)
    return True

@match('dbus-signal', signal='CurrentActivityChanged')
def expect_friends_current_activity_changed(event, data):
    contact = event.args[0]
    activity = event.args[1]
    room = event.args[2]
    room_id = data['conn_iface'].InspectHandles(2, [room])[0]

    assert activity == 'testactivity'
    assert room_id == 'testroom@conference.localhost'

    # The indexer informs us about a buddy current-activity change.
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'index.jabber.laptop.org'
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

    data['stream'].send(message)
    return True

@match('dbus-signal', signal='CurrentActivityChanged')
def expect_indexer_current_activity_changed(event, data):
    contact = event.args[0]
    activity = event.args[1]
    room = event.args[2]
    room_id = data['conn_iface'].InspectHandles(2, [room])[0]

    assert activity == 'testactivity2'
    assert room_id == 'testroom2@conference.localhost'

    # The indexer informs us about an activity properties change
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'index.jabber.laptop.org'
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

    data['stream'].send(message)
    return True

@match('dbus-signal', signal='ActivityPropertiesChanged')
def expect_indexer_activity_properties_changed(event, data):
    room = event.args[0]
    properties = event.args[1]

    assert properties == {'tags': 'game'}

    return True

if __name__ == '__main__':
    go()
