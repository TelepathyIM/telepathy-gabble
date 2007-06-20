
"""
Test that redundant calls to SetPresence don't cause anything to happen.
"""

import dbus

from servicetest import match
from gabbletest import go

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    data['conn_presence'] = dbus.Interface(data['conn'],
        'org.freedesktop.Telepathy.Connection.Interface.Presence')

    # Set presence to away. This should cause PresenceUpdate to be emitted,
    # and a new <presence> stanza to be sent to the server.
    data['conn_presence'].SetStatus({'away': {'message': 'gone'}})
    return True

@match('dbus-signal', signal='PresenceUpdate')
def expect_presence_update1(event, data):
    assert event.args == [{1L: (0L, {u'away': {u'message': u'gone'}})}]
    return True

@match('stream-presence')
def expect_presence_stanza1(event, data):
    children = list(event.stanza.elements())
    assert children[0].name == 'show'
    assert str(children[0]) == 'away'
    assert children[1].name == 'status'
    assert str(children[1]) == 'gone'

    # Set presence a second time. Since this call is redundant, there should
    # be no PresenceUpdate or <presence> sent to the server.
    data['conn_presence'].SetStatus({'away':{'message': 'gone'}})

    # Set presence a third time. This call is not redundant, and should
    # generate a signal/message.
    data['conn_presence'].SetStatus({'available': {'message': 'yo'}})

    return True

@match('dbus-signal', signal='PresenceUpdate')
def expect_presence_update2(event, data):
    assert event.args == [{1L: (0L, {u'available': {u'message': u'yo'}})}]
    return True

@match('stream-presence')
def expect_presence_stanza2(event, data):
    children = list(event.stanza.elements())
    assert children[0].name == 'status'
    assert str(children[0]) == 'yo'

    # call SetPresence with no optional arguments, as this used to cause a
    # crash in tp-glib
    data['conn_presence'].SetStatus({'available': {}})

    return True

@match('dbus-signal', signal='PresenceUpdate')
def expect_presence_update3(event, data):
    assert event.args == [{1L: (0L, {u'available': {}})}]
    return True

@match('stream-presence')
def expect_presence_stanza3(event, data):
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

