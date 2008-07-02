from gabbletest import make_result_iq, acknowledge_iq, elem, elem_iq
from twisted.words.xish import domish, xpath

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
    # TODO: Would be cool to use elem() but there is no API
    # to get a pointer on the item node...
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = from_
    message['to'] = 'test@localhost'
    event = message.addElement(("%s#event" % NS_PUBSUB, 'event'))

    items = event.addElement((None, 'items'))
    items['node'] = node
    item = items.addElement((None, 'item'))

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
    # TODO: check request structure
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

