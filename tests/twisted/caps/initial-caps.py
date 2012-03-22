"""
Test initial capabilities
"""

import dbus

from twisted.words.xish import xpath, domish

from servicetest import EventPattern
from gabbletest import exec_test, sync_stream
from caps_helper import check_caps, disco_caps
import constants as cs
import ns

from config import VOIP_ENABLED

def run_test(q, bus, conn, stream):
    initial_presence = q.expect('stream-presence')

    _, namespaces, _ = disco_caps(q, stream, initial_presence)

    # For some reason, until we advertise any capabilities, these caps turn
    # up in our presence
    if VOIP_ENABLED:
        caps = [
            ns.JINGLE,
            ns.JINGLE_015,
            ns.JINGLE_TRANSPORT_ICEUDP,
            ns.JINGLE_TRANSPORT_RAWUDP,
            ns.GOOGLE_P2P
            ]
    else:
        caps = []

    check_caps(namespaces, caps)

if __name__ == '__main__':
    exec_test(run_test)
