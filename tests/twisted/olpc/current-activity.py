"""
test OLPC Buddy properties current activity
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq
from twisted.words.protocols.jabber.client import IQ

from twisted.words.xish import domish, xpath

from util import announce_gadget, send_buddy_changed_current_act_msg,\
    answer_to_current_act_pubsub_request, answer_error_to_pubsub_request

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

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=NS_DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)
    announce_gadget(q, stream, disco_event.stanza)

    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    handles = {}

    # Alice is one of our friend so we receive her PEP notifications
    handles['alice'] = conn.RequestHandles(1, ['alice@localhost'])[0]

    # Try to get Alice's currrent-activity
    call_async(q, buddy_info_iface, "GetCurrentActivity", handles['alice'])

    # Alice's current-activity is not in the cache so Gabble sends a PEP query
    event = q.expect('stream-iq', iq_type='get', query_name='pubsub')
    answer_to_current_act_pubsub_request(stream, event.stanza, 'activity1',
        'room1@conference.localhost')

    event = q.expect('dbus-return', method='GetCurrentActivity')
    id, handles['room1'] = event.value
    assert id == 'activity1'
    assert conn.InspectHandles(2, [handles['room1']]) == \
            ['room1@conference.localhost']

    # Retry to get Alice's current-activity
    # Alice's current-activity is now in the cache so Gabble doesn't
    # send PEP query
    assert buddy_info_iface.GetCurrentActivity(handles['alice']) == \
            ('activity1', handles['room1'])

    # Alice changed her current-activity
    send_buddy_changed_current_act_msg(stream, 'alice@localhost', 'activity2',
            'room2@conference.localhost')

    event = q.expect('dbus-signal', signal='CurrentActivityChanged')
    contact, id, handles['room2'] = event.args
    assert contact == handles['alice']
    assert id == 'activity2'
    assert conn.InspectHandles(2, [handles['room2']]) == \
            ['room2@conference.localhost']

    # Get Alice's current-activity as the cache have to be updated
    assert buddy_info_iface.GetCurrentActivity(handles['alice']) == \
            ('activity2', handles['room2'])

    # request a activity view containing only Bob and one
    # activity in it.
    call_async(q, gadget_iface, 'RequestRandomActivities', 1)

    # TODO: would be cool to have to view test helper code
    iq_event, return_event = q.expect_many(
    EventPattern('stream-iq', to='gadget.localhost',
        query_ns=NS_OLPC_ACTIVITY),
    EventPattern('dbus-return', method='RequestRandomActivities'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '0'
    random = xpath.queryForNodes('/iq/view/random', iq_event.stanza)
    assert len(random) == 1
    assert random[0]['max'] == '1'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'test@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    activity = view.addElement((None, "activity"))
    activity['room'] = 'room3@conference.localhost'
    activity['id'] = 'activity3'
    buddy = activity.addElement((None, 'buddy'))
    buddy['jid'] = 'bob@localhost'
    stream.send(reply)

    # Gadget sends us a current-activity change concerning a
    # known activity
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'

    change = message.addElement((NS_OLPC_BUDDY, 'change'))
    change['jid'] = 'bob@localhost'
    change['id'] = '0'
    activity = change.addElement((NS_OLPC_CURRENT_ACTIVITY, 'activity'))
    activity['type'] = 'activity3'
    activity['room'] = 'room3@conference.localhost'

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    # Gadget notifies us about the change
    event = q.expect('dbus-signal', signal='CurrentActivityChanged')
    handles['bob'], id, handles['room3'] = event.args
    assert id == 'activity3'
    assert conn.InspectHandles(1, [handles['bob']]) == \
            ['bob@localhost']
    assert conn.InspectHandles(2, [handles['room3']]) == \
            ['room3@conference.localhost']

    # And the cache was properly updated
    assert buddy_info_iface.GetCurrentActivity(handles['bob']) == \
            ('activity3', handles['room3'])

    # Gadget sends us a current-activity change concerning an
    # unknown activity
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'

    change = message.addElement((NS_OLPC_BUDDY, 'change'))
    change['jid'] = 'bob@localhost'
    change['id'] = '0'
    activity = change.addElement((NS_OLPC_CURRENT_ACTIVITY, 'activity'))
    activity['type'] = 'activity4'
    activity['room'] = 'room4@conference.localhost'

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    # Gadget changed Alice's current-activity to none as it doesn't
    # know the activity
    event = q.expect('dbus-signal', signal='CurrentActivityChanged',
            args=[handles['bob'], '', 0])

    call_async(q, buddy_info_iface, "GetCurrentActivity", handles['bob'])

    # Bob's current-activity is not in the cache anymore so Gabble try
    # to send a PEP query
    event = q.expect('stream-iq', iq_type='get', query_name='pubsub')
    iq = event.stanza

    # Alice is not Bob's friend so she can't query his PEP node
    answer_error_to_pubsub_request(stream, iq, NS_OLPC_CURRENT_ACTIVITY)

    # so Bob is considererd without current activity
    q.expect('dbus-return', method='GetCurrentActivity',
            value=('', 0))

if __name__ == '__main__':
    exec_test(test)
