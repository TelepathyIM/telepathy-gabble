"""
Tests setting your own presence before calling Connect(), allowing the user to
sign in as Busy/Invisible/whatever rather than available.
"""

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import EventPattern, assertEquals, assertNotEquals
import ns
import constants as cs
from invisible_helper import ValidInvisibleListStream, Xep0186XmlStream

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
    exec_test(test, protocol=ValidInvisibleListStream)
    exec_test(test, protocol=Xep0186XmlStream)
