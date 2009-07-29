"""
test OLPC search activity
"""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq
import constants as cs
from util import announce_gadget, gadget_publish
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    props = conn.GetAll(
            'org.laptop.Telepathy.Gadget',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert props['GadgetAvailable'] == False

    announce_gadget(q, stream, disco_event.stanza)

    q.expect('dbus-signal', signal='GadgetDiscovered')

    props = conn.GetAll(
            'org.laptop.Telepathy.Gadget',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert props['GadgetAvailable'] == True

    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    # All the code has been moved to util.py:gadget_publish
    gadget_publish(q, stream, conn, True)
    gadget_publish(q, stream, conn, False)

    q.expect('stream-presence', presence_type='unsubscribed'),

if __name__ == '__main__':
    exec_test(test)
