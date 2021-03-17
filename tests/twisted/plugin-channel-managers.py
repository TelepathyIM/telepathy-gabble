"""
Test Gabble's implementation of channel managers from plugins.
"""

from servicetest import assertContains
from gabbletest import exec_test
import constants as cs
from config import PLUGINS_ENABLED

TEST_PLUGIN_IFACE = cs.PREFIX + ".Gabble.Plugin.Test"

if not PLUGINS_ENABLED:
    print("NOTE: built without --enable-plugins, not testing plugins")
    raise SystemExit(77) # which makes the test show up as skipped

def test(q, bus, conn, stream):
    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS,
        'RequestableChannelClasses')

    # These values are from plugins/test.c
    fixed = {
        cs.CHANNEL_TYPE: "com.jonnylamb.lolbags",
        cs.TARGET_HANDLE_TYPE: cs.HT_NONE,
    }
    allowed = ["com.jonnylamb.omg", "com.jonnylamb.brokethebuild"]
    assertContains((fixed, allowed), rccs)

if __name__ == '__main__':
    exec_test(test)
