"""
Test basic outgoing and incoming call handling
"""
from gabbletest import exec_test
from servicetest import call_async, assertEquals, assertNotEquals
from jingletest2 import JingleProtocol031

import constants as cs
from callutils import *

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

muc = "muji@test"

def run_cancel_test(q, bus, conn, stream):
    jp = JingleProtocol031 ()
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', muc + '/bob')
    jt.prepare()

    for x in xrange (0, 10):
        (path, props) = create_muji_channel (q, conn, stream, muc, x > 0)
        channel = bus.get_object (conn.bus_name, path)

        contents = channel.Get (cs.CHANNEL_TYPE_CALL, "Contents",
            dbus_interface = dbus.PROPERTIES_IFACE)

        content = bus.get_object (conn.bus_name, contents[0])

        md = jt.get_call_audio_md_dbus()
        check_and_accept_offer (q, bus, conn, content, md)

        # Accept the channel
        channel.Accept()

        def preparing(e):
            node = xpath.queryForNodes("/presence/muji/preparing", e.stanza)
            return node is not None

        q.expect('stream-presence', to = muc + "/test", predicate=preparing)

        channel.Hangup(0, "", "",
            dbus_interface=cs.CHANNEL_TYPE_CALL)

        def notpreparing(e):
            node = xpath.queryForNodes("/presence/muji/preparing", e.stanza)
            return node is None

        q.expect('stream-presence', to = muc + "/test", predicate=notpreparing)

        if x % 2 == 0:
            channel.Close()

if __name__ == '__main__':
    exec_test (run_cancel_test)
