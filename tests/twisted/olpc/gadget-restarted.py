"""
test OLPC search activity
"""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq, sync_stream
import constants as cs

from util import announce_gadget, request_random_activity_view, elem
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    handles = {}

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)
    announce_gadget(q, stream, disco_event.stanza)

    activity_prop_iface = dbus.Interface(conn,
            'org.laptop.Telepathy.ActivityProperties')
    buddy_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    sync_stream(q, stream)

    # request 3 random activities (view 0)
    view_path = request_random_activity_view(q, stream, conn, 3, '1',
            [('activity1', 'room1@conference.localhost',
                {'color': ('str', '#005FE4,#00A0FF')},
                [('lucien@localhost', {'color': ('str', '#AABBCC,#CCBBAA')}),
                 ('jean@localhost', {})]),])

    view0 = bus.get_object(conn.bus_name, view_path)
    view0_iface = dbus.Interface(view0, 'org.laptop.Telepathy.View')

    # Gadget is restarted so send us a new presence stanza
    presence = elem('presence', from_='gadget.localhost', to='test@localhost')
    stream.send(presence)

    q.expect('dbus-signal', signal='Closed', interface=cs.CHANNEL)

if __name__ == '__main__':
    exec_test(test)
