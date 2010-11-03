"""
Test Gabble's implementation of sidecars, using the test plugin.
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
    print "      (but still testing failing calls to EnsureSidecar)"

def test(q, bus, conn, stream):
    # Request a sidecar thate we support before we're connected; it should just
    # wait around until we're connected.
    call_async(q, conn.Future, 'EnsureSidecar', TEST_PLUGIN_IFACE)

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    if PLUGINS_ENABLED:
        # Now we're connected, the call we made earlier should return.
        path, props = q.expect('dbus-return', method='EnsureSidecar').value
        # This sidecar doesn't even implement get_immutable_properties; it
        # should just get the empty dict filled in for it.
        assertEquals({}, props)

        # We should get the same sidecar if we request it again
        path2, props2 = conn.Future.EnsureSidecar(TEST_PLUGIN_IFACE)
        assertEquals((path, props), (path2, props2))
    else:
        # Only now does it fail.
        q.expect('dbus-error', method='EnsureSidecar')

    # This is not a valid interface name
    call_async(q, conn.Future, 'EnsureSidecar', 'not an interface')
    q.expect('dbus-error', name=cs.INVALID_ARGUMENT)

    # The test plugin makes no reference to this interface.
    call_async(q, conn.Future, 'EnsureSidecar', 'unsupported.sidecar')
    q.expect('dbus-error', name=cs.NOT_IMPLEMENTED)

    if PLUGINS_ENABLED:
        # This sidecar does have some properties:
        path, props = conn.Future.EnsureSidecar(TEST_PLUGIN_IFACE + ".Props")
        assertContains(TEST_PLUGIN_IFACE + ".Props.Greeting", props)

        # The plugin claims it implements this sidecar, but actually doesn't.
        # Check that we don't blow up (although this is no different from
        # Gabble's perspective to creating a sidecar failing because a network
        # service wasn't there, for instance).
        call_async(q, conn.Future, 'EnsureSidecar',
            TEST_PLUGIN_IFACE + ".Buggy")
        q.expect('dbus-error', name=cs.NOT_IMPLEMENTED)

        # This sidecar sends a stanza, and waits for a reply, before being
        # created.
        pattern = EventPattern('stream-iq', to='sidecar.example.com',
            query_ns='http://example.com/sidecar')
        call_async(q, conn.Future, 'EnsureSidecar', TEST_PLUGIN_IFACE + ".IQ")
        e = q.expect_many(pattern)[0]

        sync_dbus(bus, q, conn)

        # If the server says no, EnsureSidecar should fail.
        send_error_reply(stream, e.stanza)
        q.expect('dbus-error', method='EnsureSidecar', name=cs.NOT_AVAILABLE)

        # Let's try again. The plugin should get a chance to ping the server
        # again.
        call_async(q, conn.Future, 'EnsureSidecar', TEST_PLUGIN_IFACE + ".IQ")
        e = q.expect_many(pattern)[0]

        # The server said yes, so we should get a sidecar back!
        acknowledge_iq(stream, e.stanza)
        q.expect('dbus-return', method='EnsureSidecar')

        # If we ask again once the plugin has been created, it should return at
        # once without any more network traffic.
        q.forbid_events([pattern])
        conn.Future.EnsureSidecar(TEST_PLUGIN_IFACE + ".IQ")
        sync_stream(q, stream)

        # TODO: test ensuring a sidecar that waits for something from the
        # network, disconnecting while it's waiting, and ensuring that nothing
        # breaks regardless of whether the network replies before
        # </stream:stream> or not.

    call_async(q, conn, 'Disconnect')

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-closed'),
        )

    call_async(q, conn.Future, 'EnsureSidecar', 'zomg.what')
    # With older telepathy-glib this would be DISCONNECTED;
    # with newer telepathy-glib the Connection disappears from the bus
    # sooner, and you get UnknownMethod or something from dbus-glib.
    q.expect('dbus-error')

    stream.sendFooter()
    q.expect('dbus-return', method='Disconnect')

if __name__ == '__main__':
    exec_test(test)
