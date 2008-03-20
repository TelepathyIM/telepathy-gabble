
"""
Test network error handling.
"""

import dbus

from gabbletest import go

def expect_connecting(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [1, 1]:
        return False

    return True

def expect_disconnected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    # status: disconnected / reason: network error
    assert event.args == [2, 2]
    return True

if __name__ == '__main__':
    go({'port': dbus.UInt32(4243)})

