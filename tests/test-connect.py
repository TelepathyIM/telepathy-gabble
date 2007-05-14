
from twisted.internet import glib2reactor
glib2reactor.install()

from gabbletest import go

def expect_connecting(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [1, 1]:
        return False

    return True

def expect_authenticated(event, data):
    if event[0] != 'stream-authenticated':
        return False

    return True

def expect_presence_update(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'PresenceUpdate':
        return False

    # expecting presence update for self handle
    assert event[3] == [{1L: (0L, {u'available': {}})}]
    return True

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    data['conn'].Disconnect()
    return True

def expect_disconnected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [2, 1]:
        return False

    return True

if __name__ == '__main__':
    go()

