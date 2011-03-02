"""
Test Gabble's implementation of channel managers from plugins.
"""

from servicetest import (
    sync_dbus, call_async, EventPattern, assertEquals, assertContains,
    )
from gabbletest import exec_test, send_error_reply, acknowledge_iq, sync_stream
import constants as cs
from config import PLUGINS_ENABLED

TEST_PLUGIN_IFACE = "org.freedesktop.Telepathy.Gabble.Plugin.Test"

if not PLUGINS_ENABLED:
    print "NOTE: built without --enable-plugins, not testing plugins"
    raise SystemExit(77) # which makes the test show up as skipped

def test(q, bus, conn, stream):
    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS, 'RequestableChannelClasses')

    # these values are from the test plugin.
    assertContains(({'cookies': 'lolbags'}, ['omg', 'hi mum!']), rccs)

if __name__ == '__main__':
    exec_test(test)
