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
NS_AMP = "http://jabber.org/protocol/amp"

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
