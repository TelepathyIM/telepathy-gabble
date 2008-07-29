
"""
Test that redundant calls to SetPresence don't cause anything to happen.
"""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    presence_iface = dbus.Interface (conn,
        u'org.freedesktop.Telepathy.Connection.Interface.Presence')

    # Set presence to away. This should cause PresenceUpdate to be emitted,
    # and a new <presence> stanza to be sent to the server.
    presence_iface.SetStatus({'away': {'message': 'gone'}})

    signal, event = q.expect_many (
        EventPattern('dbus-signal', signal='PresenceUpdate'),
        EventPattern('stream-presence'))
    signal.args == [{1L: (0L, {u'away': {u'message': u'gone'}})}]

    children = list(event.stanza.elements())
    assert children[0].name == 'show'
    assert str(children[0]) == 'away'
    assert children[1].name == 'status'
    assert str(children[1]) == 'gone'

    # Set presence a second time. Since this call is redundant, there should
    # be no PresenceUpdate or <presence> sent to the server.
    presence_iface.SetStatus({'away':{'message': 'gone'}})

    # Set presence a third time. This call is not redundant, and should
    # generate a signal/message.
    presence_iface.SetStatus({'available': {'message': 'yo'}})

    signal, event = q.expect_many (
        EventPattern('dbus-signal', signal='PresenceUpdate'),
        EventPattern('stream-presence'))

    assert signal.args == [{1L: (0L, {u'available': {u'message': u'yo'}})}]
    children = list(event.stanza.elements())
    assert children[0].name == 'status'
    assert str(children[0]) == 'yo'

    # call SetPresence with no optional arguments, as this used to cause a
    # crash in tp-glib
    presence_iface.SetStatus({'available': {}})


    signal, _ = q.expect_many (
        EventPattern('dbus-signal', signal='PresenceUpdate'),
        EventPattern('stream-presence'))
    assert signal.args == [{1L: (0L, {u'available': {}})}]

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
