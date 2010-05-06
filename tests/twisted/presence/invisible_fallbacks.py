"""
Test different cases in which setting invisibility fails.

"""

from gabbletest import exec_test
from servicetest import EventPattern, assertNotEquals, assertContains
import ns
import constants as cs
from invisible_helper import InvisibleXmlStream

class BrokenInvisibleXmlStream(InvisibleXmlStream):
    RESPONDERS = {"/iq/invisible[@xmlns='urn:xmpp:invisible:0']" : False,
                  "/iq/visible[@xmlns='urn:xmpp:invisible:0']" : False,
                  "/iq/query[@xmlns='jabber:iq:privacy']" : True}

class ReallyBrokenInvisibleXmlStream(InvisibleXmlStream):
    RESPONDERS = {"/iq/invisible[@xmlns='urn:xmpp:invisible:0']" : False,
                  "/iq/visible[@xmlns='urn:xmpp:invisible:0']" : False,
                  "/iq/query[@xmlns='jabber:iq:privacy']" : False}


def test_error_reply(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    assertContains("hidden",
        conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses"))

    conn.SimplePresence.SetPresence("hidden", "")

    q.expect('stream-iq', query_name='invisible')

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'dnd': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1L: (6L, u'dnd', u'')}]))

    conn.SimplePresence.SetPresence("away", "gone")

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'away': {'message': 'gone'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (3, 'away', 'gone')}]))

def test_error_reply_initial(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    q.expect('stream-iq', query_name='invisible')

    create_list, set_active = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'))

    q.unforbid_events([presence_event_pattern])

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     args=[{1: (5, u'hidden', u'')}]))

def test_really_broken_initial(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    q.expect('stream-iq', query_name='invisible', query_ns=ns.INVISIBLE)

    q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     args=[{1: (6, u'dnd', u'')}]))

if __name__ == '__main__':
    exec_test(test_error_reply_initial, protocol=BrokenInvisibleXmlStream)
    exec_test(test_error_reply, protocol=BrokenInvisibleXmlStream)
    exec_test(test_really_broken_initial,
              protocol=ReallyBrokenInvisibleXmlStream)
