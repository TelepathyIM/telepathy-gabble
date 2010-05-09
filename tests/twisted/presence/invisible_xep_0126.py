# coding=utf-8
"""
A simple smoke-test for XEP-0126 invisibility
"""

from twisted.words.xish import domish

from gabbletest import exec_test, make_presence, XmppXmlStream, elem_iq
from servicetest import (
    EventPattern, assertEquals, assertNotEquals, assertContains,
)
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

    assertContains("hidden",
        conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses"))

    conn.SimplePresence.SetPresence("hidden", "")

    # §3.5 Become Globally Invisible
    #   <http://xmpp.org/extensions/xep-0126.html#invis-global>
    #
    # First, the user sends unavailable presence for broadcasting to all
    # contacts:
    q.expect('stream-presence', to=None, presence_type='unavailable')

    # Second, the user sets as active the global invisibility list previously
    # defined:
    event = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    active = xpath.queryForNodes('//active', event.query)[0]
    assertEquals('invisible', active['name'])

    # In order to appear globally invisible, the client MUST now re-send the
    # user's presence for broadcasting to all contacts, which the active rule
    # will block to all contacts:
    q.expect('stream-presence', to=None, presence_type=None)

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'hidden': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (5, 'hidden', '')}]))

    conn.SimplePresence.SetPresence("away", "gone")


    # §3.3 Become Globally Visible
    #   <http://xmpp.org/extensions/xep-0126.html#vis-global>
    #
    # Because globally allowing outbound presence notifications is most likely
    # the default behavior of any server, a more straightforward way to become
    # globally visible is to decline the use of any active rule (the
    # equivalent, as it were, of taking off a magic invisibility ring):
    event = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    active = xpath.queryForNodes('//active', event.query)[0]
    assert (not active.compareAttribute('name', 'invisible'))

    # In order to ensure synchronization of presence notifications, the client
    # SHOULD now re-send the user's presence for broadcasting to all contacts.
    #
    # At this point, we also signal our presence change on D-Bus:
    q.expect_many(
        EventPattern('stream-presence', to=None, presence_type=None),
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'away': {'message': 'gone'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (3, 'away', 'gone')}]))

if __name__ == '__main__':
    exec_test(test, protocol=PrivacyListXmlStream)
    exec_test(test_invisible_on_connect, protocol=PrivacyListXmlStream)
