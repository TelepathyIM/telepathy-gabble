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

    change = message.addElement(('http://laptop.org/xmpp/buddy', 'change'))
    change['jid'] = 'bob@localhost'
    properties = change.addElement((NS_OLPC_BUDDY_PROPS, 'properties'))
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#FFFFFF,#AAAAAA')

    amp = message.addElement(('http://jabber.org/protocol/amp', 'amp'))
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

    return True

if __name__ == '__main__':
    go()
