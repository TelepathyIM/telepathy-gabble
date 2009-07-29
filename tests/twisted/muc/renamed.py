"""
Test dealing with the server giving you a nick you didn't ask for.
"""

import dbus

from gabbletest import (
    exec_test, make_muc_presence, request_muc_handle
    )
from servicetest import call_async, unwrap
from constants import (
    HT_CONTACT, HT_ROOM,
    CONN_IFACE_REQUESTS, CHANNEL_TYPE_TEXT, CHANNEL_IFACE_GROUP,
    CHANNEL_TYPE, TARGET_HANDLE_TYPE, TARGET_HANDLE,
    )
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    self_handle = conn.GetSelfHandle()

    requests = dbus.Interface(conn, CONN_IFACE_REQUESTS)

    room_jid = 'chat@conf.localhost'
    room_handle = request_muc_handle(q, conn, stream, room_jid)

    call_async(q, requests, 'CreateChannel',
        dbus.Dictionary({ CHANNEL_TYPE: CHANNEL_TYPE_TEXT,
                          TARGET_HANDLE_TYPE: HT_ROOM,
                          TARGET_HANDLE: room_handle,
                        }, signature='sv'))

    expected_jid = '%s/%s' % (room_jid, 'test')

    q.expect('stream-presence', to=expected_jid)

    # Send presence for another member of the MUC
    stream.send(make_muc_presence('owner', 'moderator', room_jid, 'liz'))

    # This is a themed discussion, so the MUC server forces you to have an
    # appropriate name.
    self_presence = make_muc_presence('none', 'participant', room_jid, 'toofer')
    x = [elt for elt in self_presence.elements() if elt.name == 'x'][0]
    status = x.addElement('status')
    status['code'] = '110' # "This is you"
    status = x.addElement('status')
    status['code'] = '210' # "I renamed you. Muahaha."
    stream.send(self_presence)

    # Gabble should figure out from 110 that it's in the room, and from 210
    # that we've been renamed.
    event = q.expect('dbus-return', method='CreateChannel')
    path, props = event.value
    text_chan = bus.get_object(conn.bus_name, path)
    group_props = unwrap(text_chan.GetAll(CHANNEL_IFACE_GROUP,
        dbus_interface=dbus.PROPERTIES_IFACE))

    liz, toofer, expected_handle = conn.RequestHandles(HT_CONTACT,
        ["%s/%s" % (room_jid, m) for m in ['liz', 'toofer', 'test']])

    # Check that Gabble think our nickname in the room is toofer not test
    muc_self_handle = group_props['SelfHandle']
    assert muc_self_handle == toofer, (muc_self_handle, toofer, expected_handle)

    members = group_props['Members']

    # Check there are exactly two members (liz and toofer)
    expected_members = [liz, toofer]
    assert sorted(members) == sorted(expected_members), \
        (members, expected_members)

    # There should be no pending members.
    assert len(group_props['LocalPendingMembers']) == 0, group_props
    assert len(group_props['RemotePendingMembers']) == 0, group_props

    # Check that toofer's handle owner is us, and that liz has
    # no owner.
    handle_owners = group_props['HandleOwners']
    assert handle_owners[toofer] == self_handle, \
        (handle_owners, toofer, handle_owners[toofer], self_handle)
    assert handle_owners[liz] == 0, (handle_owners, liz)

if __name__ == '__main__':
    exec_test(test)
