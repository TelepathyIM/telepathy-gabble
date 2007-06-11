
"""
Test connecting to a server.
"""

from servicetest import match
from gabbletest import go

@match('dbus-signal', signal='StatusChanged', args=[1, 1])
def expect_connecting(event, data):
    return True

@match('stream-authenticated')
def expect_authenticated(event, data):
    return True

@match('dbus-signal', signal='PresenceUpdate')
def expect_presence_update(event, data):
    # expecting presence update for self handle
    assert event.args == [{1L: (0L, {u'available': {}})}]
    return True

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

