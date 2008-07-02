"""
test gadget invitation when an activity becomes public
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

from olpc_test_helper import announce_gadget

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

def join_channel(name, q, conn, stream):
    call_async(q, conn, 'RequestHandles', 2, [name])

    # announce conference service
    event = q.expect('stream-iq', to='conference.localhost', query_ns=NS_DISCO_INFO)
    reply = make_result_iq(stream, event.stanza)
    feature = reply.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(reply)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]

    call_async(q, conn, 'RequestChannel',
            'org.freedesktop.Telepathy.Channel.Type.Text', 2, handles[0], True)

    event = q.expect('stream-presence', to='myroom@conference.localhost/test')
    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'myroom@conference.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

    event = q.expect('dbus-return', method='RequestChannel')
    return handles[0], event.value[0]

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=NS_DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)
    announce_gadget(q, stream, disco_event.stanza)

    act_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.ActivityProperties')
    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')

    q.expect_many(EventPattern('dbus-signal', signal='BuddyGadgetDiscovered'),
            EventPattern('dbus-signal', signal='ActivityGadgetDiscovered'))

    # join a room
    room_handle, room_path = join_channel('myroom@conference.localhost',
            q, conn, stream)

    call_async (q, buddy_info_iface, 'SetActivities', [("roomid", room_handle)])

    # pubsub activity iq
    event = q.expect('stream-iq')
    acknowledge_iq(stream, event.stanza)

    event = q.expect('dbus-return', method='SetActivities')

    # make activity public
    call_async(q, act_prop_iface, 'SetProperties',
            1, {'title': 'My test activity', 'private': False})

    # pseudo invite
    event = q.expect('stream-message', to='gadget.localhost')
    message = event.stanza
    properties = xpath.queryForNodes('/message/properties', message)
    assert (properties is not None and len(properties) == 1), repr(properties)
    assert properties[0].uri == 'http://laptop.org/xmpp/activity-properties'
    assert properties[0]['room'] == 'myroom@conference.localhost'
    assert properties[0]['activity'] == 'roomid'

    # invite
    event = q.expect('stream-message', to='myroom@conference.localhost')
    message = event.stanza
    x = xpath.queryForNodes('/message/x', message)
    assert (x is not None and len(x) == 1), repr(x)
    assert x[0].uri == 'http://jabber.org/protocol/muc#user'

    invites = xpath.queryForNodes('/x/invite', x[0])
    assert (invites is not None and len(invites) == 1), repr(invites)
    assert invites[0]['to'] == 'gadget.localhost'

    # pubsub activity prop iq
    event = q.expect('stream-iq')
    acknowledge_iq(stream, event.stanza)

    # pubsub activity iq
    event = q.expect('stream-iq')
    acknowledge_iq(stream, event.stanza)


    event = q.expect('dbus-return', method='SetProperties')

if __name__ == '__main__':
    exec_test(test)
