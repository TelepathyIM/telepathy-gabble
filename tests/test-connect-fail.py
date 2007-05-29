
"""
Test network error handling.
"""

import dbus

from gabbletest import go

def expect_connecting(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [1, 1]:
        return False

    return True

def expect_disconnected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    # status: disconnected / reason: network error
    assert event[3] == [2, 2]
    return True

if __name__ == '__main__':
    go({'port': dbus.UInt32(4243)})

