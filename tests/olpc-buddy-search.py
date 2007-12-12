"""test OLPC search buddy"""
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
NS_PUBSUB = "http://jabber.org/protocol/pubsub"

NS_AMP = "http://jabber.org/protocol/amp"

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    data['buddy_info_iface'] = dbus.Interface(data['conn'],
        'org.laptop.Telepathy.BuddyInfo')

    call_async(data['test'], data['conn_iface'], 'RequestHandles', 1,
        ['bob@localhost'])

    return True

@match('dbus-return', method='RequestHandles')
def expect_request_handles_return(event, data):
    handles = event.value[0]
    bob_handle = handles[0]

    call_async(data['test'], data['buddy_info_iface'], 'GetProperties',
            bob_handle)

    return True

@match('stream-iq', to='bob@localhost',
    query_ns=NS_PUBSUB)
def expect_get_buddy_info_pubsub_query(event, data):
    query = event.stanza
    assert query['to'] == 'bob@localhost'

    # send an error as reply
    reply = IQ(data['stream'], 'error')
    reply['id'] = query['id']
    reply['to'] = 'alice@localhost'
    reply['from'] = 'bob@localhost'
    data['stream'].send(reply)

    return True

@match('stream-iq', to='index.jabber.laptop.org',
    query_ns=NS_OLPC_BUDDY)
def expect_search_buddy_query(event, data):
    buddies = xpath.queryForNodes('/iq/query/buddy', event.stanza)
    assert len(buddies) == 1
    buddy = buddies[0]
    assert buddy['jid'] == 'bob@localhost'

    # send reply to the search query
    reply = make_result_iq(data['stream'], event.stanza)
    reply['from'] = 'index.jabber.laptop.org'
    reply['to'] = 'alice@localhost'
    query = xpath.queryForNodes('/iq/query', reply)[0]
    buddy = query.addElement((None, "buddy"))
    buddy['jid'] = 'bob@localhost'
    properties = buddy.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#005FE4,#00A0FF')
    data['stream'].send(reply)

    return True

@match('dbus-return', method='GetProperties')
def expect_get_properties_return(event, data):
    props = event.value[0]

    assert props == {'color': '#005FE4,#00A0FF' }
    return True

if __name__ == '__main__':
    go()
