# vim: set fileencoding=utf-8 : Python sucks!
"""
Utility functions for tests that need to interact with MUCs.
"""

import dbus

from twisted.words.xish import domish, xpath

from servicetest import call_async, wrap_channel, EventPattern, assertLength
from gabbletest import make_muc_presence, request_muc_handle

import constants as cs
import ns


def echo_muc_presence (q, stream, stanza, affiliation, role):
    x = stanza.addElement((ns.MUC_USER, 'x'))
    stanza['from'] = stanza['to']
    del stanza['to']

    item = x.addElement('item')
    item['affiliation'] = affiliation
    item['role'] = role

    stream.send (stanza)

def try_to_join_muc(q, bus, conn, stream, muc, request=None):
    """Ask Gabble to join a MUC, and expect it to send <presence/>"""

    if request is None:
        request = {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: muc,
        }

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)
    call_async(q, requests, 'CreateChannel',
        dbus.Dictionary(request, signature='sv'))

    join_event = q.expect('stream-presence', to='%s/test' % muc)
    # XEP-0045 §7.1.2 sez:
    #   “MUC clients SHOULD signal their ability to speak the MUC protocol by
    #   including in the initial presence stanza an empty <x/> element
    #   qualified by the 'http://jabber.org/protocol/muc' namespace.”
    x_muc_nodes = xpath.queryForNodes('/presence/x[@xmlns="%s"]' % ns.MUC,
        join_event.stanza)
    assertLength(1, x_muc_nodes)

def join_muc(q, bus, conn, stream, muc, request=None,
        also_capture=[], role='participant'):
    """
    Joins 'muc', returning the muc's handle, a proxy object for the channel,
    its path and its immutable properties just after the CreateChannel event
    has fired. The room contains one other member.
    """
    muc_handle = request_muc_handle(q, conn, stream, muc)
    try_to_join_muc(q, bus, conn, stream, muc, request=request)

    # Send presence for other member of room.
    stream.send(make_muc_presence('owner', 'moderator', muc, 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence('none', role, muc, 'test'))

    captured = q.expect_many(
            EventPattern('dbus-return', method='CreateChannel'),
            *also_capture)
    path, props = captured[0].value
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Messages'])

    return (muc_handle, chan, path, props) + tuple(captured[1:])

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
