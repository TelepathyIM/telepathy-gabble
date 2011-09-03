"""
Test that redundant calls to SetPresence don't cause anything to happen.
"""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test
import constants as cs
from invisible_helper import Xep0186Stream, ValidInvisibleListStream, \
    Xep0186AndValidInvisibleListStream

def run_test(q, bus, conn, stream):
    # Expect an initial presence push from the client.
    q.expect('stream-presence')

    # Set presence to away. This should cause PresencesChanged to be emitted,
    # and a new <presence> stanza to be sent to the server.
    conn.SimplePresence.SetPresence('away', 'gone')

    simple_signal, presence = q.expect_many (
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('stream-presence'))
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
    # be no PresencesChanged or <presence> sent to the server.
    conn.SimplePresence.SetPresence('away', 'gone')
    assert conn.Contacts.GetContactAttributes([1], [cs.CONN_IFACE_SIMPLE_PRESENCE], False) == { 1L:
      { cs.CONN_IFACE_SIMPLE_PRESENCE + "/presence": (3L, u'away', u'gone'),
        'org.freedesktop.Telepathy.Connection/contact-id':
            'test@localhost'}}

    # Set presence a third time. This call is not redundant, and should
    # generate a signal/message.
    conn.SimplePresence.SetPresence('available', 'yo')

    simple_signal, presence = q.expect_many (
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('stream-presence'))

    assert simple_signal.args == [{1L: (2L, u'available',  u'yo')}]
    children = list(presence.stanza.elements())
    assert children[0].name == 'status'
    assert str(children[0]) == 'yo'
    assert conn.Contacts.GetContactAttributes([1], [cs.CONN_IFACE_SIMPLE_PRESENCE], False) == { 1L:
      { cs.CONN_IFACE_SIMPLE_PRESENCE + "/presence": (2L, u'available', u'yo'),
        'org.freedesktop.Telepathy.Connection/contact-id':
            'test@localhost'}}

    # call SetPresence with an empty message, as this used to cause a
    # crash in tp-glib
    conn.SimplePresence.SetPresence('available', '')

    simple_signal, presence = q.expect_many (
        EventPattern('dbus-signal', signal='PresencesChanged'),
        EventPattern('stream-presence'))
    assert simple_signal.args == [{1L: (2L, u'available',  u'')}]
    assert conn.Contacts.GetContactAttributes([1], [cs.CONN_IFACE_SIMPLE_PRESENCE], False) == { 1L:
      { cs.CONN_IFACE_SIMPLE_PRESENCE + "/presence": (2L, u'available', u''),
        'org.freedesktop.Telepathy.Connection/contact-id':
            'test@localhost'}}

if __name__ == '__main__':
    exec_test(run_test)
    # Run this test against some invisibility-capable servers, even though we
    # don't use invisibility, to check that invisibility support doesn't break
    # us. This is a regression test for
    # <https://bugs.freedesktop.org/show_bug.cgi?id=30117>. It turned out that
    # XEP-0126 support meant that our own presence changes were never
    # signalled.
    for protocol in [Xep0186AndValidInvisibleListStream, Xep0186Stream,
                     ValidInvisibleListStream]:
        exec_test(run_test, protocol=protocol)
        exec_test(run_test, protocol=protocol)

