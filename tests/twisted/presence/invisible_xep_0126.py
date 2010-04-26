"""
A simple smoke-test for XEP-0126 invisibility

"""

from twisted.words.xish import domish

from gabbletest import exec_test, make_presence, XmppXmlStream, elem_iq
from servicetest import EventPattern, assertNotEquals
import ns
import constants as cs
from twisted.words.xish import domish, xpath
from invisible_helper import InvisibleXmlStream

class PrivacyListXmlStream(InvisibleXmlStream):
    FEATURES = [ns.PRIVACY]

def test_invisible_on_connect(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    create_list, set_active = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'))

    q.unforbid_events([presence_event_pattern])

    assertNotEquals (xpath.queryForNodes('/query/list/item/presence-out',
                                         create_list.query), [])

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test(q, bus, conn, stream):
    conn.Connect()

    event, _ = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

    assert ("hidden" in conn.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses",
                                 dbus_interface=cs.PROPERTIES_IFACE).keys())

    conn.SimplePresence.SetPresence("hidden", "")

    p1, event, p2 = q.expect_many(
        EventPattern('stream-presence', presence_type='unavailable'),
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('stream-presence'))

    active = xpath.queryForNodes('//active', event.query)[0]
    assert (active.compareAttribute('name', 'invisible'))

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'hidden': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (5, 'hidden', '')}]))

    conn.SimplePresence.SetPresence("away", "gone")

    event, p2 = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('stream-presence'))

    active = xpath.queryForNodes('//active', event.query)[0]
    assert (not active.compareAttribute('name', 'invisible'))

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'away': {'message': 'gone'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (3, 'away', 'gone')}]))

if __name__ == '__main__':
    exec_test(test, protocol=PrivacyListXmlStream)
    exec_test(test_invisible_on_connect, protocol=PrivacyListXmlStream)
