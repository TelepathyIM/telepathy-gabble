"""
test OLPC Buddy properties change notifications
"""
# FIXME: merge this file to other tests ?

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath

from util import announce_gadget, send_buddy_changed_properties_msg

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
    announce_gadget(q, stream, disco_event.stanza)

    # Alice, one our friends changed her properties
    send_buddy_changed_properties_msg(stream, 'alice@localhost',
            {'color': ('str', '#005FE4,#00A0FF')})

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    contact = event.args[0]
    props = event.args[1]

    assert props == {'color' : '#005FE4,#00A0FF'}

if __name__ == '__main__':
    exec_test(test)
