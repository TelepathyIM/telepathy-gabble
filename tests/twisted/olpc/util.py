import dbus

from servicetest import call_async, EventPattern
from gabbletest import make_result_iq, elem, elem_iq
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
import constants as cs
import ns

def properties_to_xml(properties):
    result = []

    for key, (type, value) in properties.iteritems():
        property = domish.Element((None, 'property'))
        property['type'] = type
        property['name'] = key
        property.addContent(value)
        result.append(property)

    return result

def _make_pubsub_event_msg(from_, node):
    # manually create the item node as we need a ref on it
    item = domish.Element((None, 'item'))

    message = elem('message', from_=from_, to='test@localhost')(
        elem(ns.PUBSUB_EVENT, 'event')(
            elem('items', node=node)(item)))

    return message, item

def send_buddy_changed_properties_msg(stream, from_, props):
    message, item = _make_pubsub_event_msg(from_,
        ns.OLPC_BUDDY_PROPS)

    properties = item.addElement((ns.OLPC_BUDDY_PROPS, 'properties'))

    for child in properties_to_xml(props):
        properties.addChild(child)

    stream.send(message)

def send_buddy_changed_current_act_msg(stream, from_, id, room):
    message, item = _make_pubsub_event_msg(from_,
        ns.OLPC_CURRENT_ACTIVITY)

    activity = item.addElement((ns.OLPC_CURRENT_ACTIVITY, 'activity'))
    activity['room'] = room
    activity['type'] = id

    stream.send(message)

def answer_to_current_act_pubsub_request(stream, request, id, room):
    # check request structure
    assert request['type'] == 'get'
    items = xpath.queryForNodes(
        '/iq/pubsub[@xmlns="%s"]/items' % ns.PUBSUB, request)[0]
    assert items['node'] == ns.OLPC_CURRENT_ACTIVITY

    reply = make_result_iq(stream, request)
    reply['from'] = request['to']
    pubsub = reply.firstChildElement()
    items = pubsub.addElement((None, 'items'))
    items['node'] = ns.OLPC_CURRENT_ACTIVITY
    item = items.addElement((None, 'item'))
    item['id'] = 'itemID'
    activity = item.addElement((ns.OLPC_CURRENT_ACTIVITY, 'activity'))
    activity['room'] = room
    activity['type'] = id
    reply.send()

def answer_error_to_pubsub_request(stream, request):
    # look for node's name in the request
    items = xpath.queryForNodes('/iq/pubsub/items', request)[0]
    node = items['node']

    reply = IQ(stream, "error")
    reply['id'] = request['id']
    reply['from'] = request['to']
    pubsub = reply.addElement((ns.PUBSUB, 'pubsub'))
    items = pubsub.addElement((None, 'items'))
    items['node'] = node
    error = reply.addElement((None, 'error'))
    error['type'] = 'auth'
    error.addElement((ns.STANZA, 'not-authorized'))
    error.addElement(("%s#errors" % ns.PUBSUB, 'presence-subscription-required'))
    stream.send(reply)

