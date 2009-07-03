import dbus

from servicetest import call_async, EventPattern
from gabbletest import make_result_iq, elem, elem_iq
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
import constants as cs
import ns

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
            raise ValueError

        if type == 'bool' and value not in ['1', '0', 'true', 'false']:
            raise ValueError

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
    event = q.expect('stream-iq', to='gadget.localhost', query_ns=ns.DISCO_INFO)

    reply = elem_iq(stream, 'result', id=event.stanza['id'], from_='gadget.localhost')(
        elem(ns.DISCO_INFO, 'query')(
            elem('identity', category='collaboration', type='gadget', name='OLPC Gadget')(),
            elem('feature', var=ns.OLPC_BUDDY)(),
            elem('feature', var=ns.OLPC_ACTIVITY)()))

    stream.send(reply)

def _make_pubsub_event_msg(from_, node):
    # manually create the item node as we need a ref on it
    item = domish.Element((None, 'item'))

    message = elem('message', from_=from_, to='test@localhost')(
        elem("%s#event" % ns.PUBSUB, 'event')(
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

def send_gadget_current_activity_changed_msg(stream, buddy, view_id, id, room):
    message = elem('message', from_='gadget.localhost',
        to='test@localhost', type='notice')(
            elem(ns.OLPC_BUDDY, 'change', jid=buddy, id=view_id)(
                elem(ns.OLPC_CURRENT_ACTIVITY, 'activity', id=id, room=room)()),
            elem(ns.AMP, 'amp')(
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
            properties = activity.addElement((ns.OLPC_ACTIVITY_PROPS,
                "properties"))
            for child in properties_to_xml(props):
                properties.addChild(child)

        for jid, props in buddies:
            buddy = activity.addElement((None, 'buddy'))
            buddy['jid'] = jid
            if props:
                properties = buddy.addElement((ns.OLPC_BUDDY_PROPS,
                    "properties"))
                for child in properties_to_xml(props):
                    properties.addChild(child)

    stream.send(reply)

def request_random_activity_view(q, stream, conn, max, id, activities):
    requests_iface = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    call_async(q, requests_iface, 'CreateChannel',
        { cs.CHANNEL_TYPE:
            'org.laptop.Telepathy.Channel.Type.ActivityView',
            'org.laptop.Telepathy.Channel.Interface.View.MaxSize': max
          })

    iq_event, return_event = q.expect_many(
    EventPattern('stream-iq', to='gadget.localhost',
        query_ns=ns.OLPC_ACTIVITY),
    EventPattern('dbus-return', method='CreateChannel'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == id
    assert view['size'] == str(max)
    random = xpath.queryForNodes('/iq/view/random', iq_event.stanza)

    send_reply_to_activity_view_request(stream, iq_event.stanza, activities)

    props = return_event.value[1]
    assert props['org.laptop.Telepathy.Channel.Type.ActivityView.Properties'] == {}
    assert props['org.laptop.Telepathy.Channel.Type.ActivityView.Participants'] == []

    return return_event.value[0]

def close_view(q, view, id):
    chan_iface = dbus.Interface(view, cs.CHANNEL)
    call_async(q, chan_iface, 'Close')
    event, _, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == id

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
    amp = message.addElement((ns.AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'

    return message

def gadget_publish(q, stream, conn, publish):
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    call_async(q, gadget_iface, 'Publish', publish)
    if publish:
        q.expect_many(
                EventPattern('stream-presence', presence_type='subscribe'),
                EventPattern('dbus-return', method='Publish'))

        # accept the request
        presence = elem('presence', to='test@localhost', from_='gadget.localhost',
            type='subscribed')
        stream.send(presence)

        # send a subscribe request
        presence = elem('presence', to='test@localhost', from_='gadget.localhost',
            type='subscribe')
        stream.send(presence)

        q.expect('stream-presence', presence_type='subscribed'),
    else:
        q.expect_many(
                EventPattern('stream-presence', presence_type='unsubscribe'),
                EventPattern('stream-presence', presence_type='unsubscribed'),
                EventPattern('dbus-return', method='Publish'))

        # Gadget tries to subscribe but is refused now
        presence = elem('presence', to='test@localhost', from_='gadget.localhost',
            type='subscribe')
        stream.send(presence)
