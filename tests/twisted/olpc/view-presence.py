"""
test contact presence from views
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream
import constants as cs

from twisted.words.xish import xpath

from util import announce_gadget, create_gadget_message, elem
import ns

tp_name_prefix = 'org.freedesktop.Telepathy'
olpc_name_prefix = 'org.laptop.Telepathy'

def send_presence(stream, from_, type, msg):
    presence = elem('presence', from_=from_)(
        elem('show')(unicode(type)),
        elem('status')(unicode(msg)))
    stream.send(presence)

def remove_buddy_from_view(stream, id, jid):
    message = create_gadget_message("test@localhost")
    added = message.addElement((ns.OLPC_BUDDY, 'removed'))
    added['id'] = id
    buddy = added.addElement((None, 'buddy'))
    buddy['jid'] = jid
    stream.send(message)

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')
    requests_iface = dbus.Interface(conn, tp_name_prefix + '.Connection.Interface.Requests')
    simple_presence_iface = dbus.Interface(conn, tp_name_prefix + '.Connection.Interface.SimplePresence')

    announce_gadget(q, stream, disco_event.stanza)
    sync_stream(q, stream)

    # receive presence from Charles
    send_presence(stream, 'charles@localhost/Telepathy', 'dnd', 'Hacking on Sugar')

    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    # get contacts handles
    contacts = ['bob', 'charles', 'damien', 'eric', 'fred']
    tmp = conn.RequestHandles(1, map(lambda x: "%s@localhost" % x, contacts))
    handles = dict(zip(contacts, tmp))

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
            query_ns=ns.OLPC_BUDDY),
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
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'damien@localhost'
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'eric@localhost'
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'fred@localhost'
    stream.send(reply)

    view_path = return_event.value[0]
    props = return_event.value[1]
    view1 = bus.get_object(conn.bus_name, view_path)

    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    # Only Bob and Damien presences are changed as we received a presence from Charles
    presence = event.args[0]
    assert len(presence) == 4
    # Connection_Presence_Type_Available = 2
    assert presence[handles['bob']] == (2, 'available', '')
    assert presence[handles['damien']] == (2, 'available', '')
    assert presence[handles['eric']] == (2, 'available', '')
    assert presence[handles['fred']] == (2, 'available', '')

    # Charles's presence didn't change
    presence = simple_presence_iface.GetPresences([handles['charles']])
    assert presence[handles['charles']] == (6, 'dnd', 'Hacking on Sugar')

    event = q.expect('dbus-signal', signal='BuddiesChanged')

    # remove bob from view
    remove_buddy_from_view(stream, '1', 'bob@localhost')
    event = q.expect('dbus-signal', signal='BuddiesChanged')

    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    presence = event.args[0]
    # Connection_Presence_Type_Unknown = 7
    assert presence[handles['bob']] == (7, 'unknown', '')

    # remove charles from view
    remove_buddy_from_view(stream, '1', 'charles@localhost')
    event = q.expect('dbus-signal', signal='BuddiesChanged')

    # Charles's presence didn't change
    presence = simple_presence_iface.GetPresences([handles['charles']])
    assert presence[handles['charles']] == (6, 'dnd', 'Hacking on Sugar')

    # we receive a presence from Dambien
    send_presence(stream, 'damien@localhost/Telepathy', 'away', 'Watching pr0n')

    # presence is properly changed
    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    presence = event.args[0]
    # Connection_Presence_Type_Away = 3
    assert presence[handles['damien']] == (3, 'away', 'Watching pr0n')

    # remove Damien from view
    remove_buddy_from_view(stream, '1', 'damien@localhost')
    event = q.expect('dbus-signal', signal='BuddiesChanged')

    # Damien's presence didn't change
    presence = simple_presence_iface.GetPresences([handles['damien']])
    assert presence[handles['damien']] == (3, 'away', 'Watching pr0n')

    # request a second view
    call_async(q, requests_iface, 'CreateChannel',
        { tp_name_prefix + '.Channel.ChannelType':
            olpc_name_prefix + '.Channel.Type.BuddyView',
          olpc_name_prefix + '.Channel.Interface.View.MaxSize': 1
          })

    iq_event, return_event, new_channels_event, new_channel_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=ns.OLPC_BUDDY),
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='NewChannel'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '2'
    assert view['size'] == '1'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    buddy = view.addElement((None, "buddy"))
    buddy['jid'] = 'fred@localhost'
    stream.send(reply)

    view_path = return_event.value[0]
    props = return_event.value[1]
    view2 = bus.get_object(conn.bus_name, view_path)

    event = q.expect('dbus-signal', signal='BuddiesChanged')

    # Fred is the only buddy in this view
    buddies = view2.Get('org.laptop.Telepathy.Channel.Interface.View', 'Buddies',
        dbus_interface=dbus.PROPERTIES_IFACE)
    assert buddies == [handles['fred']]

    # ... and his presence didn't change
    presence = simple_presence_iface.GetPresences([handles['fred']])
    assert presence[handles['fred']] == (2, 'available', '')

    # close view 1
    view1.Close(dbus_interface=cs.CHANNEL)

    # Eric's presence is changed as he's not in the view anymore
    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    presence = event.args[0]
    # Connection_Presence_Type_Unknown = 7
    assert presence[handles['eric']] == (7, 'unknown', '')

    # but not the presence of Fred as he's still in view2
    presence = simple_presence_iface.GetPresences([handles['fred']])
    assert presence[handles['fred']] == (2, 'available', '')

    # close view 2
    view2.Close(dbus_interface=cs.CHANNEL)

   # and now Fred is offline
    event, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('dbus-signal', signal='PresenceUpdate'))

    presence = event.args[0]
    # Connection_Presence_Type_Unknown = 7
    assert presence[handles['fred']] == (7, 'unknown', '')

if __name__ == '__main__':
    exec_test(test)
