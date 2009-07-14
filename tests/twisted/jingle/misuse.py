"""
Test misuse of the streamed media API, which should return error messages
rather than asserting Gabble.
"""

from dbus import DBusException

from servicetest import make_channel_proxy, wrap_channel, assertEquals
from gabbletest import exec_test
from jingletest2 import JingleTest2, JingleProtocol031

import constants as cs

def test(q, bus, conn, stream):
    jp = JingleProtocol031()
    remote_jid = 'foo@example.com/misc'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]
    path, _ = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: remote_handle})

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')

    # In Gabble, the StreamedMedia channel is secretly also the SessionHandler.
    # Let's make up a proxy and call some methods on it. They should fail
    # gracefully, rather than crashing Gabble.
    session_handler = make_channel_proxy(conn, path, 'Media.SessionHandler')

    try:
        session_handler.Ready()
    except DBusException, e:
        assertEquals(cs.NOT_AVAILABLE, e.get_dbus_name())

    try:
        session_handler.Error(0, "slowing down but with a sense of speeding up")
    except DBusException, e:
        assertEquals(cs.NOT_AVAILABLE, e.get_dbus_name())

if __name__ == '__main__':
    exec_test(test)
