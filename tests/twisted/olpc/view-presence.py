"""
test contact presence from views
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

from util import (announce_gadget, properties_to_xml, parse_properties,
    create_gadget_message, close_view)

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

tp_name_prefix = 'org.freedesktop.Telepathy'
olpc_name_prefix = 'org.laptop.Telepathy'

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=NS_DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')
    requests_iface = dbus.Interface(conn, tp_name_prefix + '.Connection.Interface.Requests')
    simple_presence_iface = dbus.Interface(conn, tp_name_prefix + '.Connection.Interface.SimplePresence')

    announce_gadget(q, stream, disco_event.stanza)
    sync_stream(q, stream)

    # receive presence from Charles
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'charles@localhost'
    show = presence.addElement((None, 'show'))
    show.addContent('dnd')
    status = presence.addElement((None, 'status'))
    status.addContent('Hacking on Sugar')
    stream.send(presence)

    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    handles = {}
    handles['bob'], handles['charles'] = conn.RequestHandles(1, ['bob@localhost', 'charles@localhost'])

    presence = event.args[0]
    # Connection_Presence_Type_Busy = 6
    assert presence[handles['charles']] == (6, 'dnd', 'Hacking on Sugar')

    # request 3 random buddies
    call_async(q, requests_iface, 'CreateChannel',
        { tp_name_prefix + '.Channel.ChannelType':
            olpc_name_prefix + '.Channel.Type.BuddyView',
          olpc_name_prefix + '.Channel.Interface.View.MaxSize': 3
          })

    iq_event, return_event, new_channels_event, new_channel_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=NS_OLPC_BUDDY),
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='NewChannel'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '1'
    assert view['size'] == '3'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'charles@localhost'
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'bob@localhost'
    stream.send(reply)

    view_path = return_event.value[0]
    props = return_event.value[1]
    view1 = bus.get_object(conn.bus_name, view_path)

    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    # Only Bob's presence is changed as we received a presence from Charles
    presence = event.args[0]
    # Connection_Presence_Type_Available = 2
    assert presence[handles['bob']] == (2, 'available', '')
    assert len(presence) == 1

    # Charles's presence didn't change
    presence = simple_presence_iface.GetPresences([handles['charles']])
    assert presence[handles['charles']] == (6, 'dnd', 'Hacking on Sugar')

    event = q.expect('dbus-signal', signal='BuddiesChanged')
    added, removed = event.args
    assert removed == []
    assert len(added) == 2
    assert sorted(conn.InspectHandles(1, added)) == ['bob@localhost',
            'charles@localhost']

    # remove bob from view
    message = create_gadget_message("test@localhost")
    added = message.addElement((NS_OLPC_BUDDY, 'removed'))
    added['id'] = '1'
    buddy = added.addElement((None, 'buddy'))
    buddy['jid'] = 'bob@localhost'
    stream.send(message)

    event = q.expect('dbus-signal', signal='BuddiesChanged')

    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    presence = event.args[0]
    # Connection_Presence_Type_Offline = 1
    assert presence[handles['bob']] == (1, 'offline', '')

if __name__ == '__main__':
    exec_test(test)
