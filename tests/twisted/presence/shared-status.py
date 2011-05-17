# coding=utf-8
"""
A simple smoke-test for Google Shared Status.
See: http://code.google.com/apis/talk/jep_extensions/shared_status.html
"""
from gabbletest import (
    exec_test, XmppXmlStream, acknowledge_iq, send_error_reply,
    disconnect_conn, elem, elem_iq
)
from servicetest import (
    EventPattern, assertEquals, assertNotEquals, assertContains,
    assertDoesNotContain, sync_dbus
)
import ns
import copy
import constants as cs
import dbus
from twisted.words.xish import xpath, domish
from invisible_helper import SharedStatusStream

presence_types = {'available' : cs.PRESENCE_AVAILABLE,
                  'away'      : cs.PRESENCE_AWAY,
                  'xa'        : cs.PRESENCE_EXTENDED_AWAY,
                  'hidden'    : cs.PRESENCE_HIDDEN,
                  'dnd'       : cs.PRESENCE_BUSY}

def _show_to_shared_status_show(show):
    # Away and extended away don't use shared status.
    shared_show = 'default'
    if show == 'dnd':
        shared_show = 'dnd'

    shared_invisible = 'false'
    if show == 'hidden':
        shared_invisible = 'true'

    return shared_show, shared_invisible

def _test_remote_status(q, bus, conn, stream, msg, show, list_attrs):
    self = conn.GetSelfHandle()
    presence = conn.SimplePresence.GetPresences([self])[self]
    is_away = presence[0] in (cs.PRESENCE_AWAY, cs.PRESENCE_EXTENDED_AWAY)

    if is_away:
        # Away is per connection. If we remotely change the shared status,
        # we have to stay away.
        _test_remote_status_away(q, bus, conn, stream, msg, show, list_attrs)
    else:
        # If we are not away and the remote status changes, we have to change
        # also our local presence to reflect that.
        _test_remote_status_not_away(q, stream, msg, show, list_attrs)

def _test_remote_status_away(q, bus, conn, stream, msg, show, list_attrs):
    events = [EventPattern('dbus-signal', signal='PresenceUpdate',
                           interface=cs.CONN_IFACE_PRESENCE,
                           args=[{1: (0, {show: {'message': msg}})}]),
              EventPattern('dbus-signal', signal='PresencesChanged',
                           interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                           args=[{1: (presence_types[show], show, msg)}])]
    q.forbid_events(events)

    list_attrs['status'] = list_attrs.get('status', msg)
    stream.set_shared_status_lists(**list_attrs)

    q.expect('stream-iq', iq_type='result')

    sync_dbus(bus, q, conn)

    q.unforbid_events(events)

def _test_remote_status_not_away(q, stream, msg, show, list_attrs):
    list_attrs['status'] = list_attrs.get('status', msg)
    stream.set_shared_status_lists(**list_attrs)

    q.expect('stream-iq', iq_type='result')

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {show: {'message': msg}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (presence_types[show], show, msg)}]))

def _test_local_status(q, conn, stream, msg, show, expected_show=None):
    expected_show = expected_show or show
    away = expected_show in ('away', 'xa')

    self = conn.GetSelfHandle()
    prev_presence = conn.SimplePresence.GetPresences([self])[self]
    was_away = prev_presence[0] in (cs.PRESENCE_AWAY,
                                    cs.PRESENCE_EXTENDED_AWAY)
    was_invisible = (prev_presence[0] == cs.PRESENCE_HIDDEN)

    if away:
        # Away and extended away are mapped to idle, that is per connection.
        # This means we use <presence/> instead of shared presence...
        if not was_invisible:
            # ... so in normal cases we don't expect the shared presence
            # stuff, but ...
            wrong_presence_pattern = EventPattern('stream-iq',
                    query_ns=ns.GOOGLE_SHARED_STATUS, iq_type='set')
        else:
            # ... when switching from invisible we have to leave invisible
            # first and then go away.
            wrong_presence_pattern = None
    elif was_away:
        # Non-away status, but we were away previously. Considering that we
        # went away using <presence/>, we need to also leave it using
        # <presence/> plus the shared status.
        wrong_presence_pattern = None
    else:
        # Normal case without away involvement; we just expect the use of
        # shared status.
        wrong_presence_pattern = EventPattern('stream-presence')

    if wrong_presence_pattern:
        q.forbid_events([wrong_presence_pattern])

    conn.SimplePresence.SetPresence(show, msg)

    max_status_message_length = int(stream.max_status_message_length)

    if not away or (away and was_invisible):
        event = q.expect('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                                     iq_type='set')

        shared_show, shared_invisible = _show_to_shared_status_show(expected_show)

        _status = xpath.queryForNodes('//status', event.query)[0]
        assertEquals(msg[:max_status_message_length], _status.children[0])
        _show = xpath.queryForNodes('//show', event.query)[0]
        assertEquals(shared_show, _show.children[0])
        _invisible = xpath.queryForNodes('//invisible', event.query)[0]
        assertEquals(shared_invisible, _invisible.getAttribute('value'))

        if was_away or (away and was_invisible):
            q.expect('stream-presence')
    else:
        q.expect('stream-presence')

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {expected_show: {'message':
                            msg[:max_status_message_length]}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (presence_types[expected_show], expected_show,
                            msg[:max_status_message_length])}]))

    if wrong_presence_pattern:
        q.unforbid_events([wrong_presence_pattern])

def test(q, bus, conn, stream):
    q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='get'),
                  EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='set'),
                  EventPattern('stream-presence'))

    # Set shared status to dnd.
    _test_local_status(q, conn, stream, "Don't disturb, buddy.", "dnd")

    # Test maximum status message length
    max_status_message_length = int(stream.max_status_message_length)
    _test_local_status(q, conn, stream, "ab" * max_status_message_length, "dnd")

    # Test invisibility
    _test_local_status(q, conn, stream, "Peekabo", "hidden")
    _test_local_status(q, conn, stream, "Here!", "available")

    # Set shared status to default, local status to away.
    _test_local_status(q, conn, stream, "I'm away right now", "away")
    _test_local_status(q, conn, stream, "cd" * max_status_message_length, "away")

    # Test the transition from away to non-away as GTalk treats it in a
    # different way.
    _test_local_status(q, conn, stream, "Here!", "available")
    _test_local_status(q, conn, stream, "I'm away right now", "away")
    _test_local_status(q, conn, stream, "I'm away right now", "xa")
    _test_local_status(q, conn, stream, "Here!", "available")

    # Test the transition from hidden to away.
    _test_local_status(q, conn, stream, "Peekabo", "hidden")
    _test_local_status(q, conn, stream, "I'm away right now", "away")

    # Test if the status is changed correctly from another client.
    _test_local_status(q, conn, stream, "Don't disturb, buddy.", "dnd")
    _test_remote_status(q, bus, conn, stream,
                        "This is me busy, set from another client.",
                        "dnd", {"show" : "dnd"})
    _test_remote_status(q, bus, conn, stream,
                        "This is me available, set from another client.",
                        "available", {"show" : "default"})

    # Test that our own status as exposed over D-Bus doesn't change when
    # when we are away and other clients change the shared status.
    _test_local_status(q, conn, stream, "Lunch!", "away")
    _test_remote_status(q, bus, conn, stream,
                        "This is me busy, set from another client.",
                        "dnd", {"show" : "dnd"})
    _test_remote_status(q, bus, conn, stream,
                        "This is me available, set from another client.",
                        "available", {"show" : "default"})

    # Change min version
    stream.set_shared_status_lists(min_version="1")
    q.expect('stream-iq', iq_type='result')

    # Going invisible now should fail, we should just be dnd.
    _test_local_status(q, conn, stream, "Peekabo", "hidden", "dnd")

    # Let's go back to version 2
    stream.set_shared_status_lists(min_version="2")
    q.expect('stream-iq', iq_type='result')

    # "hidden" should work again.
    _test_local_status(q, conn, stream, "Peekabo", "hidden")

    # Changing min version mid-flight should make us 'dnd'
    stream.set_shared_status_lists(min_version="1")
    q.expect('stream-iq', iq_type='result')

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {'dnd': {'message': "Peekabo"}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (cs.PRESENCE_BUSY, 'dnd', "Peekabo")}]))

def _test_on_connect(q, bus, conn, stream, shared_status, show, msg, expected_show=None):
    expected_show = expected_show or show
    _status, _show, _invisible = shared_status
    stream.shared_status = shared_status

    forbidden_event_patterns = [EventPattern('stream-presence'),
                                EventPattern('stream-iq', query_ns=ns.PRIVACY,
                                             iq_type='get')]
    q.forbid_events(forbidden_event_patterns)

    conn.SimplePresence.SetPresence(show, msg)
    conn.Connect()

    _, event = q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                                          iq_type='get'),
                             EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                                          iq_type='set'))

    shared_show, shared_invisible = _show_to_shared_status_show(show)

    _status = xpath.queryForNodes('//status', event.query)[0]
    assertEquals(msg, _status.children[0])
    _show = xpath.queryForNodes('//show', event.query)[0]
    assertEquals(shared_show, _show.children[0])
    _invisible = xpath.queryForNodes('//invisible', event.query)[0]
    assertEquals(shared_invisible, _invisible.getAttribute('value'))

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {expected_show: {'message': msg}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (presence_types[expected_show],
                                expected_show, msg)}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

    q.unforbid_events(forbidden_event_patterns)

def test_connect_available(q, bus, conn, stream):
    _test_on_connect(q, bus, conn, stream,  ("I'm busy, buddy.", 'dnd', 'false'),
                     'available', "I'm here, baby.")

def test_connect_chat(q, bus, conn, stream):
    _test_on_connect(q, bus, conn, stream,  ("I'm busy, buddy.", 'dnd', 'false'),
                     'chat', "Do you want to chat?", 'available')

def test_connect_dnd(q, bus, conn, stream):
    _test_on_connect(q, bus, conn, stream,  ("Chat with me.", 'default', 'false'),
                     'dnd', "I'm busy, buddy.")

def test_connect_hidden(q, bus, conn, stream):
    _test_on_connect(q, bus, conn, stream,  ("Chat with me.", 'default', 'false'),
                     'hidden', "I see, but I can't be seen")

def test_connect_hidden_not_available(q, bus, conn, stream):
    """Fall back to DND if you try to connect while invisible, but shared status is not
    completely supported."""
    _status, _show, _invisible = "Chat with me.", 'default', 'false'
    msg = "I see, but I don't think I can be seen"
    show = "hidden"

    stream.shared_status = (_status, _show, _invisible)
    stream.min_version = "1"

    presence_event_pattern = EventPattern('stream-presence')
    q.forbid_events([presence_event_pattern])

    conn.SimplePresence.SetPresence(show, msg)
    conn.Connect()

    _, event = q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                                          iq_type='get'),
                             EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                                          iq_type='set'))

    _status = xpath.queryForNodes('//status', event.query)[0]
    assertEquals(msg, _status.children[0])
    _show = xpath.queryForNodes('//show', event.query)[0]
    assertEquals("dnd", _show.children[0])
    _invisible = xpath.queryForNodes('//invisible', event.query)[0]
    assertEquals("false", _invisible.getAttribute('value'))

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {"dnd": {'message': msg}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (cs.PRESENCE_BUSY, "dnd", msg)}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

    q.unforbid_events([presence_event_pattern])

def test_shared_status_list(q, bus, conn, stream):
    '''Test the shared status list usage'''
    test_statuses = {"dnd"        : ['I am not available now',
                                     'I am busy with real work',
                                     'I have a life, you know...',
                                     'I am actually playing Duke Nukem',
                                     'It is important to me'],
                     "available"  : ['I am twiddling my thumbs',
                                     'Please chat me up',
                                     'I am here for you',
                                     'Message me already!']}

    max_statuses = int(stream.max_statuses)

    q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='get'),
                  EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='set'),
                  EventPattern('stream-presence'))

    for show, statuses in test_statuses.items():
        shared_show, _ = _show_to_shared_status_show(show)
        expected_list = stream.shared_status_lists[shared_show]
        for status in statuses:
            _test_local_status(q, conn, stream, status, show)
            expected_list = [status] + expected_list[:max_statuses - 1]
            assertEquals(expected_list, stream.shared_status_lists[shared_show])

def test_shared_status_away(q, bus, conn, stream):
    '''Test the shared status lists with away statuses'''
    q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='get'),
                  EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='set'),
                  EventPattern('stream-presence'))

    expected_list = copy.deepcopy(stream.shared_status_lists)
    for show in ('away', 'xa'):
        for status in ('not going to', 'be actually set'):
            _test_local_status(q, conn, stream, status, show)
            assertEquals(expected_list, stream.shared_status_lists)

def test_shared_status_chat(q, bus, conn, stream):
    '''Test that 'chat' is not supported with shared status'''
    q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='get'),
                  EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='set'),
                  EventPattern('stream-presence'))

    try:
        conn.SimplePresence.SetPresence('chat', 'This is not going to work')
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.NOT_AVAILABLE
    else:
        assert False

if __name__ == '__main__':
    exec_test(test, protocol=SharedStatusStream)
    exec_test(test_connect_available, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_connect_chat, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_connect_dnd, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_connect_hidden, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_connect_hidden_not_available, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_shared_status_list, protocol=SharedStatusStream)
    exec_test(test_shared_status_away, protocol=SharedStatusStream)
    exec_test(test_shared_status_chat, protocol=SharedStatusStream)
