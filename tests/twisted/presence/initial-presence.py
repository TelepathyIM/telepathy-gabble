
"""
A simple smoke-test for C.I.SimplePresence

FIXME: test C.I.Presence too
"""

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import EventPattern, assertEquals, assertNotEquals
import ns
import constants as cs

def test(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])
    conn.SimplePresence.SetPresence("away", "watching bees")

    conn.Connect()
    _, presence = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-presence'),
        )

    children = list(presence.stanza.elements())
    assertEquals('show', children[0].name)
    assertEquals('away', children[0].children[0])
    assertEquals('status', children[1].name)
    assertEquals('watching bees', children[1].children[0])

if __name__ == '__main__':
    exec_test(test)
