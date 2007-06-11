
"""
Test connecting to a server.
"""

from gabbletest import go

def expect_connecting(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [1, 1]:
        return False

    return True

def expect_authenticated(event, data):
    if event.type != 'stream-authenticated':
        return False

    return True

def expect_presence_update(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'PresenceUpdate':
        return False

    # expecting presence update for self handle
    assert event.args == [{1L: (0L, {u'available': {}})}]
    return True

def expect_connected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [0, 1]:
        return False

    data['conn_iface'].Disconnect()
    return True

def expect_disconnected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [2, 1]:
        return False

    return True

if __name__ == '__main__':
    go()

