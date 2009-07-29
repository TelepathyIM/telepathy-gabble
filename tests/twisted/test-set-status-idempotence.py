
"""
Test that redundant calls to SetPresence don't cause anything to happen.
"""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test
import constants as cs

def test_presence(q, bus, conn, stream):
    conn.Connect()

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-presence'))

    iface = dbus.Interface (conn,
        u'org.freedesktop.Telepathy.Connection.Interface.Presence')

    def set_presence (status, message = None):
        if message:
          iface.SetStatus({status: {'message': message}})
        else:
          iface.SetStatus({status: {}})

    run_test(q, bus, conn, stream, set_presence)

def test_simple_presence(q, bus, conn, stream):
    conn.Connect()

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-presence'))

    iface = dbus.Interface (conn, cs.CONN_IFACE_SIMPLE_PRESENCE)
    run_test(q, bus, conn, stream,
      (lambda status, message = "": iface.SetPresence (status, message)))

def run_test(q, bus, conn, stream, set_status_func):
    # Set presence to away. This should cause PresenceUpdate to be emitted,
    # and a new <presence> stanza to be sent to the server.
    set_status_func('away', 'gone')

    signal, simple_signal, presence = q.expect_many (
        EventPattern('dbus-signal', signal='PresenceUpdate'),
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('stream-presence'))
    assert signal.args == [{1L: (0L, {u'away': {u'message': u'gone'}})}]
    assert simple_signal.args == [{1L: (3L, u'away',  u'gone')}]
    assert conn.Contacts.GetContactAttributes([1], [cs.CONN_IFACE_SIMPLE_PRESENCE], False) == { 1L:
      { cs.CONN_IFACE_SIMPLE_PRESENCE + "/presence": (3L, u'away', u'gone'),
        'org.freedesktop.Telepathy.Connection/contact-id':
            'test@localhost'}}

    children = list(presence.stanza.elements())
    assert children[0].name == 'show'
    assert str(children[0]) == 'away'
    assert children[1].name == 'status'
    assert str(children[1]) == 'gone'

    # Set presence a second time. Since this call is redundant, there should
    # be no PresenceUpdate or <presence> sent to the server.
    set_status_func('away', 'gone')
    assert conn.Contacts.GetContactAttributes([1], [cs.CONN_IFACE_SIMPLE_PRESENCE], False) == { 1L:
      { cs.CONN_IFACE_SIMPLE_PRESENCE + "/presence": (3L, u'away', u'gone'),
        'org.freedesktop.Telepathy.Connection/contact-id':
            'test@localhost'}}

    # Set presence a third time. This call is not redundant, and should
    # generate a signal/message.
    set_status_func('available', 'yo')

    signal, simple_signal, presence = q.expect_many (
        EventPattern('dbus-signal', signal='PresenceUpdate'),
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('stream-presence'))

    assert signal.args == [{1L: (0L, {u'available': {u'message': u'yo'}})}]
    assert simple_signal.args == [{1L: (2L, u'available',  u'yo')}]
    children = list(presence.stanza.elements())
    assert children[0].name == 'status'
    assert str(children[0]) == 'yo'
    assert conn.Contacts.GetContactAttributes([1], [cs.CONN_IFACE_SIMPLE_PRESENCE], False) == { 1L:
      { cs.CONN_IFACE_SIMPLE_PRESENCE + "/presence": (2L, u'available', u'yo'),
        'org.freedesktop.Telepathy.Connection/contact-id':
            'test@localhost'}}

    # call SetPresence with no optional arguments, as this used to cause a
    # crash in tp-glib
    set_status_func('available')

    signal, simple_signal, presence = q.expect_many (
        EventPattern('dbus-signal', signal='PresenceUpdate'),
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('stream-presence'))
    assert signal.args == [{1L: (0L, {u'available': {}})}]
    assert simple_signal.args == [{1L: (2L, u'available',  u'')}]
    assert conn.Contacts.GetContactAttributes([1], [cs.CONN_IFACE_SIMPLE_PRESENCE], False) == { 1L:
      { cs.CONN_IFACE_SIMPLE_PRESENCE + "/presence": (2L, u'available', u''),
        'org.freedesktop.Telepathy.Connection/contact-id':
            'test@localhost'}}

if __name__ == '__main__':
    exec_test(test_simple_presence)
    exec_test(test_presence)

