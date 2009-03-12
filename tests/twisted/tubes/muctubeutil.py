import dbus

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import make_result_iq, acknowledge_iq, make_muc_presence

from twisted.words.xish import domish, xpath

def get_muc_tubes_channel(q, bus, conn, stream, muc_jid):
    """
    Returns a singleton list containing the MUC's handle, a proxy for the Tubes
    channel, and a proxy for the Tubes iface on that channel.
    """
    muc_server = muc_jid.split('@')[1]
    test_jid = muc_jid + "/test"
    bob_jid = muc_jid + "/bob"

    call_async(q, conn, 'RequestHandles', 2, [muc_jid])

    event = q.expect('stream-iq', to=muc_server,
            query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]

    # request tubes channel
    call_async(q, conn, 'RequestChannel',
        tp_name_prefix + '.Channel.Type.Tubes', 2, handles[0], True)

    _, stream_event = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to=test_jid))

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', muc_jid, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', muc_jid, 'test'))

    q.expect('dbus-signal', signal='MembersChanged',
            args=[u'', [2, 3], [], [], [], 0, 0])

    assert conn.InspectHandles(1, [2]) == [test_jid]
    assert conn.InspectHandles(1, [3]) == [bob_jid]

    event = q.expect('dbus-return', method='RequestChannel')

    tubes_chan = bus.get_object(conn.bus_name, event.value[0])
    tubes_iface = dbus.Interface(tubes_chan,
            tp_name_prefix + '.Channel.Type.Tubes')

    return (handles, tubes_chan, tubes_iface)
