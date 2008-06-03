
"""
Test network error handling.
"""

import dbus

from gabbletest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 2])

if __name__ == '__main__':
    exec_test(test, {'port': dbus.UInt32(4243)})

