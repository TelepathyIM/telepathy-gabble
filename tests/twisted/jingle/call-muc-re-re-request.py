"""
Test basic outgoing and incoming call handling
"""
from gabbletest import exec_test
from servicetest import call_async, assertEquals, assertNotEquals
from jingletest2 import JingleProtocol031

import constants as cs
from callutils import *

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
        codecs = jt.get_call_audio_codecs_dbus()
        content.SetCodecs(codecs)
        channel.Accept()

        e = q.expect('stream-presence', to = muc + "/test")
        mujinode = xpath.queryForNodes("/presence/muji/preparing", e.stanza)
        assertNotEquals(None, mujinode)

        channel.Hangup(0, "", "",
            dbus_interface=cs.CHANNEL_TYPE_CALL)

        e = q.expect('stream-presence', to = muc + "/test")
        mujinode = xpath.queryForNodes("/presence/muji/preparing", e.stanza)
        assertEquals(None, mujinode)

        if x % 2 == 0:
            channel.Close()

if __name__ == '__main__':
    exec_test (run_cancel_test)
