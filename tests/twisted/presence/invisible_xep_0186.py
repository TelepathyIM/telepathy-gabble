"""
A simple smoke-test for XEP-0186 invisibility

"""

from gabbletest import exec_test, acknowledge_iq, send_error_reply
from servicetest import (
    EventPattern, assertEquals, assertNotEquals, assertContains,
)
import ns
import constants as cs
from invisible_helper import Xep0186XmlStream

def test_invisible_on_connect(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    event = q.expect('stream-iq', query_name='invisible')
    acknowledge_iq(stream, event.stanza)

    q.unforbid_events([presence_event_pattern])

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_invisible_on_connect_fails(q, bus, conn, stream):
    conn.SimplePresence.SetPresence("hidden", "")
    conn.Connect()

    event = q.expect('stream-iq', query_name='invisible')
    send_error_reply(stream, event.stanza)

    # Well this is upsetting. We recover by setting ourselves to DND and 
    acknowledge_iq(stream, event.stanza)

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    assertContains("hidden",
        conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses"))

    conn.SimplePresence.SetPresence("hidden", "")

    # First we send an <invisible/> command.
    event = q.expect('stream-iq', query_name='invisible')
    acknowledge_iq(stream, event.stanza)

    # When that's returned successfully, we can signal the change on D-Bus.
    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'hidden': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (5, 'hidden', '')}]))

    conn.SimplePresence.SetPresence("away", "gone")

    # First Gabble sends a <visible/> command.
    event = q.expect('stream-iq', query_name='visible')
    acknowledge_iq(stream, event.stanza)

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
    exec_test(test_invisible_on_connect, protocol=Xep0186XmlStream)
    exec_test(test, protocol=Xep0186XmlStream)
