"""
Utility functions for tests that need to interact with MUCs.
"""

import dbus

from servicetest import call_async, wrap_channel
from gabbletest import make_muc_presence, request_muc_handle

import constants as cs

def join_muc(q, bus, conn, stream, muc, request=None):
    """
    Joins 'muc', returning the muc's handle, a proxy object for the channel,
    its path and its immutable properties just after the CreateChannel event
    has fired. The room contains one other member.
    """
    if request is None:
        request = {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: muc,
        }

    muc_handle = request_muc_handle(q, conn, stream, muc)

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)
    call_async(q, requests, 'CreateChannel',
        dbus.Dictionary(request, signature='sv'))

    q.expect('stream-presence', to='%s/test' % muc)

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', muc, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', 'participant', muc, 'test'))

    event = q.expect('dbus-return', method='CreateChannel')
    path, props = event.value
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Messages'])

    return (muc_handle, chan, path, props)

def join_muc_and_check(q, bus, conn, stream, muc, request=None):
    """
    Like join_muc(), but also checks the NewChannels and NewChannel signals and
    the Members property, and returns both members' handles.
    """
    muc_handle, chan, path, props = \
        join_muc(q, bus, conn, stream, muc, request=request)

    q.expect('dbus-signal', signal='NewChannels', args=[[(path, props)]])
    q.expect('dbus-signal', signal='NewChannel',
        args=[path, cs.CHANNEL_TYPE_TEXT, cs.HT_ROOM, muc_handle, True])

    test_handle, bob_handle = conn.RequestHandles(cs.HT_CONTACT,
        ['%s/test' % muc, '%s/bob' % muc])

    members = chan.Get(cs.CHANNEL_IFACE_GROUP, 'Members',
        dbus_interface=cs.PROPERTIES_IFACE)
    assert set(members) == set([test_handle, bob_handle]), \
        (members, (test_handle, bob_handle))

    return (muc_handle, chan, test_handle, bob_handle)
