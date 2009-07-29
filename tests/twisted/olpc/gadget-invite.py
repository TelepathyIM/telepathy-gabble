"""
test gadget invitation when an activity becomes public
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream,\
    make_muc_presence, disconnect_conn
import constants as cs

from twisted.words.xish import xpath

from util import announce_gadget, gadget_publish
import ns

def join_channel(name, q, conn, stream):
    call_async(q, conn, 'RequestHandles', cs.HT_ROOM, [name])

    # announce conference service
    event = q.expect('stream-iq', to='conference.localhost', query_ns=ns.DISCO_INFO)
    reply = make_result_iq(stream, event.stanza)
    feature = reply.firstChildElement().addElement('feature')
    feature['var'] = ns.MUC
    stream.send(reply)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]

    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TEXT, cs.HT_ROOM,
        handles[0], True)

    event = q.expect('stream-presence', to='myroom@conference.localhost/test')
    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', 'myroom@conference.localhost', 'test'))

    event = q.expect('dbus-return', method='RequestChannel')
    return handles[0], event.value[0]

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)
    announce_gadget(q, stream, disco_event.stanza)

    act_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.ActivityProperties')
    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    simple_presence_iface = dbus.Interface(conn, 'org.freedesktop.Telepathy.Connection.Interface.SimplePresence')

    q.expect('dbus-signal', signal='GadgetDiscovered')
    gadget_publish(q, stream, conn, True)

    # join a room
    room_handle, room_path = join_channel('myroom@conference.localhost',
            q, conn, stream)

    call_async (q, buddy_info_iface, 'SetActivities', [("roomid", room_handle)])

    # pubsub activity iq
    event = q.expect('stream-iq', iq_type='set', query_name='pubsub',
        query_ns=ns.PUBSUB)
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
    assert properties[0].uri == ns.OLPC_ACTIVITY_PROPS
    assert properties[0]['room'] == 'myroom@conference.localhost'
    assert properties[0]['activity'] == 'roomid'

    # invite
    event = q.expect('stream-message', to='myroom@conference.localhost')
    message = event.stanza
    x = xpath.queryForNodes('/message/x', message)
    assert (x is not None and len(x) == 1), repr(x)
    assert x[0].uri == ns.MUC_USER

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

    # Gadget joins the room
    stream.send(make_muc_presence('none', 'participant', 'myroom@conference.localhost',
        'inspector', 'gadget.localhost'))

    muc = bus.get_object(conn.bus_name, room_path)
    muc_group = dbus.Interface(muc, "org.freedesktop.Telepathy.Channel.Interface.Group")

    sync_stream(q, stream)
    members = muc_group.GetMembers()
    assert conn.InspectHandles(1, members) == ['myroom@conference.localhost/test']

    # Regression test for a nasty bug caused by the presence cache handling
    # the inspector presence as a non-MUC one because the inspector is not
    # added to MUC's members.
    handle = conn.RequestHandles(1, ['myroom@conference.localhost'])[0]
    presence = simple_presence_iface.GetPresences([handle])
    assert presence[handle] == (7, 'unknown', '')

    # PEP activity update
    disconnect_conn(q, conn, stream, [EventPattern('stream-iq')])[0]

if __name__ == '__main__':
    exec_test(test)
