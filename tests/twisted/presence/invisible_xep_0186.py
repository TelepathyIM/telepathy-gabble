"""
A simple smoke-test for XEP-0186 invisibility

"""

from gabbletest import exec_test
from servicetest import (
    EventPattern, assertEquals, assertNotEquals, assertContains,
)
import ns
import constants as cs
from invisible_helper import InvisibleXmlStream

def test_invisible_on_connect(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    q.expect('stream-iq', query_name='invisible')

    q.unforbid_events([presence_event_pattern])

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    assertContains("hidden",
        conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses"))

    conn.SimplePresence.SetPresence("hidden", "")

    # First we send an <invisible/> command.
    q.expect('stream-iq', query_name='invisible')
    # (acked by InvisibleXmlStream)

    # When that's returned successfully, we can signal the change on D-Bus.
    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'hidden': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (5, 'hidden', '')}]))

    conn.SimplePresence.SetPresence("away", "gone")

    # First Gabble sends a <visible/> command.
    q.expect('stream-iq', query_name='visible')
    # (acked by InvisibleXmlStream)

    # Then: "It is the responsibility of the client to send an undirected
    # presence notification to the server". Plus, we should signal the change
    # on D-Bus.
    q.expect_many(
        EventPattern('stream-presence', to=None),
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'away': {'message': 'gone'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (3, 'away', 'gone')}]))


if __name__ == '__main__':
    exec_test(test_invisible_on_connect, protocol=InvisibleXmlStream)
    exec_test(test, protocol=InvisibleXmlStream)
