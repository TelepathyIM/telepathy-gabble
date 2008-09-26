"""
test OLPC search activity
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
from util import announce_gadget, request_random_activity_view, elem

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"

NS_PUBSUB = "http://jabber.org/protocol/pubsub"
NS_DISCO_INFO = "http://jabber.org/protocol/disco#info"
NS_DISCO_ITEMS = "http://jabber.org/protocol/disco#items"

NS_AMP = "http://jabber.org/protocol/amp"
NS_STANZA = "urn:ietf:params:xml:ns:xmpp-stanzas"

def test(q, bus, conn, stream):
    conn.Connect()

    handles = {}

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=NS_DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)
    announce_gadget(q, stream, disco_event.stanza)

    activity_prop_iface = dbus.Interface(conn,
            'org.laptop.Telepathy.ActivityProperties')
    buddy_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    sync_stream(q, stream)

    # request 3 random activities (view 0)
    view_path = request_random_activity_view(q, stream, conn, 3, '0',
            [('activity1', 'room1@conference.localhost',
                {'color': ('str', '#005FE4,#00A0FF')},
                [('lucien@localhost', {'color': ('str', '#AABBCC,#CCBBAA')}),
                 ('jean@localhost', {})]),])

    view0 = bus.get_object(conn.bus_name, view_path)
    view0_iface = dbus.Interface(view0, 'org.laptop.Telepathy.View')

    # Gadget is restarted so send us a new presence stanza
    presence = elem('presence', from_='gadget.localhost', to='test@localhost')
    stream.send(presence)

    q.expect('dbus-signal', signal='Closed', interface='org.laptop.Telepathy.View')

if __name__ == '__main__':
    exec_test(test)
