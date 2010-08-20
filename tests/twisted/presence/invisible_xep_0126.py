# coding=utf-8
"""
A simple smoke-test for XEP-0126 invisibility

We are deliberately not advertising support for privacy lists on
connection disco, this is because it is not explicitly required by
XEP-0016 or XEP-0126. Some servers silently support it, like certain
version of Ejabberd and all released versions of Prosody (as of 7.0).
"""
from gabbletest import (
    exec_test, XmppXmlStream, acknowledge_iq, send_error_reply,
    disconnect_conn, elem, elem_iq
)
from servicetest import (
    EventPattern, assertEquals, assertNotEquals, assertContains,
    assertDoesNotContain
)
import ns
import constants as cs
from twisted.words.xish import xpath, domish
from invisible_helper import send_privacy_list_push_iq, send_privacy_list, \
    Xep0126XmlStream

def handle_get_all_privacy_lists(q, bus, conn, stream, lists=[]):
    elem_list=[]
    for li in lists:
        elem_list.append(elem('list', name=li))

    e = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')
    iq = elem_iq(stream, "result", id=e.stanza["id"])(
        elem(ns.PRIVACY, 'query')(*elem_list))
    stream.send(iq)

def handle_set_active_list(q, bus, conn, stream):
    e = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    acknowledge_iq(stream, e.stanza)

def test_create_invisible_list(q, bus, conn, stream):
    conn.SimplePresence.SetPresence("away", "")

    conn.Connect()

    handle_get_all_privacy_lists(q, bus, conn, stream)

    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')
    list_node = xpath.queryForNodes('//list', get_list.query)[0]
    assertEquals('invisible', list_node['name'])

    error = domish.Element((None, 'error'))
    error['type'] = 'cancel'
    error.addElement((ns.STANZA, 'item-not-found'))
    send_error_reply (stream, get_list.stanza, error)

    create_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    list_node = xpath.queryForNodes('//list', create_list.query)[0]
    assertEquals('invisible', list_node['name'])
    assertNotEquals([],
        xpath.queryForNodes('/query/list/item/presence-out', create_list.query))
    acknowledge_iq(stream, create_list.stanza)

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    assertContains("hidden",
        conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses"))

def test_invisible_on_connect_fail_no_list(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    handle_get_all_privacy_lists(q, bus, conn, stream)

    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')
    list_node = xpath.queryForNodes('//list', get_list.query)[0]
    assertEquals('invisible', list_node['name'])

    send_error_reply(stream, get_list.stanza)

    q.unforbid_events([presence_event_pattern])

    # Darn! At least we should have our presence set to DND.
    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'dnd': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (6, 'dnd', '')}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

    # 'hidden' should not be an available status.
    assertDoesNotContain("hidden",
        conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses"))

def test_invisible_on_connect_fail_invalid_list(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    handle_get_all_privacy_lists(q, bus, conn, stream, ['invisible'])

    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')
    list_node = xpath.queryForNodes('//list', get_list.query)[0]
    assertEquals('invisible', list_node['name'])

    send_privacy_list (
        stream, get_list.stanza,
        [elem('item', type='jid', value='tybalt@example.com', action='allow',
             order='1')(elem('presence-out')),
        elem('item', action='deny', order='2')(elem('presence-out'))])

    create_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    created = xpath.queryForNodes('//list', create_list.stanza)[0]
    assertEquals(created["name"], 'invisible-gabble')
    acknowledge_iq (stream, create_list.stanza)

    q.unforbid_events([presence_event_pattern])

    activate_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    active = xpath.queryForNodes('//active', activate_list.stanza)[0]
    assertEquals(active["name"], 'invisible-gabble')
    acknowledge_iq (stream, activate_list.stanza)

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'hidden': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (5, 'hidden', '')}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

    # 'hidden' should not be an available status.
    assertContains("hidden",
        conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses"))

def test_invisible_on_connect_fail(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    handle_get_all_privacy_lists(q, bus, conn, stream)

    create_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    # Check its name
    assertNotEquals([],
        xpath.queryForNodes('/query/list/item/presence-out', create_list.query))
    acknowledge_iq(stream, create_list.stanza)

    set_active = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    active = xpath.queryForNodes('//active', set_active.query)[0]
    assertEquals('invisible', active['name'])
    send_error_reply(stream, set_active.stanza)

    q.unforbid_events([presence_event_pattern])

    # Darn! At least we should have our presence set to DND.
    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     args=[{1: (0, {'dnd': {}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (6, 'dnd', '')}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

def test_invisible_on_connect(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])

    presence_event_pattern = EventPattern('stream-presence')

    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    handle_get_all_privacy_lists(q, bus, conn, stream, ['invisible'])

    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')
    list_node = xpath.queryForNodes('//list', get_list.query)[0]
    assertEquals('invisible', list_node['name'])

    send_privacy_list (
        stream, get_list.stanza,
        [elem('item', action='deny', order='1')(elem('presence-out'))])

    set_active = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    active = xpath.queryForNodes('//active', set_active.query)[0]
    assertEquals('invisible', active['name'])
    acknowledge_iq(stream, set_active.stanza)

    q.unforbid_events([presence_event_pattern])

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_invisible(q, bus, conn, stream):
    conn.Connect()

    handle_get_all_privacy_lists(q, bus, conn, stream, ['invisible'])

    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')

    send_privacy_list (
        stream, get_list.stanza,
        [elem('item', action='deny', order='1')(elem('presence-out'))])

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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
    acknowledge_iq(stream, event.stanza)

    # In order to appear globally invisible, the client MUST now re-send the
    # user's presence for broadcasting to all contacts, which the active rule
    # will block to all contacts:
    q.expect_many(
        EventPattern('stream-presence', to=None, presence_type=None),
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
    acknowledge_iq(stream, event.stanza)

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

def test_privacy_list_push_conflict(q, bus, conn, stream):
    test_invisible_on_connect(q, bus, conn, stream)

    set_id = send_privacy_list_push_iq(stream, "invisible")

    _, req_list = q.expect_many(
        EventPattern('stream-iq', iq_type='result', predicate=lambda event: \
                         event.stanza['id'] == set_id),
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type="get"))

    send_privacy_list(
        stream, req_list.stanza,
        [elem('item', type='jid', value='tybalt@example.com', action='allow',
              order='1')(elem('presence-out')),
         elem('item', action='deny', order='2')(elem('presence-out'))])

    create_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    created = xpath.queryForNodes('//list', create_list.stanza)[0]
    assertEquals(created["name"], 'invisible-gabble')

    acknowledge_iq(stream, create_list.stanza)

    set_active = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    active = xpath.queryForNodes('//active', set_active.query)[0]
    assertEquals('invisible-gabble', active['name'])
    acknowledge_iq(stream, set_active.stanza)

def test_privacy_list_push_valid(q, bus, conn, stream):
    test_invisible_on_connect(q, bus, conn, stream)

    set_id = send_privacy_list_push_iq(stream, "invisible")

    _, req_list = q.expect_many(
        EventPattern('stream-iq', iq_type='result', predicate=lambda event: \
                         event.stanza['id'] == set_id),
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type="get"))

    send_privacy_list (
        stream, req_list.stanza,
        [elem('item', action='deny', order='1')(elem(u'presence-out')),
         elem('item', type='jid', value='tybalt@example.com', action='deny',
              order='2')(elem(u'message'))])

    # We redundantly re-activate the 'invisible' list. These lines also
    # check to see that we didn't switch over to 'invisible-gabble'
    activate_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    active = xpath.queryForNodes('//active', activate_list.stanza)[0]
    assertEquals(active["name"], 'invisible')
    acknowledge_iq (stream, activate_list.stanza)

if __name__ == '__main__':
    exec_test(test_invisible, protocol=Xep0126XmlStream)
    exec_test(test_invisible_on_connect, protocol=Xep0126XmlStream)
    exec_test(test_create_invisible_list, protocol=Xep0126XmlStream)
    exec_test(test_invisible_on_connect_fail_no_list,
              protocol=Xep0126XmlStream)
    exec_test(test_invisible_on_connect_fail_invalid_list,
              protocol=Xep0126XmlStream)
    exec_test(test_privacy_list_push_valid, protocol=Xep0126XmlStream)
    exec_test(test_privacy_list_push_conflict, protocol=Xep0126XmlStream)
