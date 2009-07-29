"""
Regression test for a bug where, if you had any MUC Tubes channels open and
changed your presence, a GabbleMucChannel method was called on a
GabbleTubesChannel, crashing Gabble.
"""

from gabbletest import exec_test
import constants as cs

from muctubeutil import get_muc_tubes_channel

def test(q, bus, conn, stream):
    conn.Connect()

    _ = q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    handle, tubes_chan, tubes_iface = get_muc_tubes_channel(q, bus, conn,
        stream, 'chat@conf.localhost')

    conn.Presence.SetStatus({'away':{'message':'Christmas lunch!'}})

if __name__ == '__main__':
    exec_test(test)
