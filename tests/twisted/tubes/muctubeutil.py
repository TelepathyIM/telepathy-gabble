import dbus

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import make_result_iq, make_muc_presence
import constants as cs

def get_muc_tubes_channel(q, bus, conn, stream, muc_jid, anonymous=True):
    """
    Returns a singleton list containing the MUC's handle, a proxy for the Tubes
    channel, and a proxy for the Tubes iface on that channel.
    """
    muc_server = muc_jid.split('@')[1]
    test_jid = muc_jid + "/test"
    bob_jid = muc_jid + "/bob"

    self_handle = conn.GetSelfHandle()
    self_name = conn.InspectHandles(cs.HT_CONTACT, [self_handle])[0]

    call_async(q, conn, 'RequestHandles', cs.HT_ROOM, [muc_jid])

    event = q.expect('stream-iq', to=muc_server,
            query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]
    room_handle = handles[0]

    # request tubes channel
    call_async(q, conn, 'RequestChannel',
        tp_name_prefix + '.Channel.Type.Tubes', cs.HT_ROOM, room_handle, True)

    _, stream_event = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to=test_jid))

    # Send presence for other member of room.
    if not anonymous:
        real_jid = 'bob@localhost'
    else:
        real_jid = None

    stream.send(make_muc_presence('owner', 'moderator', muc_jid, 'bob', real_jid))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', muc_jid, 'test'))

    q.expect('dbus-signal', signal='MembersChanged',
            args=[u'', [2, 3], [], [], [], 0, 0])

    assert conn.InspectHandles(cs.HT_CONTACT, [2]) == [test_jid]
    assert conn.InspectHandles(cs.HT_CONTACT, [3]) == [bob_jid]

    # text and tubes channels are created
    # FIXME: We can't check NewChannel signals (old API) because two of them
    # would be fired and we can't catch twice the same signals without specifying
    # all their arguments.
    new_sig, returned = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-return', method='RequestChannel'))

    channels = new_sig.args[0]
    assert len(channels) == 2

    for channel in channels:
        path, props = channel
        type = props[cs.CHANNEL_TYPE]

        if type == cs.CHANNEL_TYPE_TEXT:
            # check text channel properties
            assert props[cs.TARGET_HANDLE] == room_handle
            assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
            assert props[cs.TARGET_ID] == 'chat@conf.localhost'
            assert props[cs.REQUESTED] == False
            assert props[cs.INITIATOR_HANDLE] == self_handle
            assert props[cs.INITIATOR_ID] == self_name
        elif type == cs.CHANNEL_TYPE_TUBES:
            # check tubes channel properties
            assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
            assert props[cs.TARGET_HANDLE] == room_handle
            assert props[cs.TARGET_ID] == 'chat@conf.localhost'
            assert props[cs.REQUESTED] == True
            assert props[cs.INITIATOR_HANDLE] == self_handle
            assert props[cs.INITIATOR_ID] == self_name
        else:
            assert True

    tubes_chan = bus.get_object(conn.bus_name, returned.value[0])
    tubes_iface = dbus.Interface(tubes_chan,
            tp_name_prefix + '.Channel.Type.Tubes')

    return (room_handle, tubes_chan, tubes_iface)
