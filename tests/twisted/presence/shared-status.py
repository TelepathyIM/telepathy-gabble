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
    assertDoesNotContain
)
import ns
import constants as cs
from twisted.words.xish import xpath, domish
from invisible_helper import SharedStatusStream

presence_types = {'available' : cs.PRESENCE_AVAILABLE,
                  'away'      : cs.PRESENCE_AWAY,
                  'hidden'    : cs.PRESENCE_HIDDEN,
                  'dnd'       : cs.PRESENCE_BUSY}

def _show_to_shared_status_show(show):
    shared_show = 'default'
    if show == 'dnd':
        shared_show = 'dnd'

    shared_invisible = 'false'
    if show == 'hidden':
        shared_invisible = 'true'

    return shared_show, shared_invisible

def _test_remote_status(q, stream, msg, show, list_attrs):
    list_attrs['status'] = list_attrs.get('status') or msg
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

    shared_show, shared_invisible = _show_to_shared_status_show(expected_show)

    conn.SimplePresence.SetPresence(show, msg)

    event = q.expect('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                                 iq_type='set')

    max_status_message_length = int(stream.max_status_message_length)

    _status = xpath.queryForNodes('//status', event.query)[0]
    assertEquals(msg[:max_status_message_length], _status.children[0])
    _show = xpath.queryForNodes('//show', event.query)[0]
    assertEquals(shared_show, _show.children[0])
    _invisible = xpath.queryForNodes('//invisible', event.query)[0]
    assertEquals(shared_invisible, _invisible.getAttribute('value'))

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {expected_show: {'message':
                            msg[:max_status_message_length]}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (presence_types[expected_show], expected_show,
                            msg[:max_status_message_length])}]))


def test(q, bus, conn, stream):
    q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='get'),
                  EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='set'))

    # Set shared status to dnd.
    _test_local_status(q, conn, stream, "Don't disturb, buddy.", "dnd")

    # Test maximum status message length
    max_status_message_length = int(stream.max_status_message_length)
    _test_local_status(q, conn, stream, "ab" * max_status_message_length, "dnd")

    # Test invisibility
    _test_local_status(q, conn, stream, "Peekabo", "hidden")

    # Set shared status to default, local status to away.
    _test_local_status(q, conn, stream, "I'm away right now", "away")

    # Status changes from another client
    _test_remote_status(q, stream, "This is me, set from another client.",
                        "away", {"show" : "default"})

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

def _test_on_connect(q, bus, conn, stream, shared_status, show, msg):
    _status, _show, _invisible = shared_status
    stream.shared_status = shared_status

    presence_event_pattern = EventPattern('stream-presence')
    q.forbid_events([presence_event_pattern])

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

    q.unforbid_events([presence_event_pattern])

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {show: {'message': msg}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (presence_types[show], show, msg)}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

def test_connect_available(q, bus, conn, stream):
    _test_on_connect(q, bus, conn, stream,  ("I'm busy, buddy.", 'dnd', 'false'),
                     'available', "I'm here, baby.")

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

    q.unforbid_events([presence_event_pattern])

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
                     interface=cs.CONN_IFACE_PRESENCE,
                     args=[{1: (0, {"dnd": {'message': msg}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
                     interface=cs.CONN_IFACE_SIMPLE_PRESENCE,
                     args=[{1: (cs.PRESENCE_BUSY, "dnd", msg)}]),
        EventPattern('dbus-signal', signal='StatusChanged',
                     args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

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
                                     'Message me already!'],
                     "away"       : ['I am not around',
                                     'Don\'t bother...',
                                     'I am awaaaaaaaay']}

    max_statuses = int(stream.max_statuses)

    q.expect_many(EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='get'),
                  EventPattern('stream-iq', query_ns=ns.GOOGLE_SHARED_STATUS,
                               iq_type='set'))

    for show, statuses in test_statuses.items():
        shared_show, _ = _show_to_shared_status_show(show)
        expected_list = stream.shared_status_lists[shared_show]
        for status in statuses:
            _test_local_status(q, conn, stream, status, show)
            expected_list = [status] + expected_list[:max_statuses - 1]
            assertEquals(expected_list, stream.shared_status_lists[shared_show])

if __name__ == '__main__':
    exec_test(test, protocol=SharedStatusStream)
    exec_test(test_connect_available, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_connect_dnd, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_connect_hidden, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_connect_hidden_not_available, protocol=SharedStatusStream, do_connect=False)
    exec_test(test_shared_status_list, protocol=SharedStatusStream)
