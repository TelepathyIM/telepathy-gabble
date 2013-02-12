"""
Test closing the muc channel while a muji request is in flight
"""

from gabbletest import exec_test, disconnect_conn
from servicetest import call_async

import constants as cs

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

muc = "muji@test"

def run_cancel_test(q, bus, conn, stream):
    call_async (q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc,
          cs.CALL_INITIAL_AUDIO: True,
         }, byte_arrays = True)

    q.expect('stream-presence', to = muc + "/test")

    disconnect_conn(q, conn, stream)

if __name__ == '__main__':
    exec_test (run_cancel_test)
