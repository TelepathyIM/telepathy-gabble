"""
Test closing the muc channel while a muji request is in flight
"""

from gabbletest import exec_test
from servicetest import call_async

import constants as cs

muc = "muji@test"

def run_cancel_test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    call_async (q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc,
          cs.CALL_INITIAL_AUDIO: True,
         }, byte_arrays = True)

    q.expect('stream-presence', to = muc + "/test")

    conn.Disconnect()

if __name__ == '__main__':
    exec_test (run_cancel_test)
