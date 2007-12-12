"""
test OLPC search buddy
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"
NS_PUBSUB = "http://jabber.org/protocol/pubsub"

NS_AMP = "http://jabber.org/protocol/amp"

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')

    call_async(q, conn, 'RequestHandles', 1, ['bob@localhost'])

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]
    bob_handle = handles[0]

    call_async(q, buddy_info_iface, 'GetProperties', bob_handle)

    # wait for pubsub query
    event = q.expect('stream-iq', to='bob@localhost', query_ns=NS_PUBSUB)
    query = event.stanza
    assert query['to'] == 'bob@localhost'

    # send an error as reply
    reply = IQ(stream, 'error')
    reply['id'] = query['id']
    reply['to'] = 'alice@localhost'
    reply['from'] = 'bob@localhost'
    stream.send(reply)

    # wait for buddy search query
    event = q.expect('stream-iq', to='index.jabber.laptop.org',
            query_ns=NS_OLPC_BUDDY)
    buddies = xpath.queryForNodes('/iq/query/buddy', event.stanza)
    assert len(buddies) == 1
    buddy = buddies[0]
    assert buddy['jid'] == 'bob@localhost'

    # send reply to the search query
    reply = make_result_iq('stream', event.stanza)
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
    stream.send(reply)

    event = q.expect('dbus-return', method='GetProperties')
    props = event.value[0]

    assert props == {'color': '#005FE4,#00A0FF' }

if __name__ == '__main__':
    exec_test(test)
