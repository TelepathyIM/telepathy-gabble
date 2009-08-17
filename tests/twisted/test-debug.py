
"""
Test the debug message interface.
"""

import dbus

from gabbletest import exec_test
import constants as cs
from config import DEBUGGING

if not DEBUGGING:
    print " --   Not testing debugger, built with --disable-debug"
    raise SystemExit(77)

path = '/org/freedesktop/Telepathy/debug'
iface = 'org.freedesktop.Telepathy.Debug'

def test(q, bus, conn, stream):
    messages = []

    def new_message(timestamp, domain, level, string):
        messages.append((timestamp, domain, level, string))

    debug = bus.get_object(conn.bus_name, path)
    debug_iface = dbus.Interface(debug, iface)
    debug_iface.connect_to_signal('NewDebugMessage', new_message)
    props_iface = dbus.Interface(debug, cs.PROPERTIES_IFACE)

    assert len(debug_iface.GetMessages()) > 0

    # Turn signalling on and generate some messages.

    assert len(messages) == 0
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
    assert props_iface.Get(iface, 'Enabled') == False
    props_iface.Set(iface, 'Enabled', True)

    conn.RequestChannel(
        cs.CHANNEL_TYPE_TEXT, cs.HT_CONTACT, conn.GetSelfHandle(), True)
    q.expect('dbus-signal', signal='NewChannel')
    assert len(messages) > 0

    # Turn signalling off and check we have no new messages.

    props_iface.Set(iface, 'Enabled', False)
    snapshot = list(messages)

    assert snapshot == messages

if __name__ == '__main__':
    exec_test(test)

