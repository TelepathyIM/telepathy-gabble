
"""
Test the debug message interface.
"""

from servicetest import assertEquals, sync_dbus, call_async, ProxyWrapper
from servicetest import EventPattern
from gabbletest import exec_test
import constants as cs
from config import DEBUGGING

def test(q, bus, conn, stream):
    messages = []

    def new_message(timestamp, domain, level, string):
        messages.append((timestamp, domain, level, string))

    debug = ProxyWrapper(bus.get_object(conn.bus_name, cs.DEBUG_PATH),
            cs.DEBUG_IFACE)
    debug.connect_to_signal('NewDebugMessage', new_message)

    if not DEBUGGING:
        # If we're built with --disable-debug, check that the Debug object
        # isn't present.
        call_async(q, debug, 'GetMessages')
        q.expect('dbus-error', method='GetMessages')
        return

    assert len(debug.GetMessages()) > 0

    # Turn signalling on and generate some messages.

    assert len(messages) == 0
    assert debug.Properties.Get(cs.DEBUG_IFACE, 'Enabled') == False
    debug.Properties.Set(cs.DEBUG_IFACE, 'Enabled', True)

    channel_path, props = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: conn.Properties.Get(cs.CONN, "SelfHandle")
        })
    q.expect('dbus-signal', signal = 'NewDebugMessage')

    assert len(messages) > 0

    # Turn signalling off and check we don't get any more messages.

    debug.Properties.Set(cs.DEBUG_IFACE, 'Enabled', False)
    sync_dbus(bus, q, conn)
    snapshot = list(messages)

    channel = bus.get_object(conn.bus_name, channel_path)
    channel.Close(dbus_interface=cs.CHANNEL)
    q.expect('dbus-signal', signal='Closed')

    channel_path, props = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: conn.Properties.Get(cs.CONN, "SelfHandle")
        })
    q.expect('dbus-signal', signal='NewChannels')

    assertEquals (snapshot, messages)

if __name__ == '__main__':
    exec_test(test)

