import dbus

from servicetest import call_async, EventPattern
from gabbletest import make_result_iq, acknowledge_iq, elem, elem_iq
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"

NS_DISCO_INFO = "http://jabber.org/protocol/disco#info"
NS_DISCO_ITEMS = "http://jabber.org/protocol/disco#items"
NS_PUBSUB = 'http://jabber.org/protocol/pubsub'
NS_AMP = "http://jabber.org/protocol/amp"
NS_STANZA = "urn:ietf:params:xml:ns:xmpp-stanzas"

# Copied from Gadget
valid_types = ['str', 'int', 'uint', 'bool', 'bytes']

def parse_properties(elems):
    properties = {}

    for elem in xpath_query('/*/property', elems):
        type = elem.getAttribute('type')
        name = elem.getAttribute('name')
        value = None

        for child in elem.children:
            if isinstance(child, unicode) or isinstance(child, str):
                value = child
                break

        if type is None or name is None or value is None:
            continue

        if type not in valid_types:
            raise PropertyTypeError(type, elems.uri)

        if type == 'bool' and value not in ['1', '0', 'true', 'false']:
            raise PropertyTypeError(type, elems.uri)

        properties[name] = (type, value)

    return properties

def properties_to_xml(properties):
    result = []

    for key, (type, value) in properties.iteritems():
        property = domish.Element((None, 'property'))
        property['type'] = type
        property['name'] = key
        property.addContent(value)
        result.append(property)

    return result

def announce_gadget(q, stream, disco_stanza):
    # announce Gadget service
    reply = make_result_iq(stream, disco_stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    item = query.addElement((None, 'item'))
    item['jid'] = 'gadget.localhost'
    stream.send(reply)

    # wait for Gadget disco#info query
    event = q.expect('stream-iq', to='gadget.localhost', query_ns=NS_DISCO_INFO)

    reply = elem_iq(stream, 'result', id=event.stanza['id'])(
        elem(NS_DISCO_INFO, 'query')(
            elem('identity', category='collaboration', type='gadget', name='OLPC Gadget')(),
            elem('feature', var=NS_OLPC_BUDDY)(),
            elem('feature', var=NS_OLPC_ACTIVITY)()))

    stream.send(reply)

def _make_pubsub_event_msg(from_, node):
    # manually create the item node as we need a ref on it
    item = domish.Element((None, 'item'))

    message = elem('message', from_=from_, to='test@localhost')(
        elem("%s#event" % NS_PUBSUB, 'event')(
            elem('items', node=node)(item)))

    return message, item

def send_buddy_changed_properties_msg(stream, from_, props):
    message, item = _make_pubsub_event_msg(from_,
        NS_OLPC_BUDDY_PROPS)

    properties = item.addElement((NS_OLPC_BUDDY_PROPS, 'properties'))

    for child in properties_to_xml(props):
        properties.addChild(child)

    stream.send(message)

def send_buddy_changed_current_act_msg(stream, from_, id, room):
    message, item = _make_pubsub_event_msg(from_,
        NS_OLPC_CURRENT_ACTIVITY)

    activity = item.addElement((NS_OLPC_CURRENT_ACTIVITY, 'activity'))
    activity['room'] = room
    activity['type'] = id

    stream.send(message)

def answer_to_current_act_pubsub_request(stream, request, id, room):
    # check request structure
    assert request['type'] == 'get'
    items = xpath.queryForNodes(
        '/iq/pubsub[@xmlns="%s"]/items' % NS_PUBSUB, request)[0]
    assert items['node'] == NS_OLPC_CURRENT_ACTIVITY

    reply = make_result_iq(stream, request)
    reply['from'] = request['to']
    pubsub = reply.firstChildElement()
    items = pubsub.addElement((None, 'items'))
    items['node'] = NS_OLPC_CURRENT_ACTIVITY
    item = items.addElement((None, 'item'))
    item['id'] = 'itemID'
    activity = item.addElement((NS_OLPC_CURRENT_ACTIVITY, 'activity'))
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
    pubsub = reply.addElement((NS_PUBSUB, 'pubsub'))
    items = pubsub.addElement((None, 'items'))
    items['node'] = node
    error = reply.addElement((None, 'error'))
    error['type'] = 'auth'
    error.addElement((NS_STANZA, 'not-authorized'))
    error.addElement(("%s#errors" % NS_PUBSUB, 'presence-subscription-required'))
    stream.send(reply)

def send_gadget_current_activity_changed_msg(stream, buddy, view_id, id, room):
    message = elem('message', from_='gadget.localhost',
        to='test@localhost', type='notice')(
            elem(NS_OLPC_BUDDY, 'change', jid=buddy, id=view_id)(
                elem(NS_OLPC_CURRENT_ACTIVITY, 'activity', id=id, room=room)()),
            elem(NS_AMP, 'amp')(
                elem('rule', condition='deliver-at', value='stored',
                    action='error')))

    stream.send(message)

def send_reply_to_activity_view_request(stream, stanza, activities):
    reply = make_result_iq(stream, stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'test@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    for id, room, props, buddies in activities:
        activity = view.addElement((None, "activity"))
        activity['room'] = room
        activity['id'] = id
        if props:
            properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS,
                "properties"))
            for child in properties_to_xml(props):
                properties.addChild(child)

        for jid, props in buddies:
            buddy = activity.addElement((None, 'buddy'))
            buddy['jid'] = jid
            if props:
                properties = buddy.addElement((NS_OLPC_BUDDY_PROPS,
                    "properties"))
                for child in properties_to_xml(props):
                    properties.addChild(child)

    stream.send(reply)

def request_random_activity_view(q, stream, conn, max, id, activities):
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    call_async(q, gadget_iface, 'RequestRandomActivities', max)

    iq_event, return_event = q.expect_many(
    EventPattern('stream-iq', to='gadget.localhost',
        query_ns=NS_OLPC_ACTIVITY),
    EventPattern('dbus-return', method='RequestRandomActivities'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == id
    random = xpath.queryForNodes('/iq/view/random', iq_event.stanza)
    assert len(random) == 1
    assert random[0]['max'] == str(max)

    send_reply_to_activity_view_request(stream, iq_event.stanza, activities)

    return return_event.value[0]

# copied from Gadget
def xpath_query(query, elem):
    nodes = xpath.queryForNodes(query, elem)

    if nodes is None:
        return []
    else:
        return nodes

def create_gadget_message(to):
    message = domish.Element((None, 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = to
    message['type'] = 'notice'
    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'

    return message
